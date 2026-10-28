import * as fs from 'node:fs/promises';
import { installMcp } from '../../src/mcp.mjs';
import { hash } from '../../src/mcp-config.mjs';

const spec = JSON.parse(process.argv[2]);
process.on('message', () => {});
const pause = async stage => {
  if (stage === spec.pause) {
    process.send({ ready: true });
    await new Promise(() => {});
  }
};
try {
  const rollbackUpgrade = spec.rollback && Object.fromEntries(Object.entries(spec.rollback).map(([key, value]) =>
    [key, ['config', 'receipt'].includes(key) ? value : Buffer.from(value, 'base64')]));
  const result = await installMcp(spec.command, spec.options, { home: spec.home, env: spec.env ?? {}, rollbackUpgrade,
    probeCli: async file => ({ path: file, tool_version: '2.0.0-dev.9', schema_version: '1.6', sha256: hash(await fs.readFile(file)) }),
    probeClient: async (agent, file) => ({ path: file, version: { codex: '0.164.0', claude: '2.1.310', opencode: '1.19.4' }[agent] }),
    checkServer: async () => {}, checkpoint: pause, recoveryCheckpoint: pause,
  });
  process.send({ result }); process.disconnect();
} catch (error) {
  process.send({ error: error.code || 'FIXTURE_FAILED' }); process.disconnect(); process.exitCode = 1;
}
