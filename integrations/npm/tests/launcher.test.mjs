import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { createHash } from 'node:crypto';
import { architecture, command } from '../src/launcher.mjs';

const context = { platform: 'win32', machine: 'x86_64', root: path.resolve('missing-rondo-package') };
test('select host architecture and reject ARM64 or non-Windows instead of guessing', () => {
  assert.equal(architecture('win32', 'AMD64'), 'x64');
  assert.equal(architecture('win32', 'i686'), 'x86');
  for (const [platform, machine] of [['win32', 'ARM64'], ['linux', 'x86_64'], ['darwin', 'arm64']]) {
    assert.throws(() => architecture(platform, machine), { code: 'RONDO_PLATFORM' });
  }
});
test('setup supplies bundled CLI, manifest and MCP paths without executing or writing', async () => {
  const result = await command(['setup', '--agent', 'auto', '--scope', 'project', '--integration=mcp', '--dry-run'], context);
  assert.equal(result.file, process.execPath);
  for (const flag of ['--cli', '--manifest', '--server']) {
    assert.ok(path.isAbsolute(result.args[result.args.indexOf(flag) + 1]));
  }
  assert.ok(result.args.includes(path.join(context.root, 'runtime/x64/rondo.exe')));
});
test('upgrade delegates with packaged runtime paths and preserves explicit runtime options', async () => {
  const upgraded = await command(['upgrade', '--agent', 'codex', '--scope', 'user', '--integration', 'mcp'], context);
  assert.equal(upgraded.args[1], 'upgrade');
  for (const flag of ['--cli', '--manifest', '--server']) assert.ok(upgraded.args.includes(flag));
  const explicit = ['upgrade', '--cli=C:\\own\\rondo.exe', '--server=C:\\own\\mcp.js', '--integration=mcp'];
  assert.deepEqual((await command(explicit, context)).args.slice(1), explicit);
  assert.deepEqual((await command(['upgrade', '--help'], context)).args.slice(1), ['upgrade', '--help']);
  const all = ['upgrade', '--agent', 'all', '--scope', 'user', '--integration=mcp',
    '--codex-client=c.exe', '--claude-client=a.exe', '--opencode-client=o.exe'];
  const batch = (await command(all, context)).args.slice(1);
  assert.deepEqual(batch.slice(0, all.length), all);
  for (const flag of ['--cli', '--manifest', '--server']) assert.ok(batch.includes(flag));
});
test('explicit source runtime paths are preserved without silently attaching another manifest', async () => {
  const args = ['setup', '--cli=C:\own\rondo.exe', '--server=C:\own\mcp.js', '--integration', 'mcp'];
  const result = await command(args, context);
  assert.deepEqual(result.args.slice(1), args);
});
test('verify, uninstall, discovery and setup help do not depend on packaged executable bytes', async () => {
  for (const args of [['verify', '--agent', 'codex'], ['uninstall', '--agent', 'codex'], ['list-agents'], ['setup', '--help']]) {
    assert.deepEqual((await command(args, context)).args.slice(1), args);
  }
});
test('explicit MCP CLI remains explicit', async () => {
  const args = ['--cli', 'C:\own\rondo.exe', '--allow-root', 'C:\data'];
  assert.deepEqual((await command(['mcp', ...args], context)).args.slice(1), args);
});
test('native and default MCP reject missing runtime', async () => {
  for (const args of [['--version', '--json'], ['mcp']]) await assert.rejects(command(args, context));
});
test('native arguments remain literal and corrupted binaries fail before launch', async t => {
  const root = await fs.realpath(await fs.mkdtemp(path.join(os.tmpdir(), 'rondo-launcher-')));
  t.after(async () => {
    assert.equal(path.dirname(root), await fs.realpath(os.tmpdir()));
    assert.ok(path.basename(root).startsWith('rondo-launcher-'));
    await fs.rm(root, { recursive: true });
  });
  const directory = path.join(root, 'runtime/x64');
  await fs.mkdir(directory, { recursive: true });
  const cli = path.join(directory, 'rondo.exe');
  const bytes = Buffer.alloc(256);
  bytes.write('MZ'); bytes.writeUInt32LE(128, 0x3c); bytes.write('PE\0\0', 128);
  bytes.writeUInt16LE(0x8664, 132); bytes.writeUInt16LE(0x20b, 152);
  await fs.writeFile(cli, bytes);
  await fs.writeFile(path.join(directory, 'manifest.json'), JSON.stringify({ format: 1, product: 'rondo-cli',
    platform: 'windows', architecture: 'x64', runtime: 'msvc-v14', source_revision: null,
    tool_version: '2.0.0-dev.9', schema_version: '1.6',
    files: { 'rondo.exe': { size: bytes.length, sha256: createHash('sha256').update(bytes).digest('hex') } } }));
  const args = ['doctor', 'C:\中文 space\a&$b.dll', '--json'];
  assert.deepEqual(await command(args, { ...context, root }), { file: cli, args });
  const mcp = await command(['mcp', '--allow-root', root], { ...context, root });
  assert.deepEqual(mcp.args.slice(-2), ['--cli', cli]);
  await fs.appendFile(cli, 'changed');
  await assert.rejects(command(args, { ...context, root }), { code: 'DISTRIBUTION_INVALID' });
});
