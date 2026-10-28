import { createHash } from 'node:crypto';
import { isDeepStrictEqual } from 'node:util';
import { parse as tomlParse } from 'smol-toml';
import { parseTree, findNodeAtLocation, getNodeValue, modify, applyEdits, createScanner, SyntaxKind } from 'jsonc-parser';
import { InstallError } from './installer.mjs';

export const SERVER_NAME = 'dll_doctor';
export const hash = data => createHash('sha256').update(data).digest('hex');
const object = value => value !== null && typeof value === 'object' && !Array.isArray(value);
const key = agent => agent === 'claude' ? 'mcpServers' : agent === 'codex' ? 'mcp_servers' : 'mcp';
const fail = message => { throw new InstallError('MCP_CONFIG', message); };
// Parsers use null-prototype objects; compare the JSON-compatible managed entry by value.
const plainEntry = value => value === undefined ? undefined : JSON.parse(JSON.stringify(value));

function preserveComments(agent, before, after) {
  if (agent !== 'opencode') return;
  const comments = text => {
    const scanner = createScanner(text), found = [];
    let token;
    while ((token = scanner.scan()) !== SyntaxKind.EOF) {
      if (token === SyntaxKind.LineCommentTrivia || token === SyntaxKind.BlockCommentTrivia) {
        found.push(text.slice(scanner.getTokenOffset(), scanner.getTokenOffset() + scanner.getTokenLength()));
      }
    }
    return found;
  };
  if (!isDeepStrictEqual(comments(before), comments(after))) {
    fail('This JSONC layout cannot be edited without losing comments; preserving configuration.');
  }
}

export function inspectConfig(agent, text) {
  try {
    if (agent === 'codex') {
      const data = tomlParse(text, { integersAsBigInt: true });
      if (data.mcp_servers !== undefined && !object(data.mcp_servers)) fail('MCP container must be an object/table.');
      return { data, entry: plainEntry(data.mcp_servers?.[SERVER_NAME]) };
    }
    const errors = [];
    const tree = parseTree(text, errors, { allowTrailingComma: agent === 'opencode', disallowComments: agent === 'claude' });
    if (errors.length || tree?.type !== 'object') fail('Configuration must be a valid UTF-8 object.');
    function unique(node) {
      if (node.type === 'object') {
        const names = node.children.map(property => property.children[0].value);
        if (new Set(names).size !== names.length) fail('Duplicate configuration keys are unsupported.');
      }
      for (const child of node.children || []) unique(child);
    }
    unique(tree);
    const data = getNodeValue(tree);
    if (data[key(agent)] !== undefined && !object(data[key(agent)])) fail('MCP container must be an object.');
    const node = findNodeAtLocation(tree, [key(agent), SERVER_NAME]);
    return { data, entry: node ? plainEntry(getNodeValue(node)) : undefined,
      entryTextHash: node ? hash(text.slice(node.offset, node.offset + node.length)) : undefined };
  } catch (error) {
    if (error instanceof InstallError) throw error;
    fail('Cannot parse this configuration; preserve it and correct its syntax first.');
  }
}

export function mcpEntry(agent, runtime) {
  const args = [runtime.server.path, '--cli', runtime.cli.path];
  for (const root of runtime.roots) args.push('--allow-root', root);
  if (runtime.allowProcesses) args.push('--allow-processes');
  return agent === 'opencode' ? { type: 'local', command: [runtime.node, ...args], enabled: true }
    : { command: runtime.node, args };
}

export function addEntry(agent, original, entry) {
  const text = original ?? (agent === 'codex' ? '' : '{}\n');
  const before = inspectConfig(agent, text);
  if (before.entry !== undefined) fail('An unowned dll_doctor entry already exists.');
  let updated, block;
  if (agent === 'codex') {
    block = '\n# BEGIN dll-doctor managed MCP\n[mcp_servers.dll_doctor]\ncommand = '
      + JSON.stringify(entry.command) + '\nargs = ' + JSON.stringify(entry.args)
      + '\n# END dll-doctor managed MCP\n';
    updated = text + block;
  } else {
    updated = applyEdits(text, modify(text, [key(agent), SERVER_NAME], entry,
      { formattingOptions: { insertSpaces: true, tabSize: 2, eol: text.includes('\r\n') ? '\r\n' : '\n' } }));
  }
  const after = inspectConfig(agent, updated);
  preserveComments(agent, text, updated);
  if (!isDeepStrictEqual(after.entry, entry)) fail('Configuration layout cannot accept the managed entry.');
  return { text: updated, ownership: { block, entryTextHash: after.entryTextHash,
    containerCreated: before.data[key(agent)] === undefined, configCreated: original === null,
    installedHash: hash(updated) } };
}

export function assertOwned(agent, text, record) {
  const observed = inspectConfig(agent, text);
  if (!isDeepStrictEqual(observed.entry, record.entry)) fail('Managed MCP values changed or are missing; preserving configuration.');
  if (agent === 'codex') {
    if (typeof record.ownership.block !== 'string' || !record.ownership.block.startsWith('\n# BEGIN dll-doctor managed MCP\n')
        || text.split(record.ownership.block).length !== 2) fail('Managed TOML block changed; preserving configuration.');
    // Never trust arbitrary deletion text supplied by a changed receipt.
    const expected = addEntry(agent, null, record.entry).ownership.block;
    if (record.ownership.block !== expected) fail('Managed TOML ownership record is invalid.');
  } else if (observed.entryTextHash !== record.ownership.entryTextHash) {
    fail('Managed MCP text changed; preserving configuration.');
  }
}

export function removeEntry(agent, text, record) {
  assertOwned(agent, text, record);
  const unchanged = hash(text) === record.ownership.installedHash;
  if (unchanged && record.ownership.configCreated) {
    if (text !== addEntry(agent, null, record.entry).text) fail('Receipt cannot authorize removing a configuration containing other content.');
    return null;
  }
  let updated;
  if (agent === 'codex') updated = text.replace(record.ownership.block, '');
  else {
    const container = inspectConfig(agent, text).data[key(agent)];
    const location = unchanged && record.ownership.containerCreated && Object.keys(container).length === 1
      ? [key(agent)] : [key(agent), SERVER_NAME];
    updated = applyEdits(text, modify(text, location, undefined, {}));
  }
  if (inspectConfig(agent, updated).entry !== undefined) fail('Managed entry could not be removed.');
  preserveComments(agent, text, updated);
  return updated;
}

export function updateEntry(agent, text, record, entry) {
  assertOwned(agent, text, record);
  const canonical = addEntry(agent, null, entry);
  const updated = agent === 'codex' ? text.replace(record.ownership.block, canonical.ownership.block)
    : applyEdits(text, modify(text, [key(agent), SERVER_NAME], entry,
      { formattingOptions: { insertSpaces: true, tabSize: 2, eol: text.includes('\r\n') ? '\r\n' : '\n' } }));
  const after = inspectConfig(agent, updated);
  preserveComments(agent, text, updated);
  if (!isDeepStrictEqual(after.entry, entry)) fail('Configuration layout cannot update the managed entry.');
  return { text: updated, ownership: {
    block: canonical.ownership.block, entryTextHash: after.entryTextHash,
    // An upgrade must not adopt unrelated content added since the original setup.
    configCreated: record.ownership.configCreated && text === addEntry(agent, null, record.entry).text,
    containerCreated: record.ownership.containerCreated && hash(text) === record.ownership.installedHash,
    installedHash: hash(updated),
  } };
}
