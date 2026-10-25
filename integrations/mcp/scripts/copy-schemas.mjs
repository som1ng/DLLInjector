import { copyFile, mkdir } from 'node:fs/promises';

const destination = new URL('../dist/schemas/', import.meta.url);
await mkdir(destination, { recursive: true });
for (const suffix of ['1', '1.1', '1.2', '1.3', '1.4', '1.5', '1.6']) {
  const file = `cli-v${suffix}.json`;
  await copyFile(new URL(`../../../schemas/${file}`, import.meta.url), new URL(file, destination));
}
