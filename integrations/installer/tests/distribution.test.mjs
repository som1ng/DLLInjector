import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { createHash } from 'node:crypto';
import { install } from '../src/installer.mjs';
import { verifyDistribution } from '../src/distribution.mjs';

const hash = data => createHash('sha256').update(data).digest('hex');

async function fixture(t, arch = 'x64') {
  const temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'dll-distribution-test-'));
  const base = await fs.realpath(temporary);
  const project = path.join(base, '中文 project 🚀');
  await fs.mkdir(project);
  const cli = path.join(base, 'dllinjector.exe');
  // Minimal headers for checksum/architecture checks only; never execute this fixture.
  const data = Buffer.alloc(256);
  data.write('MZ'); data.writeUInt32LE(128, 0x3c); data.write('PE\0\0', 128);
  data.writeUInt16LE(arch === 'x64' ? 0x8664 : 0x14c, 132);
  data.writeUInt16LE(arch === 'x64' ? 0x20b : 0x10b, 152);
  await fs.writeFile(cli, data);
  const manifestPath = path.join(base, 'manifest.json');
  const manifest = { format: 1, product: 'dll-doctor-cli', platform: 'windows', architecture: arch,
    tool_version: '2.0.0-dev.9', schema_version: '1.6', source_revision: null, runtime: 'msvc-v14',
    files: { 'dllinjector.exe': { size: data.length, sha256: hash(data) } } };
  await fs.writeFile(manifestPath, JSON.stringify(manifest));
  const options = { agent: 'codex', scope: 'project', project, cli, manifest: manifestPath };
  let probes = 0;
  const context = { probe: async file => {
    probes++;
    return { path: file, tool_version: '2.0.0-dev.9', schema_version: '1.6', sha256: hash(await fs.readFile(file)) };
  } };
  t.after(async () => {
    assert.equal(path.dirname(base), await fs.realpath(os.tmpdir()));
    assert.ok(path.basename(base).startsWith('dll-distribution-test-'));
    await fs.rm(base, { recursive: true });
  });
  return { project, cli, data, manifestPath, manifest, options, context, probes: () => probes };
}

for (const arch of ['x86', 'x64']) {
  test('Rondo ' + arch + ' manifest uses only rondo.exe while legacy manifests remain valid', async t => {
    const f = await fixture(t, arch);
    const cli = path.join(path.dirname(f.cli), 'rondo.exe');
    await fs.rename(f.cli, cli);
    const manifest = { ...f.manifest, product: 'rondo-cli', files: { 'rondo.exe': f.manifest.files['dllinjector.exe'] } };
    await fs.writeFile(f.manifestPath, JSON.stringify(manifest));
    assert.equal((await verifyDistribution(cli, f.manifestPath)).architecture, arch);
    assert.equal((await install('setup', { ...f.options, cli, dryRun: true }, f.context)).distribution_checksum, 'verified');
    manifest.product = 'dll-doctor-cli';
    await fs.writeFile(f.manifestPath, JSON.stringify(manifest));
    await assert.rejects(verifyDistribution(cli, f.manifestPath), { code: 'DISTRIBUTION_INVALID' });
  });
  test('verified ' + arch + ' distribution previews without execution and binds its identity', async t => {
    const f = await fixture(t, arch);
    const preview = await install('setup', { ...f.options, dryRun: true }, f.context);
    assert.equal(preview.distribution_checksum, 'verified');
    assert.equal(preview.cli_probe, 'not_run');
    assert.equal(f.probes(), 0);
    assert.deepEqual(await fs.readdir(f.project), []);
    const result = await install('setup', { ...f.options, verify: true }, f.context);
    assert.equal(result.action, 'installed');
    const binding = JSON.parse(await fs.readFile(path.join(result.target, 'references/installation.json')));
    assert.equal(binding.cli.distribution.architecture, arch);
    assert.equal(binding.cli.distribution.manifest_sha256, hash(await fs.readFile(f.manifestPath)));
    assert.equal((await install('setup', f.options, f.context)).action, 'unchanged');
  });
}

test('invalid metadata is rejected before probing or writing an installation', async t => {
  const f = await fixture(t);
  const variants = [null, {}, { ...f.manifest, format: 2 }, { ...f.manifest, product: 'other' },
    { ...f.manifest, platform: 'linux' }, { ...f.manifest, architecture: 'arm64' },
    { ...f.manifest, source_revision: 'not-a-commit' }, { ...f.manifest, runtime: 'other' },
    { ...f.manifest, tool_version: '1.0.0' }, { ...f.manifest, schema_version: '99.0' },
    { ...f.manifest, files: { '../dllinjector.exe': f.manifest.files['dllinjector.exe'] } },
    { ...f.manifest, files: { 'dllinjector.exe': { size: -1, sha256: 'a'.repeat(64) } } },
    { ...f.manifest, files: { 'dllinjector.exe': { size: 2 ** 30, sha256: 'a'.repeat(64) } } },
    { ...f.manifest, files: { 'dllinjector.exe': { size: 256, sha256: 'bad-hash' } } }];
  for (const value of variants) {
    await fs.writeFile(f.manifestPath, JSON.stringify(value));
    await assert.rejects(install('setup', f.options, f.context), { code: 'DISTRIBUTION_INVALID' });
  }
  for (const text of ['{invalid', ' '.repeat(65537)]) {
    await fs.writeFile(f.manifestPath, text);
    await assert.rejects(install('setup', f.options, f.context), { code: 'DISTRIBUTION_INVALID' });
  }
  await assert.rejects(install('setup', { ...f.options, manifest: '' }, f.context), { code: 'DISTRIBUTION_INVALID' });
  assert.equal(f.probes(), 0);
  assert.deepEqual(await fs.readdir(f.project), []);
});

test('size, hash, machine and PE magic failures do not execute the candidate', async t => {
  const f = await fixture(t);
  const badHeader = Buffer.from(f.data); badHeader.writeUInt32LE(0xfffffff0, 0x3c);
  const wrongMagic = Buffer.from(f.data); wrongMagic.writeUInt16LE(0x10b, 152);
  for (const bytes of [f.data.subarray(0, 63), badHeader, wrongMagic]) {
    await fs.writeFile(f.cli, bytes);
    await fs.writeFile(f.manifestPath, JSON.stringify({ ...f.manifest,
      files: { 'dllinjector.exe': { size: bytes.length, sha256: hash(bytes) } } }));
    await assert.rejects(install('setup', f.options, f.context), { code: 'DISTRIBUTION_INVALID' });
  }
  await fs.writeFile(f.cli, f.data);
  for (const change of [{ architecture: 'x86' },
    { files: { 'dllinjector.exe': { size: 256, sha256: '0'.repeat(64) } } },
    { files: { 'dllinjector.exe': { size: 257, sha256: hash(f.data) } } }]) {
    await fs.writeFile(f.manifestPath, JSON.stringify({ ...f.manifest, ...change }));
    await assert.rejects(install('setup', f.options, f.context), { code: 'DISTRIBUTION_INVALID' });
  }
  assert.equal(f.probes(), 0);
  assert.deepEqual(await fs.readdir(f.project), []);
});

test('runtime version or byte mismatch rolls installation back', async t => {
  const f = await fixture(t);
  for (const changes of [{ tool_version: '2.0.0-dev.8' }, { sha256: '0'.repeat(64) }]) {
    await assert.rejects(install('setup', f.options, { probe: async file => ({ ...await f.context.probe(file), ...changes }) }),
      { code: 'DISTRIBUTION_MISMATCH' });
    assert.deepEqual(await fs.readdir(f.project), []);
  }
});

test('manifest is setup-only and a directory cannot substitute for it', async t => {
  const f = await fixture(t);
  await assert.rejects(install('uninstall', f.options, f.context), { code: 'ARGUMENT' });
  await assert.rejects(verifyDistribution(f.cli, f.project), { code: 'DISTRIBUTION_INVALID' });
  assert.equal(f.probes(), 0);
  assert.deepEqual(await fs.readdir(f.project), []);
});
