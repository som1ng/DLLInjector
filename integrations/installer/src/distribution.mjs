import * as fs from 'node:fs/promises';
import path from 'node:path';
import { createHash } from 'node:crypto';

const digest = data => createHash('sha256').update(data).digest('hex');
function invalid(message) {
  const error = new Error(message);
  error.code = 'DISTRIBUTION_INVALID';
  throw error;
}

export function peArchitecture(data) {
  if (data.length < 64 || data.toString('ascii', 0, 2) !== 'MZ') invalid('CLI PE header is invalid.');
  const offset = data.readUInt32LE(0x3c);
  if (offset < 64 || offset > data.length - 26 || !data.subarray(offset, offset + 4).equals(Buffer.from('PE\0\0'))) {
    invalid('CLI PE header is invalid.');
  }
  const machine = data.readUInt16LE(offset + 4), magic = data.readUInt16LE(offset + 24);
  if (machine === 0x14c && magic === 0x10b) return 'x86';
  if (machine === 0x8664 && magic === 0x20b) return 'x64';
  invalid('CLI machine and optional-header magic are unsupported or inconsistent.');
}

export async function verifyDistribution(cli, manifestPath) {
  if (!path.isAbsolute(manifestPath)) invalid('Use an absolute manifest path.');
  const entry = await fs.lstat(manifestPath);
  if (!entry.isFile() || entry.isSymbolicLink() || entry.size > 65536) invalid('Manifest must be a regular JSON file of at most 64 KiB.');
  const raw = await fs.readFile(manifestPath);
  if (raw.length > 65536) invalid('Manifest exceeds the size limit.');
  let manifest;
  try { manifest = JSON.parse(raw.toString('utf8')); }
  catch { invalid('Manifest is not valid JSON.'); }
  const filename = manifest?.product === 'rondo-cli' ? 'rondo.exe' : 'dllinjector.exe';
  if (!manifest || manifest.format !== 1 || !['rondo-cli', 'dll-doctor-cli'].includes(manifest.product) || manifest.platform !== 'windows'
      || !['x86', 'x64'].includes(manifest.architecture) || manifest.runtime !== 'msvc-v14'
      || !['2.0.0-dev.8', '2.0.0-dev.9'].includes(manifest.tool_version) || manifest.schema_version !== '1.6'
      || !(manifest.source_revision === null || (typeof manifest.source_revision === 'string' && /^[0-9a-f]{40}$/.test(manifest.source_revision)))
      || !manifest.files || Object.keys(manifest.files).join() !== filename) invalid('Manifest contract is invalid or unsupported.');
  const file = manifest.files[filename];
  if (!file || !Number.isSafeInteger(file.size) || file.size < 64 || file.size > 64 * 1024 * 1024
      || typeof file.sha256 !== 'string' || !/^[0-9a-f]{64}$/.test(file.sha256)
      || path.basename(cli).toLowerCase() !== filename) invalid('Manifest file metadata is invalid.');
  const current = await fs.stat(cli);
  if (!current.isFile() || current.size !== file.size) invalid('CLI size differs from the manifest.');
  const bytes = await fs.readFile(cli);
  if (bytes.length !== file.size || digest(bytes) !== file.sha256) invalid('CLI SHA-256 differs from the manifest.');
  if (peArchitecture(bytes) !== manifest.architecture) invalid('CLI architecture differs from the manifest.');
  return { format: 1, manifest_sha256: digest(raw), sha256: file.sha256,
    tool_version: manifest.tool_version, schema_version: manifest.schema_version,
    architecture: manifest.architecture, source_revision: manifest.source_revision };
}
