import { install, InstallError } from './installer.mjs';
import { beginBatch, recoverAll, assertNoPendingBatch } from './batch-recovery.mjs';

const agents = ['codex', 'claude', 'opencode'];

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
  const previews = [], completed = [];
  let active, batch, committed = false;
  await assertNoPendingBatch(options, context);
  try {
    for (active of groups) {
      const preview = await run('upgrade', { ...options, agent: active.agent,
        client: integration === 'mcp' ? options.clients[active.agent] : undefined, dryRun: true }, context);
      previews.push({ ...preview, agents: active.agents });
    }
    if (options.dryRun) return { ...result, action: 'preview', targets: previews };
    batch = await beginBatch(options, context);
    await context.batchCheckpoint?.('batch-prepared');
    for (active of groups) {
      const upgraded = await run('upgrade', { ...options, agent: active.agent,
        client: integration === 'mcp' ? options.clients[active.agent] : undefined }, { ...context,
        batchUpgrade: batch.info,
        captureUpgrade: async state => {
          // Hard-link the bounded, owned-only recovery record before this target writes.
          await batch.capture(active.agent);
          await context.captureUpgrade?.(state);
        },
      });
      completed.push({ ...upgraded, agents: active.agents });
      await batch.check();
      await context.batchCheckpoint?.('target-completed', { agent: active.agent });
    }
    await recoverAll({ agent: 'all', integration, scope: options.scope, project: options.project, dryRun: true },
      { ...context, batchOwner: batch.info, batchExpected: 'committed' });
    await batch.commit(); committed = true;
    await batch.close();
    await context.batchCheckpoint?.('batch-committed');
    const cleanup = await recoverAll({ agent: 'all', integration, scope: options.scope, project: options.project }, { ...context, batchOwner: batch.info });
    if (cleanup.action !== 'completed') throw new InstallError('RECOVERY_CONFLICT', 'Committed batch cleanup is incomplete.');
    return { ...result, action: completed.some(item => item.action === 'upgraded') ? 'upgraded' : 'unchanged', targets: completed };
  } catch (cause) {
    await batch?.close();
    if (committed) {
      const error = new InstallError('BATCH_CLEANUP_REQUIRED', 'Batch upgrade committed, but record cleanup needs recover --agent all with the same integration and scope.');
      error.details = { command: 'upgrade', integration, agent: 'all', committed: true, journal: batch.info.journal,
        cause_code: cause.code || 'INSTALL_FAILED' };
      throw error;
    }
    let rollback = [];
    if (batch) {
      try {
        const restored = await recoverAll({ agent: 'all', integration, scope: options.scope, project: options.project },
          { ...context, checkpoint: undefined, captureUpgrade: undefined, batchOwner: batch.info });
        rollback = restored.action === 'absent' ? [{ agent: active?.agent, action: 'failed', code: 'RECOVERY_JOURNAL' }] : restored.targets;
      } catch (error) {
        rollback = error.details?.targets ?? [{ agent: active?.agent, action: 'failed', code: error.code || 'INSTALL_FAILED' }];
      }
    }
    const incomplete = rollback.some(item => item.action === 'failed');
    const error = new InstallError(incomplete ? 'ROLLBACK_INCOMPLETE' : (cause.code || 'BATCH_UPGRADE_FAILED'),
      incomplete ? 'Batch upgrade failed and rollback is incomplete; preserve the records and use recover --agent all after inspecting the reported targets.'
        : 'Batch upgrade failed; prior configuration and receipts were restored, unchanged targets were preserved.');
    const paths = item => integration === 'skill' ? { target: item.target } : { config: item.config, receipt: item.receipt };
    const failed = previews.find(item => item.agent === active?.agent);
    error.details = { command: 'upgrade', integration, agent: 'all', failed_agent: active?.agent,
      ...(batch ? { journal: batch.info.journal } : {}),
      ...(failed ? paths(failed) : {}), cause_code: cause.code || 'INSTALL_FAILED',
      rollback_status: incomplete ? 'incomplete' : 'complete',
      completed_targets: completed.map(item => ({ agent: item.agent, action: item.action, ...paths(item) })), rollback };
    throw error;
  }
}
