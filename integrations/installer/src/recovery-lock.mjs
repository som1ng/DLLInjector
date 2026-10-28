import { spawn } from 'node:child_process';
import path from 'node:path';

// FileShare.None provides an OS-owned lock. EOF releases it even if the parent
// recovery process dies, so a second recovery never has to delete a stale guard.
const script = `$ErrorActionPreference = 'Stop'
try {
  $guard = [System.IO.File]::Open($env:RONDO_RECOVERY_GUARD, 'OpenOrCreate', 'ReadWrite', 'None')
  try {
    [Console]::Out.WriteLine('ready')
    [Console]::Out.Flush()
    $null = [Console]::In.ReadToEnd()
  } finally { $guard.Dispose() }
} catch { exit 3 }
`;
const busy = () => Object.assign(new Error('Recovery guard is unavailable.'), { code: 'RECOVERY_BUSY' });

export async function acquireRecoveryGuard(file) {
  if (process.platform !== 'win32' || !process.env.SystemRoot) throw busy();
  const shell = path.join(process.env.SystemRoot, 'System32/WindowsPowerShell/v1.0/powershell.exe');
  const env = Object.fromEntries(Object.entries(process.env).filter(([key]) =>
    ['SYSTEMROOT', 'WINDIR', 'TEMP', 'TMP'].includes(key.toUpperCase())));
  const child = spawn(shell, ['-NoProfile', '-NonInteractive', '-EncodedCommand', Buffer.from(script, 'utf16le').toString('base64')],
    { windowsHide: true, stdio: ['pipe', 'pipe', 'ignore'], env: { ...env, RONDO_RECOVERY_GUARD: file } });
  child.stdin.on('error', () => {});
  let stopped = false, timer;
  const exited = new Promise(resolve => child.once('close', code => { stopped = true; resolve(code); }));
  const ready = new Promise((resolve, reject) => {
    let output = '';
    child.once('error', reject);
    child.stdout.on('data', bytes => {
      output += bytes.toString('utf8');
      if (output.trim() === 'ready') resolve();
      else if (output.length > 32) reject(busy());
    });
  });
  try {
    await Promise.race([ready, exited.then(() => { throw busy(); }),
      new Promise((_, reject) => { timer = setTimeout(() => reject(busy()), 10000); })]);
  } catch {
    child.stdin.destroy(); child.kill(); throw busy();
  } finally { clearTimeout(timer); }
  return {
    check: () => { if (stopped) throw busy(); },
    release: async () => {
      child.stdin.end();
      const timer = setTimeout(() => child.kill(), 10000);
      try { await exited; } finally { clearTimeout(timer); }
    },
  };
}
