import * as fs from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { upgradeAll } from '../../src/batch-upgrade.mjs';
import { recoverAll } from '../../src/batch-recovery.mjs';

const spec = JSON.parse(process.argv[2]);
process.on('message', () => {});
let captured = 0;
const pause = async (stage, data) => {
  if (stage === spec.fail) throw new Error('fixture rollback');
  if (stage === spec.pause && (spec.agent === undefined || data?.agent === spec.agent)
      && (spec.target === undefined || captured === spec.target)) {
    process.send({ ready: true }); await new Promise(() => {});
  }
};
const probe = async file => ({ path: file, tool_version: '2.0.0-dev.9', schema_version: '1.6',
  sha256: createHash('sha256').update(await fs.readFile(file)).digest('hex') });
try {
  const result = await (spec.command === 'upgrade' ? upgradeAll : recoverAll)(spec.options, {
    home: spec.home, env: {}, probe, probeCli: probe,
    probeClient: async (agent, file) => ({ path: file, version: { codex: '0.164.0', claude: '2.1.310', opencode: '1.19.4' }[agent] }),
    checkServer: async () => {}, captureUpgrade: async () => { captured++; },
    checkpoint: pause, recoveryCheckpoint: pause, batchCheckpoint: pause,
  });
  process.send({ result }); process.disconnect();
} catch (error) { process.send({ error: error.code || 'FIXTURE_FAILED' }); process.disconnect(); process.exitCode = 1; }
