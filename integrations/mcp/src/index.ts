#!/usr/bin/env node
import { serveStdio, StdioServerTransport } from '@modelcontextprotocol/server/stdio';
import { publicError } from './errors.js';
import { parseConfig } from './policy.js';
import { createServer, verifyCli } from './server.js';

try {
  const config = await parseConfig(process.argv.slice(2));
  await verifyCli(config);
  const handle = serveStdio(() => createServer(config), {
    transport: new StdioServerTransport(process.stdin, process.stdout, { maxBufferSize: 1048576 }),
    onerror: () => process.stderr.write('{"event":"protocol_error"}\n'),
  });
  for (const signal of ['SIGINT', 'SIGTERM'] as const) {
    process.once(signal, () => { void handle.close(); });
  }
} catch (error) {
  const failure = publicError(error);
  process.stderr.write(JSON.stringify({ event: 'startup_error', code: failure.code, message: failure.message }) + '\n');
  process.exitCode = 1;
}
