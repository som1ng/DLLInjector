import { install, InstallError } from './installer.mjs';

const agents = ['codex', 'claude', 'opencode'];
const copyTree = tree => Object.fromEntries(Object.entries(tree).map(([file, bytes]) => [file, Buffer.from(bytes)]));

export async function upgradeAll(options, context = {}) {
  const integration = options.integration ?? 'skill';
  if (options.agent !== 'all' || !['skill', 'mcp'].includes(integration)
      || options.select !== undefined || options.client !== undefined
      || (integration === 'skill' && (options.server !== undefined || options.allowRoot !== undefined
        || options.allowProcesses || Object.keys(options.clients ?? {}).length))
      || (integration === 'mcp' && (!options.clients || agents.some(agent => !options.clients[agent])
        || Object.keys(options.clients).some(agent => !agents.includes(agent))))) {
    throw new InstallError('ARGUMENT', 'Batch upgrade requires --agent all; MCP requires all three per-agent client paths, without --client or --select.');
  }
  // Keep Skill-only installations independent of the optional MCP dependencies.
  const run = integration === 'mcp' ? (await import('./mcp.mjs')).installMcp : install;
  const groups = integration === 'mcp' ? agents.map(agent => ({ agent, agents: [agent] }))
    : [{ agent: 'codex', agents: ['codex', 'opencode'] }, { agent: 'claude', agents: ['claude'] }];
  const result = { status: 'success', command: 'upgrade', agent: 'all', integration, scope: options.scope,
    agents, client_discovery: 'not_run' };
  const previews = [], completed = [], states = new Map();
  let active;
  try {
    for (active of groups) {
      const preview = await run('upgrade', { ...options, agent: active.agent,
        client: integration === 'mcp' ? options.clients[active.agent] : undefined, dryRun: true }, context);
      previews.push({ ...preview, agents: active.agents });
    }
    if (options.dryRun) return { ...result, action: 'preview', targets: previews };
    for (active of groups) {
      const upgraded = await run('upgrade', { ...options, agent: active.agent,
        client: integration === 'mcp' ? options.clients[active.agent] : undefined }, { ...context,
        captureUpgrade: async state => {
          // Snapshot bytes can contain private user settings; retain them only in memory.
          states.set(active.agent, integration === 'skill'
            ? { target: state.target, before: copyTree(state.before), after: copyTree(state.after) }
            : { config: state.config, receipt: state.receipt, ...Object.fromEntries(
              ['configBefore', 'receiptBefore', 'configAfter', 'receiptAfter'].map(key => [key, Buffer.from(state[key])])) });
          await context.captureUpgrade?.(state);
        },
      });
      completed.push({ ...upgraded, agents: active.agents });
    }
    return { ...result, action: completed.some(item => item.action === 'upgraded') ? 'upgraded' : 'unchanged', targets: completed };
  } catch (cause) {
    const rollback = [];
    // Include the failing target: its cleanup can throw after a successful commit.
    for (const [agent, state] of [...states].reverse()) {
      const paths = integration === 'skill' ? { target: state.target } : { config: state.config, receipt: state.receipt };
      try {
        const restored = await run('rollback-upgrade', { agent, scope: options.scope, project: options.project },
          { ...context, checkpoint: undefined, captureUpgrade: undefined, rollbackUpgrade: state });
        rollback.push({ agent, ...paths, action: restored.action });
      } catch (error) {
        rollback.push({ agent, ...paths, action: 'failed', code: error.code || 'INSTALL_FAILED' });
      }
    }
    const incomplete = cause.code === 'ROLLBACK_INCOMPLETE' || rollback.some(item => item.action === 'failed');
    const error = new InstallError(incomplete ? 'ROLLBACK_INCOMPLETE' : (cause.code || 'BATCH_UPGRADE_FAILED'),
      incomplete ? 'Batch upgrade failed and rollback is incomplete; preserve and inspect the reported targets.'
        : 'Batch upgrade failed; prior configuration and receipts were restored, unchanged targets were preserved.');
    const paths = item => integration === 'skill' ? { target: item.target } : { config: item.config, receipt: item.receipt };
    const failed = previews.find(item => item.agent === active?.agent);
    error.details = { command: 'upgrade', integration, agent: 'all', failed_agent: active?.agent,
      ...(failed ? paths(failed) : {}), cause_code: cause.code || 'INSTALL_FAILED',
      rollback_status: incomplete ? 'incomplete' : 'complete',
      completed_targets: completed.map(item => ({ agent: item.agent, action: item.action, ...paths(item) })), rollback };
    throw error;
  }
}
