import { readFileSync } from 'node:fs';
import { Ajv2020 } from 'ajv/dist/2020.js';
import { AdapterError } from './errors.js';

export const CLI_VERSION = '2.0.0-dev.9';
export const SCHEMA_VERSION = '1.6';
const ajv = new Ajv2020({ strict: false, allErrors: false });
const validators = new Map(['1.0', '1.1', '1.2', '1.3', '1.4', '1.5', '1.6'].map(version => {
  const file = version === '1.0' ? '1' : version;
  const schema = JSON.parse(readFileSync(new URL(`./schemas/cli-v${file}.json`, import.meta.url), 'utf8'));
  return [version, ajv.compile(schema)];
}));

export interface NativeReport extends Record<string, unknown> {
  schema_version: string;
  tool_version: string;
  command: string;
  status: 'success' | 'error';
}

export function parseReport(bytes: Buffer, code: number, command: string, saved = false): NativeReport {
  let report: NativeReport;
  try {
    report = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes), (_key, value: unknown) => {
      if (typeof value === 'number' && !Number.isSafeInteger(value)) {
        throw new AdapterError('UNSAFE_INTEGER', 'Report has integers outside the exact JavaScript range; use native report export.');
      }
      return value;
    });
  } catch (error) {
    if (error instanceof AdapterError) throw error;
    throw new AdapterError('CLI_JSON', 'CLI did not return one valid UTF-8 JSON report.');
  }
  const validate = validators.get(report?.schema_version);
  if (!validate || !validate(report)) throw new AdapterError('CLI_SCHEMA', 'CLI report does not match a bundled supported schema.');
  if (saved && code === 0) return report;
  if (report.schema_version !== SCHEMA_VERSION || report.tool_version !== CLI_VERSION || report.command !== command) {
    throw new AdapterError('CLI_VERSION', `Expected DLLInjector ${CLI_VERSION}, schema ${SCHEMA_VERSION}, and the requested command.`);
  }
  if (code === 0 ? report.status !== 'success' : report.status === 'success' && !(command === 'doctor' && code === 1)) {
    throw new AdapterError('CLI_EXIT', 'CLI exit code is inconsistent with its report status.');
  }
  return report;
}
