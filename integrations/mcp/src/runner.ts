import { spawn } from 'node:child_process';
import { AdapterError } from './errors.js';

export interface RunOptions {
  deadlineMs: number;
  maxBytes: number;
  signal?: AbortSignal;
}

// This runner is only given the configured native executable and fixed CLI verbs.
export function runProcess(executable: string, args: string[], options: RunOptions): Promise<{ bytes: Buffer; code: number }> {
  return new Promise((resolve, reject) => {
    if (options.signal?.aborted) return reject(new AdapterError('CANCELLED', 'Tool call was cancelled.'));
    const child = spawn(executable, args, { shell: false, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    let failure: AdapterError | undefined;
    let bytes = 0;
    let stderrBytes = 0;
    const chunks: Buffer[] = [];
    const stop = (error: AdapterError) => {
      failure ??= error;
      child.kill();
    };
    const cancel = () => stop(new AdapterError('CANCELLED', 'Tool call was cancelled.'));
    const timer = setTimeout(() => stop(new AdapterError('TIMEOUT', 'CLI exceeded its process deadline.')), options.deadlineMs);
    options.signal?.addEventListener('abort', cancel, { once: true });
    // Recheck after installing the listener, before waiting for any child output.
    if (options.signal?.aborted) cancel();
    child.stdout.on('data', (chunk: Buffer) => {
      bytes += chunk.length;
      if (bytes > options.maxBytes) stop(new AdapterError('OUTPUT_LIMIT', 'CLI output exceeded max_output_bytes; no partial report returned.'));
      else if (!failure) chunks.push(chunk);
    });
    child.stderr.on('data', (chunk: Buffer) => {
      stderrBytes += chunk.length;
      if (stderrBytes > 65536) stop(new AdapterError('STDERR_LIMIT', 'CLI diagnostics exceeded the stderr limit.'));
    });
    child.on('error', () => { failure ??= new AdapterError('CLI_START', 'Could not launch the configured CLI executable.'); });
    // close follows termination and closure of the child's pipes. Never release
    // a concurrency slot while a timed-out direct child is still running.
    child.on('close', (code, signal) => {
      clearTimeout(timer);
      options.signal?.removeEventListener('abort', cancel);
      if (failure) reject(failure);
      else if (signal || code === null || code < 0 || code > 5) {
        reject(new AdapterError('CLI_EXIT', 'CLI terminated abnormally.'));
      } else resolve({ bytes: Buffer.concat(chunks), code });
    });
  });
}
