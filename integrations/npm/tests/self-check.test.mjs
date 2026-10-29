import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { createHash } from 'node:crypto';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { fileURLToPath } from 'node:url';
import { selfCheck } from '../src/self-check.mjs';

const execute = promisify(execFile);
const entry = fileURLToPath(new URL('../bin/rondo.mjs', import.meta.url));
const version = { command: 'version', status: 'success', tool_version: '2.0.0-dev.9', schema_version: '1.6',
  data: { text: '2.0.0-dev.9' }, execution: { operation_completed: true } };
const item = (report, id) => report.checks.find(check => check.id === id);

async function fixture(t) {
  const base = await fs.realpath(await fs.mkdtemp(path.join(os.tmpdir(), 'rondo-self-check-')));
  const root = path.join(base, '中文 package & $ 🚀'), calls = [], files = [];
  const write = async (relative, bytes) => {
    const file = path.join(root, relative); await fs.mkdir(path.dirname(file), { recursive: true });
    await fs.writeFile(file, bytes); if (!files.includes(file)) files.push(file); return file;
  };
  t.after(async () => {
    assert.equal(path.dirname(base), await fs.realpath(os.tmpdir())); assert.ok(path.basename(base).startsWith('rondo-self-check-'));
    await fs.rm(base, { recursive: true, maxRetries: 10, retryDelay: 50 });
  });
  await write('package.json', JSON.stringify({ name: '@som1ng/rondo', version: '0.1.0-dev.8', type: 'module' }));
  const bytes = Buffer.alloc(256); bytes.write('MZ'); bytes.writeUInt32LE(128, 0x3c);
  bytes.write('PE\0\0', 128); bytes.writeUInt16LE(0x8664, 132); bytes.writeUInt16LE(0x20b, 152);
  const cli = await write('runtime/x64/rondo.exe', bytes);
  await write('runtime/x64/manifest.json', JSON.stringify({ format: 1, product: 'rondo-cli', platform: 'windows',
    architecture: 'x64', runtime: 'msvc-v14', source_revision: null, tool_version: version.tool_version, schema_version: version.schema_version,
    files: { 'rondo.exe': { size: bytes.length, sha256: createHash('sha256').update(bytes).digest('hex') } } }));
  await write('integrations/installer/src/index.mjs', 'if (process.argv[2] !== "--help") process.exit(1); console.log("Rondo Agent installer fixture");');
  await write('integrations/installer/src/mcp.mjs', 'export function installMcp() { throw new Error("must not install"); }');
  await write('integrations/mcp/dist/server.js', 'export function createServer() { throw new Error("must not start"); }');
  await write('integrations/mcp/dist/index.js', 'throw new Error("must not start MCP");');
  for (const file of ['SKILL.md', 'references/commands.md', 'references/evidence.md', 'agents/openai.yaml']) {
    await write('integrations/skills/windows-dll-doctor/' + file, 'fixture');
  }
  const context = { root, platform: 'win32', machine: 'x86_64', nodeVersion: '22.19.0',
    execute: async (file, args, options) => {
      calls.push({ file, args, options });
      return file === cli ? { stdout: JSON.stringify(version), stderr: '' } : execute(file, args, options);
    } };
  return { root, cli, context, calls, files, write };
}

test('healthy package probes literal Unicode paths without changing files or starting MCP', async t => {
  const f = await fixture(t), before = await Promise.all(f.files.map(file => fs.readFile(file)));
  const report = await selfCheck(f.context);
  assert.equal(report.status, 'success'); assert.equal(report.checks.length, 8);
  assert.equal(report.runtime_verification, 'passed'); assert.equal(report.client_discovery, 'not_run');
  assert.equal(report.architecture, 'x64'); assert.ok(!JSON.stringify(report).includes(f.root));
  assert.deepEqual(await Promise.all(f.files.map(file => fs.readFile(file))), before);
  assert.equal(f.calls.length, 4);
  for (const { options } of f.calls) {
    assert.equal(options.shell, false); assert.equal(options.windowsHide, true);
    assert.equal(options.timeout, 10000); assert.equal(options.maxBuffer, 1024 * 1024);
  }
});

for (const overrides of [{ platform: 'linux' }, { machine: 'ARM64' }, { nodeVersion: '22.18.0' }, { nodeVersion: '20.99.0' }]) {
  test(`unsupported prerequisites never start a process: ${JSON.stringify(overrides)}`, async t => {
    const f = await fixture(t), report = await selfCheck({ ...f.context, ...overrides });
    assert.equal(report.status, 'error'); assert.equal(report.runtime_verification, 'not_run');
    assert.equal(f.calls.length, 0); assert.equal(item(report, 'installer').status, 'skipped');
  });
}

test('invalid package metadata prevents probes without disclosing its contents', async t => {
  const f = await fixture(t); await f.write('package.json', 'private-fixture-value');
  const report = await selfCheck(f.context);
  assert.equal(item(report, 'package').code, 'RONDO_PACKAGE'); assert.equal(f.calls.length, 0);
  assert.ok(!JSON.stringify(report).includes('private-fixture-value'));
});

for (const damage of ['cli', 'manifest']) {
  test(`${damage} damage blocks the native probe but independent resource checks still run`, async t => {
    const f = await fixture(t);
    if (damage === 'cli') await fs.appendFile(f.cli, 'tampered');
    else await fs.unlink(path.join(f.root, 'runtime/x64/manifest.json'));
    const report = await selfCheck(f.context);
    assert.equal(item(report, 'cli_distribution').code, 'DISTRIBUTION_INVALID');
    assert.equal(item(report, 'cli_runtime').status, 'skipped'); assert.equal(report.runtime_verification, 'not_run');
    assert.ok(!f.calls.some(call => call.file === f.cli)); assert.equal(item(report, 'mcp_modules').status, 'passed');
  });
}

for (const failure of ['launch', 'json', 'version', 'incomplete']) {
  test(`CLI ${failure} failure is reported without claiming a missing runtime`, async t => {
    const f = await fixture(t);
    const report = await selfCheck({ ...f.context, execute: async (file, args, options) => {
      if (file !== f.cli) return f.context.execute(file, args, options);
      if (failure === 'launch') throw new Error('private-fixture-value');
      return { stdout: failure === 'json' ? 'private-fixture-value' : JSON.stringify({ ...version,
        ...(failure === 'version' ? { tool_version: 'unexpected' } : { execution: { operation_completed: false } }) }) };
    } });
    assert.equal(report.status, 'error'); assert.equal(report.runtime_verification, 'failed');
    assert.equal(item(report, 'cli_runtime').code, 'RONDO_CLI_PROBE');
    assert.ok(!JSON.stringify(report).includes('private-fixture-value'));
    assert.match(item(report, 'cli_runtime').remedy, /does not identify the cause/);
  });
}

test('a valid manifest for the wrong bundled architecture still prevents execution', async t => {
  const f = await fixture(t), bytes = await fs.readFile(f.cli);
  bytes.writeUInt16LE(0x14c, 132); bytes.writeUInt16LE(0x10b, 152); await fs.writeFile(f.cli, bytes);
  const file = path.join(f.root, 'runtime/x64/manifest.json'), manifest = JSON.parse(await fs.readFile(file));
  manifest.architecture = 'x86'; manifest.files['rondo.exe'].sha256 = createHash('sha256').update(bytes).digest('hex');
  await fs.writeFile(file, JSON.stringify(manifest));
  const report = await selfCheck(f.context);
  assert.equal(item(report, 'cli_distribution').status, 'failed'); assert.equal(report.runtime_verification, 'not_run');
  assert.ok(!f.calls.some(call => call.file === f.cli));
});

test('installer errors remain bounded diagnostics and do not suppress other checks', async t => {
  const f = await fixture(t);
  await f.write('integrations/installer/src/index.mjs', 'throw new Error("private-fixture-value");');
  const report = await selfCheck(f.context);
  assert.equal(item(report, 'installer').code, 'RONDO_INSTALLER'); assert.equal(item(report, 'mcp_modules').status, 'passed');
  assert.ok(!JSON.stringify(report).includes('private-fixture-value'));
});

test('missing MCP imports are caught without exposing child stderr', async t => {
  const f = await fixture(t);
  await f.write('integrations/mcp/dist/server.js', 'import "./private-fixture-value.js"; export function createServer() {}');
  const report = await selfCheck(f.context);
  assert.equal(item(report, 'mcp_modules').code, 'RONDO_MCP_MODULES'); assert.equal(item(report, 'skill').status, 'passed');
  assert.ok(!JSON.stringify(report).includes('private-fixture-value'));
});

test('invalid MCP entry syntax fails without executing the entry or importing its modules', async t => {
  const f = await fixture(t); await f.write('integrations/mcp/dist/index.js', 'import {');
  const report = await selfCheck(f.context);
  assert.equal(item(report, 'mcp_modules').code, 'RONDO_MCP_MODULES');
  assert.ok(f.calls.some(call => call.args[0] === '--check'));
  assert.ok(!f.calls.some(call => call.args[0] === '--input-type=module'));
});

test('an empty required Skill resource fails instead of reporting ready', async t => {
  const f = await fixture(t); await f.write('integrations/skills/windows-dll-doctor/agents/openai.yaml', '');
  assert.equal(item(await selfCheck(f.context), 'skill').code, 'RONDO_SKILL');
});

test('a hanging installer probe is terminated by the real deadline', async t => {
  const f = await fixture(t); await f.write('integrations/installer/src/index.mjs', 'setInterval(() => {}, 1000);');
  const report = await selfCheck(f.context);
  assert.equal(item(report, 'installer').code, 'RONDO_INSTALLER'); assert.equal(item(report, 'skill').status, 'passed');
});

test('excessive module output is bounded and never copied into the public report', async t => {
  const f = await fixture(t);
  await f.write('integrations/mcp/dist/server.js', 'console.log("private-fixture-value".repeat(100000)); export function createServer() {}');
  const report = await selfCheck(f.context);
  assert.equal(item(report, 'mcp_modules').code, 'RONDO_MCP_MODULES'); assert.ok(JSON.stringify(report).length < 5000);
});

test('help and invalid options work without an assembled package or native runtime', async () => {
  const helped = await execute(process.execPath, [entry, 'self-check', '--help'], { windowsHide: true });
  assert.match(helped.stdout, /rondo self-check/); assert.equal(helped.stderr, '');
  await assert.rejects(execute(process.execPath, [entry, 'self-check', '--json', '--cli', 'unused.exe'], { windowsHide: true }), error => {
    assert.equal(error.code, 2); assert.equal(JSON.parse(error.stdout).code, 'ARGUMENT'); assert.equal(error.stderr, ''); return true;
  });
});
