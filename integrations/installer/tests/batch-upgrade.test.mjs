import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { install } from '../src/installer.mjs';
import { installMcp } from '../src/mcp.mjs';
import { setupAll } from '../src/batch.mjs';
import { setupAllMcp } from '../src/batch-mcp.mjs';
import { upgradeAll } from '../src/batch-upgrade.mjs';
import { hash } from '../src/mcp-config.mjs';

const agents = ['codex', 'claude', 'opencode'];
const privateValue = 'private fixture, never a real credential';
async function fixture(t, integration, scope = 'project') {
  const base = await fs.realpath(await fs.mkdtemp(path.join(os.tmpdir(), 'rondo-batch-upgrade-')));
  const project = path.join(base, '项目 🚀'), home = path.join(base, '用户 home');
  await fs.mkdir(project); await fs.mkdir(home);
  const cli = path.join(base, 'old.exe'), nextCli = path.join(base, 'new.exe');
  const server = path.join(base, 'old.js'), nextServer = path.join(base, 'new.js');
  const clients = Object.fromEntries(agents.map(agent => [agent, path.join(base, agent + '.exe')]));
  for (const file of [cli, nextCli, server, nextServer, ...Object.values(clients)]) await fs.writeFile(file, 'fixture: ' + path.basename(file));
  let probes = 0;
  const probe = async file => { probes++; return { path: file, tool_version: '2.0.0-dev.9',
    schema_version: '1.6', sha256: hash(await fs.readFile(file)) }; };
  const context = { home, env: {}, probe, probeCli: probe,
    probeClient: async (agent, file) => { probes++; return { path: file,
      version: { codex: '0.164.0', claude: '2.1.310', opencode: '1.19.4' }[agent] }; },
    checkServer: async () => { probes++; },
  };
  const options = { integration, agent: 'all', scope, ...(scope === 'project' ? { project } : {}), cli,
    ...(integration === 'mcp' ? { server, clients, allowRoot: [project] } : {}) };
  if (integration === 'mcp') {
    const config = path.join(scope === 'project' ? project : home, scope === 'project' ? '.mcp.json' : '.claude.json');
    await fs.writeFile(config, JSON.stringify({ privateFixture: privateValue, mcpServers: { other: { command: 'other.exe' } } }));
  }
  t.after(async () => {
    assert.equal(path.dirname(base), await fs.realpath(os.tmpdir()));
    assert.ok(path.basename(base).startsWith('rondo-batch-upgrade-'));
    await fs.rm(base, { recursive: true });
  });
  const setup = integration === 'skill' ? setupAll : setupAllMcp;
  const run = integration === 'skill' ? install : installMcp;
  const targets = (await setup(options, context)).targets;
  const upgraded = { ...options, cli: nextCli, ...(integration === 'mcp' ? { server: nextServer } : {}) };
  const query = agent => ({ agent, scope, project: options.project });
  const single = agent => ({ ...upgraded, agent, ...(integration === 'mcp' ? { client: clients[agent] } : {}) });
  return { base, project, home, options, upgraded, context, targets, run, setup, query, single, cli, nextCli, server,
    probes: () => probes };
}
const paths = target => target.files ?? [target.config, target.receipt];
async function snapshot(targets) {
  return Promise.all(targets.flatMap(paths).map(async file => [file, await fs.readFile(file)]));
}
async function unchanged(before) {
  for (const [file, bytes] of before) assert.deepEqual(await fs.readFile(file), bytes, file);
}
function lockPath(target) {
  return target.target ? path.join(path.dirname(target.target), '.dll-doctor-installer.lock')
    : path.join(path.dirname(target.receipt), 'mcp-' + target.agent + '.lock');
}

for (const integration of ['skill', 'mcp']) {
  for (const scope of ['project', 'user']) {
    test(`${integration}/${scope}: preview, upgrade, repeat and removal preserve unrelated content`, async t => {
      const f = await fixture(t, integration, scope), before = await snapshot(f.targets), probes = f.probes();
      const preview = await upgradeAll({ ...f.upgraded, dryRun: true }, f.context);
      assert.equal(preview.targets.length, integration === 'skill' ? 2 : 3);
      assert.equal(preview.action, 'preview'); assert.equal(f.probes(), probes); await unchanged(before);
      const result = await upgradeAll(f.upgraded, f.context);
      assert.equal(result.action, 'upgraded'); assert.equal(result.command, 'upgrade'); assert.equal(result.scope, scope);
      assert.ok(!JSON.stringify(result).includes(privateValue));
      if (integration === 'skill') assert.deepEqual(result.targets[0].agents, ['codex', 'opencode']);
      assert.equal((await upgradeAll(f.upgraded, f.context)).action, 'unchanged');
      for (const target of f.targets) {
        assert.equal((await f.run('verify', f.query(target.agent), f.context)).action, 'verified');
        await f.run('uninstall', f.query(target.agent), f.context);
      }
      if (integration === 'mcp') {
        const source = await fs.readFile(f.targets[1].config, 'utf8');
        assert.ok(source.includes(privateValue) && source.includes('other.exe'));
      }
    });

    const count = integration === 'skill' ? 2 : 3;
    for (let failing = 1; failing <= count; failing++) {
      test(`${integration}/${scope}: target ${failing} failure restores all bytes even when old runtimes disappeared`, async t => {
        const f = await fixture(t, integration, scope), before = await snapshot(f.targets);
        await fs.unlink(f.cli); await fs.unlink(f.server);
        let prepared = 0;
        await assert.rejects(upgradeAll(f.upgraded, { ...f.context,
          captureUpgrade: () => { prepared++; },
          checkpoint: () => { if (prepared === failing) throw new Error(privateValue); },
        }), error => {
          assert.equal(error.details.rollback_status, 'complete');
          assert.equal(error.details.rollback.length, failing);
          assert.ok(!JSON.stringify(error).includes(privateValue)); assert.ok(!error.message.includes(privateValue));
          return true;
        });
        await unchanged(before);
      });
    }
  }

  test(`${integration}: a missing receipt aborts every target during preflight without probes or writes`, async t => {
    const f = await fixture(t, integration), last = f.targets.at(-1);
    const receipt = last.receipt ?? path.join(last.target, '.dll-doctor-install.json');
    const before = (await snapshot(f.targets)).filter(([file]) => file !== receipt);
    await fs.unlink(receipt);
    const probes = f.probes();
    await assert.rejects(upgradeAll(f.upgraded, f.context));
    assert.equal(f.probes(), probes); await unchanged(before);
    assert.equal(await fs.stat(receipt).catch(() => null), null);
  });

  test(`${integration}: unchanged first target is excluded from batch restoration`, async t => {
    const f = await fixture(t, integration);
    await f.run('upgrade', f.single('codex'), f.context);
    const before = await snapshot(f.targets);
    await assert.rejects(upgradeAll(f.upgraded, { ...f.context, checkpoint: () => { throw new Error('fixture'); } }), error => {
      assert.equal(error.details.completed_targets[0].action, 'unchanged');
      assert.ok(error.details.rollback.every(item => item.agent !== 'codex'));
      assert.equal(error.details.rollback_status, 'complete'); return true;
    });
    await unchanged(before);
  });

  test(`${integration}: post-commit lock cleanup failure restores the failing target too`, async t => {
    const f = await fixture(t, integration), before = await snapshot(f.targets);
    let prepared = 0;
    await assert.rejects(upgradeAll(f.upgraded, { ...f.context,
      captureUpgrade: () => { prepared++; },
      checkpoint: async (stage, data) => {
        if (prepared === f.targets.length && (stage === 'receipt-written' || data?.file === '.dll-doctor-install.json')) {
          await fs.unlink(lockPath(f.targets.at(-1)));
        }
      },
    }), error => { assert.equal(error.details.rollback_status, 'complete'); return true; });
    await unchanged(before);
  });

  test(`${integration}: later runtime verification failure rolls back all upgraded targets`, async t => {
    const f = await fixture(t, integration), before = await snapshot(f.targets);
    let prepared = 0;
    const context = { ...f.context, captureUpgrade: () => { prepared++; } };
    const key = integration === 'skill' ? 'probe' : 'checkServer', original = context[key];
    context[key] = async (...args) => {
      if (prepared === f.targets.length) throw new Error('post-write verification fixture');
      return original(...args);
    };
    await assert.rejects(upgradeAll(f.upgraded, context), error => {
      assert.equal(error.details.rollback_status, 'complete'); return true;
    });
    await unchanged(before);
  });

  test(`${integration}: concurrent edits are preserved and reported as incomplete rollback`, async t => {
    const f = await fixture(t, integration);
    const edited = f.targets[0].target ? path.join(f.targets[0].target, 'SKILL.md') : f.targets[0].config;
    let prepared = 0;
    await assert.rejects(upgradeAll(f.upgraded, { ...f.context, captureUpgrade: async () => {
      if (++prepared === f.targets.length) {
        await fs.appendFile(edited, '\n# concurrent fixture');
        throw new Error(privateValue);
      }
    } }), error => {
      assert.equal(error.code, 'ROLLBACK_INCOMPLETE'); assert.equal(error.details.rollback_status, 'incomplete');
      assert.ok(error.details.rollback.some(item => item.agent === 'codex' && item.action === 'failed'));
      assert.ok(!JSON.stringify(error).includes(privateValue)); return true;
    });
    assert.ok((await fs.readFile(edited, 'utf8')).includes('concurrent fixture'));
  });

  test(`${integration}: pre-existing locks are not removed and completed targets are restored`, async t => {
    const f = await fixture(t, integration), before = await snapshot(f.targets);
    const lock = lockPath(f.targets.at(-1));
    await fs.writeFile(lock, 'another operation');
    await assert.rejects(upgradeAll(f.upgraded, f.context), error => {
      assert.equal(error.details.rollback_status, 'complete'); return true;
    });
    await unchanged(before); assert.equal(await fs.readFile(lock, 'utf8'), 'another operation');
  });

  test(`${integration}: CLI batch preview dispatches without executing fixture clients`, async t => {
    const f = await fixture(t, integration), before = await snapshot(f.targets);
    const entry = fileURLToPath(new URL('../src/index.mjs', import.meta.url));
    const args = [entry, 'upgrade', '--agent', 'all', '--scope', 'project', '--project', f.project, '--cli', f.nextCli, '--dry-run'];
    if (integration === 'mcp') {
      args.push('--integration', 'mcp', '--server', f.upgraded.server, '--allow-root', f.project);
      for (const agent of agents) args.push('--' + agent + '-client', f.options.clients[agent]);
    }
    const result = JSON.parse(execFileSync(process.execPath, args, { windowsHide: true, encoding: 'utf8', stdio: 'pipe' }));
    assert.equal(result.action, 'preview'); assert.equal(result.command, 'upgrade'); await unchanged(before);
  });
}

test('invalid batch selections and incomplete client maps are rejected', async t => {
  const f = await fixture(t, 'mcp');
  for (const change of [{ agent: 'auto' }, { select: 'codex' }, { client: f.options.clients.codex },
    { clients: { codex: f.options.clients.codex } }, { clients: { ...f.options.clients, unknown: 'x' } }]) {
    await assert.rejects(upgradeAll({ ...f.upgraded, ...change }, f.context), { code: 'ARGUMENT' });
  }
});

test('batch upgrades validate real CLI and MCP runtimes in isolated fixtures', {
  skip: !process.env.DLL_INSTALLER_TEST_CLI || !process.env.DLL_INSTALLER_TEST_MCP,
}, async t => {
  for (const integration of ['skill', 'mcp']) {
    const f = await fixture(t, integration);
    const { probe, probeCli, checkServer, ...context } = f.context;
    await fs.copyFile(process.env.DLL_INSTALLER_TEST_CLI, f.nextCli);
    const options = { ...f.upgraded, ...(integration === 'mcp' ? { server: process.env.DLL_INSTALLER_TEST_MCP } : {}) };
    assert.equal((await upgradeAll(options, context)).action, 'upgraded');
    for (const target of f.targets) await f.run('verify', f.query(target.agent), context);
  }
});
