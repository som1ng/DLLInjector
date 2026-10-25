import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { once } from 'node:events';
import { copyFile, mkdir, mkdtemp, readFile, realpath, readdir, rm, symlink, unlink, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { createInterface } from 'node:readline';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { allowedPath, canonical, parseConfig } from '../dist/policy.js';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const build = path.resolve(process.env.DLL_DOCTOR_TEST_BUILD ?? path.join(root, 'build/msvc-x64/Release'));
const cli = path.join(build, 'cli/dllinjector.exe');
const entry = fileURLToPath(new URL('../dist/index.js', import.meta.url));
const expectedTools = ['inspect_pe', 'analyze_dependencies', 'list_processes', 'inspect_process', 'list_modules', 'diagnose_dll', 'read_report'].sort();

async function connect(args) {
  const transport = new StdioClientTransport({ command: process.execPath, args: [entry, '--cli', cli, ...args], stderr: 'pipe' });
  let audit = '';
  transport.stderr.on('data', chunk => { audit += chunk.toString(); });
  const client = new Client({ name: 'dll-doctor-contract-test', version: '1.0.0' });
  try { await client.connect(transport); }
  catch (error) { await transport.close(); throw new Error(`MCP connection failed: ${audit}`, { cause: error }); }
  return { client, transport, audit: () => audit };
}

async function call(client, name, args) {
  const result = await client.callTool({ name, arguments: args });
  assert.deepEqual(JSON.parse(result.content[0].text), result.structuredContent);
  return { ...result.structuredContent, isError: result.isError ?? false };
}

function native(args) {
  const result = spawnSync(cli, [...args, '--json', '--timeout-ms', '5000'], { windowsHide: true, encoding: 'utf8', timeout: 10000 });
  assert.equal(result.error, undefined);
  return { code: result.status, report: JSON.parse(result.stdout) };
}
function withoutDuration(report) { const copy = structuredClone(report); delete copy.duration_ms; return copy; }

test('stdio exposes seven tools and runs native fixtures with bounded read-only policy', { timeout: 60000 }, async () => {
  assert.equal(process.platform, 'win32', 'native integration requires Windows');
  const temp = await realpath(await mkdtemp(path.join(tmpdir(), 'doctor-mcp-')));
  const folder = path.join(temp, '中文 space 🚀');
  const outside = path.join(temp, 'outside');
  await mkdir(folder); await mkdir(outside);
  const dll = path.join(folder, '插件.dll');
  await copyFile(path.join(build, 'noop_fixture.dll'), dll);
  const child = spawn(path.join(build, 'module_host.exe'), [dll], { windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
  let session;
  try {
    const [ready] = await once(child.stdout, 'data', { signal: AbortSignal.timeout(10000) });
    assert.match(ready.toString(), /^READY /);
    session = await connect(['--allow-root', folder, '--allow-pid', String(child.pid)]);
    const { client } = session;
    const listed = await client.listTools();
    assert.deepEqual(listed.tools.map(t => t.name).sort(), expectedTools);
    for (const tool of listed.tools) {
      assert.equal(tool.annotations.readOnlyHint, true);
      assert.equal(tool.annotations.destructiveHint, false);
      assert.equal(tool.inputSchema.additionalProperties, false);
      assert.ok(tool.outputSchema);
    }
    for (const [name, args, command] of [
      ['inspect_pe', { path: dll }, ['pe', 'inspect', dll]],
      ['analyze_dependencies', { path: dll, search_directories: [folder] }, ['doctor', dll, '--search-dir', folder]],
      ['diagnose_dll', { path: dll, target_arch: 'x64' }, ['doctor', dll, '--target-arch', 'x64']],
      ['inspect_process', { pid: child.pid }, ['process', 'inspect', '--pid', String(child.pid)]],
      ['list_modules', { pid: child.pid }, ['process', 'modules', '--pid', String(child.pid)]],
    ]) {
      const result = await call(client, name, args);
      const direct = native(command);
      assert.equal(result.source, 'live_cli', name);
      assert.equal(result.cli_exit_code, direct.code, name);
      assert.deepEqual(withoutDuration(result.report), withoutDuration(direct.report), name);
      assert.equal(result.isError, false, name);
    }
    assert.equal((await call(client, 'list_processes', {})).error.code, 'PROCESS_DENIED');
    assert.equal((await call(client, 'inspect_process', { pid: process.pid })).error.code, 'PROCESS_DENIED');
    const listSession = await connect(['--allow-processes']);
    try {
      const result = await call(listSession.client, 'list_processes', {});
      assert.equal(result.report.command, 'process.list');
      assert.equal(result.isError, false);
      const unavailable = await call(listSession.client, 'inspect_process', { pid: 4294967295 });
      assert.equal(unavailable.cli_exit_code, 3);
      assert.equal(unavailable.isError, true);
      const concurrent = await Promise.all(Array.from({ length: 3 }, () => call(listSession.client, 'list_processes', {})));
      assert.ok(concurrent.some(result => result.error?.code === 'BUSY'));
      assert.ok(concurrent.some(result => result.report?.command === 'process.list'));
    } finally { await listSession.client.close(); }

    const truncated = path.join(folder, 'bad.dll');
    await writeFile(truncated, 'MZ truncated');
    const diagnosis = await call(client, 'diagnose_dll', { path: truncated });
    assert.equal(diagnosis.cli_exit_code, 1);
    assert.equal(diagnosis.isError, false);
    const snapshot = path.join(folder, 'report.json');
    await writeFile(snapshot, JSON.stringify(diagnosis.report));
    await unlink(truncated);
    const saved = await call(client, 'read_report', { path: snapshot });
    assert.equal(saved.source, 'saved_report');
    assert.equal(saved.cli_exit_code, 0);
    assert.equal(saved.isError, false);
    assert.deepEqual(saved.report, diagnosis.report);
    for (const fixture of await readdir(path.join(root, 'tests/fixtures/json'))) {
      if (!fixture.endsWith('.json')) continue;
      const bytes = await readFile(path.join(root, 'tests/fixtures/json', fixture));
      await writeFile(snapshot, bytes);
      assert.deepEqual((await call(client, 'read_report', { path: snapshot })).report, JSON.parse(bytes), fixture);
    }
    const precise = structuredClone(saved.report);
    precise.duration_ms = 9007199254740992;
    await writeFile(snapshot, JSON.stringify(precise));
    assert.equal((await call(client, 'read_report', { path: snapshot })).error.code, 'UNSAFE_INTEGER');
    await writeFile(snapshot, '{');
    const broken = await call(client, 'read_report', { path: snapshot });
    assert.equal(broken.cli_exit_code, 2);
    assert.equal(broken.report.command, 'report.export');
    assert.equal(broken.isError, true);

    assert.equal((await call(client, 'inspect_pe', { path: dll, timeout_ms: 0 })).cli_exit_code, 5);
    assert.equal((await call(client, 'inspect_pe', { path: dll, max_output_bytes: 1024 })).error.code, 'OUTPUT_LIMIT');
    assert.equal((await call(client, 'inspect_pe', { path: truncated })).error.code, 'FILE_ACCESS');
    await copyFile(dll, path.join(outside, 'other.dll'));
    assert.equal((await call(client, 'inspect_pe', { path: path.join(outside, 'other.dll') })).error.code, 'PATH_DENIED');
    await symlink(outside, path.join(folder, 'escape'), 'junction');
    assert.equal((await call(client, 'inspect_pe', { path: path.join(folder, 'escape/other.dll') })).error.code, 'PATH_DENIED');
    assert.equal((await call(client, 'analyze_dependencies', { path: dll, search_directories: [outside] })).error.code, 'PATH_DENIED');
    for (const bad of ['relative.dll', '\\\\server\\share\\x.dll', '\\\\?\\C:\\x.dll', `${dll}:stream`, `${dll}\u0000`]) {
      assert.equal((await call(client, 'inspect_pe', { path: bad })).error.code, 'PATH_POLICY');
    }
    for (const args of [{ path: dll, unexpected: true }, { path: dll, timeout_ms: -1 }, { path: dll, max_output_bytes: 4194305 }]) {
      // SDK protocol revisions differ in whether invalid tool arguments are a
      // protocol error or an isError result. Both must reject without execution.
      let rejected = false;
      try { rejected = (await client.callTool({ name: 'inspect_pe', arguments: args })).isError === true; }
      catch { rejected = true; }
      assert.equal(rejected, true);
    }
    assert.equal((await call(client, 'inspect_pe', { path: dll })).isError, false, 'server recovers after failures');
    const audit = session.audit().trim().split(/\r?\n/).map(line => JSON.parse(line));
    assert.ok(audit.some(event => event.tool === 'inspect_pe'));
    assert.ok(audit.every(event => !('path' in event) && !('report' in event)));
    assert.ok(!session.audit().includes(folder));
  } finally {
    if (session) await session.client.close();
    const done = once(child, 'close'); child.stdin.end('\n'); child.kill(); await done;
    await rm(temp, { recursive: true, force: true });
  }
});

test('configuration and startup failures are actionable stderr-only messages', { timeout: 15000 }, async () => {
  for (const args of [[], ['--cli', cli, '--allow-processes', '--allow-pid', '1'], ['--cli', cli, '--allow-pid', '0']]) {
    await assert.rejects(parseConfig(args), { code: 'CONFIG' });
  }
  for (const [args, code] of [
    [[], 'CONFIG'],
    [['--cli', 'Z:\\missing.exe'], 'FILE_ACCESS'],
    [['--cli', process.execPath], 'CLI_EXIT'],
  ]) {
    const result = spawnSync(process.execPath, [entry, ...args], { windowsHide: true, encoding: 'utf8', timeout: 10000 });
    assert.equal(result.stdout, '');
    assert.equal(result.status, 1);
    assert.equal(JSON.parse(result.stderr).code, code);
  }
  const config = await parseConfig(['--cli', cli]);
  await assert.rejects(allowedPath(config, cli), { code: 'PATH_DENIED' });
  await assert.rejects(canonical(cli, false, 1), { code: 'FILE_LIMIT' });
  await assert.rejects(canonical(cli, true), { code: 'PATH_TYPE' });
});

test('legacy 2025-11-25 protocol initializes and calls a tool without stdout contamination', { timeout: 15000 }, async () => {
  const child = spawn(process.execPath, [entry, '--cli', cli, '--allow-root', build],
    { windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
  child.stderr.resume();
  const lines = createInterface({ input: child.stdout });
  const messages = lines[Symbol.asyncIterator]();
  const send = message => child.stdin.write(JSON.stringify({ jsonrpc: '2.0', ...message }) + '\n');
  try {
    send({ id: 1, method: 'initialize', params: { protocolVersion: '2025-11-25', capabilities: {}, clientInfo: { name: 'legacy-test', version: '1.0.0' } } });
    const initialized = JSON.parse((await messages.next()).value);
    assert.equal(initialized.id, 1);
    assert.equal(initialized.result.protocolVersion, '2025-11-25');
    send({ method: 'notifications/initialized' });
    send({ id: 2, method: 'tools/call', params: { name: 'inspect_pe', arguments: { path: path.join(build, 'noop_fixture.dll') } } });
    const called = JSON.parse((await messages.next()).value);
    assert.equal(called.id, 2);
    assert.equal(called.result.isError, false);
    assert.equal(called.result.structuredContent.report.command, 'pe.inspect');
    const closed = once(child, 'close');
    child.stdin.end();
    assert.equal((await closed)[0], 0, 'clean exit on stdin EOF');
  } finally {
    lines.close();
    if (child.exitCode === null) { const closed = once(child, 'close'); child.kill(); await closed; }
  }
});
