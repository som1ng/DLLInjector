import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import { runProcess } from '../dist/runner.js';
import { CLI_VERSION, parseReport } from '../dist/reports.js';

const options = { deadlineMs: 2000, maxBytes: 1024 };
const node = (script, extra = {}) => runProcess(process.execPath, ['-e', script], { ...options, ...extra });

test('runner isolates stdout, preserves exit code and treats arguments literally', async () => {
  const result = await runProcess(process.execPath,
    ['-e', 'process.stderr.write("diagnostic"); process.stdout.write(process.argv[1]); process.exitCode=3', 'space 中文 & $(x)'], options);
  assert.equal(result.bytes.toString(), 'space 中文 & $(x)');
  assert.equal(result.code, 3);
});

test('runner bounds stdout and stderr, rejects crashes and missing executable, then recovers', async () => {
  for (const [script, code] of [
    ['process.stdout.write("x".repeat(100000));setInterval(()=>{},1000)', 'OUTPUT_LIMIT'],
    ['process.stderr.write("x".repeat(100000));setInterval(()=>{},1000)', 'STDERR_LIMIT'],
    ['process.exit(99)', 'CLI_EXIT'],
  ]) await assert.rejects(node(script), { code });
  await assert.rejects(runProcess('Z:\\missing-doctor-cli.exe', [], options), { code: 'CLI_START' });
  assert.equal((await node('process.stdout.write("recovered")')).bytes.toString(), 'recovered');
});

test('timeout and cancellation reap the direct child before returning', async () => {
  // A child prints its PID to a private temporary file so cleanup can be checked.
  const { mkdtemp, rm } = await import('node:fs/promises');
  const { tmpdir } = await import('node:os');
  const { join } = await import('node:path');
  const folder = await mkdtemp(join(tmpdir(), 'doctor-runner-'));
  try {
    for (const cancel of [false, true]) {
      const file = join(folder, cancel ? 'cancel.pid' : 'timeout.pid');
      const controller = new AbortController();
      const pending = runProcess(process.execPath,
        ['-e', 'require("fs").writeFileSync(process.argv[1],String(process.pid));setInterval(()=>{},1000)', file],
        { ...options, deadlineMs: 1500, signal: controller.signal });
      // Attach rejection handling immediately; await readiness without an unhandled rejection.
      const outcome = pending.catch(error => error);
      let pid;
      for (let n = 0; n < 100; ++n) {
        try { pid = Number(await readFile(file, 'utf8')); if (pid) break; } catch {}
        await new Promise(resolve => setTimeout(resolve, 10));
      }
      assert.ok(pid, 'child started');
      if (cancel) controller.abort();
      assert.equal((await outcome).code, cancel ? 'CANCELLED' : 'TIMEOUT');
      assert.throws(() => process.kill(pid, 0), { code: 'ESRCH' });
    }
    await assert.rejects(node('process.exit(0)', { signal: AbortSignal.abort() }), { code: 'CANCELLED' });
  } finally { await rm(folder, { recursive: true, force: true }); }
});

test('report validation preserves snapshots and rejects invalid, mismatched or lossy reports', async () => {
  const bytes = await readFile(new URL('../../../tests/fixtures/json/pe.json', import.meta.url));
  const saved = parseReport(bytes, 0, 'report.export', true);
  assert.deepEqual(saved, JSON.parse(bytes));
  assert.throws(() => parseReport(Buffer.from('{'), 0, 'version'), { code: 'CLI_JSON' });
  assert.throws(() => parseReport(Buffer.from([0xff]), 0, 'version'), { code: 'CLI_JSON' });
  assert.throws(() => parseReport(Buffer.from('null'), 0, 'version'), { code: 'CLI_SCHEMA' });
  assert.throws(() => parseReport(Buffer.from('{}'), 0, 'version'), { code: 'CLI_SCHEMA' });
  saved.duration_ms = 9007199254740992;
  assert.throws(() => parseReport(Buffer.from(JSON.stringify(saved)), 0, 'report.export', true), { code: 'UNSAFE_INTEGER' });
  saved.duration_ms = 0;
  assert.throws(() => parseReport(bytes, 0, 'pe.inspect'), { code: 'CLI_VERSION' });
  const live = Buffer.from(JSON.stringify({ schema_version: '1.6', tool_version: CLI_VERSION,
    command: 'version', status: 'success', target: { pid: null, path: null },
    execution: { command_accepted: true, operation_completed: true, outcome_verified: true },
    data: { text: CLI_VERSION }, findings: [], evidence: [], errors: [], duration_ms: 0 }));
  assert.equal(parseReport(live, 0, 'version').tool_version, CLI_VERSION);
  assert.throws(() => parseReport(live, 0, 'pe.inspect'), { code: 'CLI_VERSION' });
  assert.throws(() => parseReport(live, 1, 'version'), { code: 'CLI_EXIT' });
});
