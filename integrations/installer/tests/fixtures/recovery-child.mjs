import * as fs from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { install } from '../../src/installer.mjs';

const spec = JSON.parse(process.argv[2]);
process.on('message', () => {}); // Keep IPC alive while the parent tests forced termination.
let count = 0;
const pause = async stage => {
  if (stage === spec.pause && ++count === (spec.count ?? 1)) {
    process.send({ ready: true, stage });
    await new Promise(() => {});
  }
};
try {
  const result = await install(spec.command, spec.options, { home: spec.home, env: {},
    probe: async file => ({ path: file, tool_version: '2.0.0-dev.9', schema_version: '1.6',
      sha256: createHash('sha256').update(await fs.readFile(file)).digest('hex') }),
    checkpoint: pause, recoveryCheckpoint: pause,
  });
  process.send({ result });
  process.disconnect();
} catch (error) {
  process.send({ error: error.code || 'FIXTURE_FAILED' });
  process.disconnect();
  process.exitCode = 1;
}
