import { realpath, stat } from 'node:fs/promises';
import path from 'node:path';
import { AdapterError } from './errors.js';

export interface Config {
  cli: string;
  roots: string[];
  allProcesses: boolean;
  pids: Set<number>;
}

function localPath(value: string): void {
  if (!/^[a-z]:[\\/]/i.test(value) || /[\x00-\x1f\x7f*?"<>|:]/.test(value.slice(2))) {
    throw new AdapterError('PATH_POLICY', 'Use an absolute local drive path without ADS, devices or wildcards.');
  }
}

export async function canonical(value: string, directory: boolean, maxBytes?: number): Promise<string> {
  localPath(value);
  try {
    const resolved = await realpath(value);
    localPath(resolved);
    const info = await stat(resolved);
    if (directory ? !info.isDirectory() : !info.isFile()) {
      throw new AdapterError('PATH_TYPE', directory ? 'Expected a directory.' : 'Expected a regular file.');
    }
    if (maxBytes !== undefined && info.size > maxBytes) {
      throw new AdapterError('FILE_LIMIT', 'Input exceeds the permitted file size.');
    }
    return resolved;
  } catch (error) {
    if (error instanceof AdapterError) throw error;
    throw new AdapterError('FILE_ACCESS', 'Path is missing or cannot be accessed with current permissions.');
  }
}

export async function allowedPath(config: Config, value: string, directory = false, maxBytes?: number): Promise<string> {
  const resolved = await canonical(value, directory, maxBytes);
  if (!config.roots.some(root => {
    const relative = path.relative(root, resolved);
    return relative === '' || (!path.isAbsolute(relative) && relative !== '..' && !relative.startsWith(`..${path.sep}`));
  })) throw new AdapterError('PATH_DENIED', 'Path is outside the configured allowed roots.');
  return resolved;
}

export function allowProcess(config: Config, pid?: number): void {
  if (!config.allProcesses && (pid === undefined || !config.pids.has(pid))) {
    throw new AdapterError('PROCESS_DENIED', 'Configure --allow-processes or an explicit --allow-pid for this query.');
  }
}

export async function parseConfig(args: string[]): Promise<Config> {
  if (process.platform !== 'win32') throw new AdapterError('PLATFORM', 'This server requires Windows.');
  let cli: string | undefined;
  let allProcesses = false;
  const roots: string[] = [];
  const pids = new Set<number>();
  for (let i = 0; i < args.length; ++i) {
    const key = args[i];
    if (key === '--allow-processes' && !allProcesses) { allProcesses = true; continue; }
    if (!['--cli', '--allow-root', '--allow-pid'].includes(key) || !args[i + 1]) {
      throw new AdapterError('CONFIG', 'Use --cli <exe>, --allow-root <directory>, and --allow-processes or --allow-pid <PID>.');
    }
    const value = args[++i];
    if (key === '--cli') {
      if (cli) throw new AdapterError('CONFIG', 'Specify --cli once.');
      cli = value;
    } else if (key === '--allow-root') roots.push(value);
    else {
      const pid = Number(value);
      if (!/^[0-9]+$/.test(value) || !Number.isInteger(pid) || pid < 1 || pid > 4294967295) {
        throw new AdapterError('CONFIG', 'PID must be an integer from 1 to 4294967295.');
      }
      pids.add(pid);
    }
  }
  if (!cli || (allProcesses && pids.size)) {
    throw new AdapterError('CONFIG', 'Supply --cli; choose either --allow-processes or --allow-pid.');
  }
  const resolvedCli = await canonical(cli, false);
  if (path.extname(resolvedCli).toLowerCase() !== '.exe') {
    throw new AdapterError('CONFIG', '--cli must point to a trusted native .exe, not a shell script.');
  }
  return { cli: resolvedCli, roots: await Promise.all(roots.map(root => canonical(root, true))), allProcesses, pids };
}
