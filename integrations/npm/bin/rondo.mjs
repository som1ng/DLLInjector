#!/usr/bin/env node
import { spawn } from 'node:child_process';
import { command, usage } from '../src/launcher.mjs';

try {
  const args = process.argv.slice(2);
  if (args[0] === 'self-check') {
    const { runSelfCheck } = await import('../src/self-check.mjs');
    process.exitCode = await runSelfCheck(args.slice(1));
  } else {
    if (!args.length || (args.length === 1 && args[0] === '--help')) console.log(usage);
    const next = await command(args.length ? args : ['--help']);
    const child = spawn(next.file, next.args, { stdio: 'inherit', windowsHide: true, shell: false });
    const signals = new Map(['SIGINT', 'SIGTERM'].map(signal => [signal, () => child.kill(signal)]));
    for (const [signal, handler] of signals) process.on(signal, handler);
    const cleanup = () => { for (const [signal, handler] of signals) process.off(signal, handler); };
    child.once('error', () => {
      cleanup();
      console.error(JSON.stringify({ status: 'error', code: 'RONDO_START',
        message: 'Rondo could not start. Check package files and the Microsoft Visual C++ v14 runtime.' }));
      process.exitCode = 1;
    });
    child.once('exit', (code, signal) => { cleanup(); process.exitCode = code ?? (signal === 'SIGINT' ? 130 : 143); });
  }
} catch (error) {
  console.error(JSON.stringify({ status: 'error', code: error.code || 'RONDO_PACKAGE',
    message: error.code === 'RONDO_PLATFORM' ? error.message
      : 'Rondo package validation failed. Install a complete trusted Rondo archive; check --help for usage.' }));
  process.exitCode = 1;
}
