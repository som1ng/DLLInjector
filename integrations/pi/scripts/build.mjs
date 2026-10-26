import { copyFile, lstat, mkdir, readFile, realpath } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = await realpath(fileURLToPath(new URL('../', import.meta.url)));
const source = path.resolve(root, '../skills/windows-dll-doctor');
const manifest = JSON.parse(await readFile(path.join(root, 'package.json'), 'utf8'));
const prefix = 'dist/skills/windows-dll-doctor/';

async function info(file) {
  try { return await lstat(file); }
  catch (error) { if (error.code === 'ENOENT') return undefined; throw error; }
}

// Only create declared output parents, one at a time. Never follow a junction
// into another directory and never recursively delete generated/user files.
async function outputFile(relative) {
  let directory = root;
  for (const segment of relative.split('/').slice(0, -1)) {
    directory = path.join(directory, segment);
    const existing = await info(directory);
    if (existing && (!existing.isDirectory() || existing.isSymbolicLink())) {
      throw new Error('Generated output directory must not be a link or file.');
    }
    if (!existing) await mkdir(directory);
  }
  const file = path.join(root, relative);
  const existing = await info(file);
  if (existing && (!existing.isFile() || existing.isSymbolicLink())) {
    throw new Error('Generated output file must be a regular file.');
  }
  return file;
}

for (const declared of manifest.files) {
  const relative = declared.slice(prefix.length);
  if (!declared.startsWith(prefix) || !relative || relative.split('/').some(part => !part || part === '.' || part === '..')
      || /[\\:*?]/.test(relative)) throw new Error('Invalid generated resource path in package manifest.');
  const input = path.join(source, relative);
  const sourceInfo = await info(input);
  if (!sourceInfo?.isFile() || sourceInfo.isSymbolicLink()) throw new Error('Canonical Skill resource is missing or linked.');
  await copyFile(input, await outputFile(declared));
}
console.log(`Built ${manifest.files.length} Pi resources from the canonical Skill.`);
