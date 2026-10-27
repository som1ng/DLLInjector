import { installMcp } from './mcp.mjs';
import { InstallError, stat } from './installer.mjs';

const agents = ['codex', 'claude', 'opencode'];

export async function setupAllMcp(options, context = {}) {
  if (options.agent !== 'all' || options.integration !== 'mcp' || options.scope !== 'project'
      || options.client !== undefined || options.select !== undefined
      || !options.clients || agents.some(agent => !options.clients[agent])
      || Object.keys(options.clients).some(agent => !agents.includes(agent))) {
    throw new InstallError('MCP_ARGUMENT', 'Batch MCP setup requires project scope and --codex-client, --claude-client, --opencode-client; do not use --client or --select.');
  }
  const result = { status: 'success', integration: 'mcp', command: 'setup', agent: 'all', scope: 'project',
    agents, client_discovery: 'not_run' };
  const previews = [], completed = [], commits = new Map();
  let active, writing = false;
  try {
    for (active of agents) {
      const preview = await installMcp('setup', { ...options, agent: active, client: options.clients[active], dryRun: true }, context);
      previews.push({ ...preview, exists: !!await stat(preview.receipt) });
    }
    if (options.dryRun) return { ...result, action: 'preview', targets: previews };

    writing = true;
    for (active of agents) {
      const installed = await installMcp('setup', { ...options, agent: active, client: options.clients[active] }, {
        ...context,
        checkpoint: async (stage, data) => {
          if (stage === 'receipt-written') {
            // Configuration bytes may contain unrelated secrets. Keep them only
            // in memory for rollback; never return them in results or errors.
            commits.set(active, { ...data, configBefore: data.configBefore === null ? null : Buffer.from(data.configBefore),
              configAfter: Buffer.from(data.configAfter), receiptAfter: Buffer.from(data.receiptAfter) });
          }
          await context.checkpoint?.(stage, data);
        },
      });
      completed.push(installed);
    }
    return { ...result, action: completed.some(item => item.action === 'installed') ? 'installed' : 'unchanged', targets: completed };
  } catch (cause) {
    const rollback = [];
    for (const item of [...completed].reverse()) {
      if (item.action !== 'installed') continue;
      try {
        if (!commits.has(item.agent)) throw new InstallError('ROLLBACK_INCOMPLETE', 'The batch cannot identify its installation.');
        const removed = await installMcp('uninstall', { agent: item.agent, scope: 'project', project: options.project },
          { ...context, rollbackState: commits.get(item.agent) });
        rollback.push({ agent: item.agent, config: item.config, receipt: item.receipt, action: removed.action });
      } catch (error) {
        rollback.push({ agent: item.agent, config: item.config, receipt: item.receipt, action: 'failed', code: error.code || 'INSTALL_FAILED' });
      }
    }
    const failed = previews.find(item => item.agent === active);
    let failedTargetState;
    if (writing && failed && !failed.exists) {
      try { failedTargetState = await stat(failed.receipt) ? 'present' : 'absent'; }
      catch { failedTargetState = 'unavailable'; }
    }
    const incomplete = cause.code === 'ROLLBACK_INCOMPLETE' || rollback.some(item => item.action === 'failed')
      || ['present', 'unavailable'].includes(failedTargetState);
    const error = new InstallError(incomplete ? 'ROLLBACK_INCOMPLETE' : (cause.code || 'BATCH_MCP_FAILED'),
      incomplete ? 'Batch MCP setup failed and rollback is incomplete; preserve and inspect the reported configurations.'
        : 'Batch MCP setup failed; new entries were rolled back and existing entries preserved.');
    error.details = { integration: 'mcp', agent: 'all', failed_agent: active,
      ...(failed ? { failed_config: failed.config, failed_receipt: failed.receipt } : {}),
      cause_code: cause.code || 'INSTALL_FAILED', rollback_status: incomplete ? 'incomplete' : 'complete',
      ...(failedTargetState ? { failed_target_state: failedTargetState } : {}),
      completed_targets: completed.map(({ agent, config, receipt, action }) => ({ agent, config, receipt, action })), rollback };
    throw error;
  }
}
