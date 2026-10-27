import { destination, install, InstallError, stat } from './installer.mjs';

// Codex and OpenCode read the same managed Skill directory.
const groups = [{ agent: 'codex', agents: ['codex', 'opencode'] }, { agent: 'claude', agents: ['claude'] }];

export async function setupAll(options, context = {}) {
  if (options.agent !== 'all' || (options.integration ?? 'skill') !== 'skill'
      || options.select !== undefined || options.client !== undefined || options.server !== undefined
      || options.allowRoot !== undefined || options.allowProcesses) {
    throw new InstallError('ARGUMENT', '--agent all supports Skill setup only; use explicit agents for MCP, verify and uninstall.');
  }
  const result = { status: 'success', command: 'setup', agent: 'all', integration: 'skill', scope: options.scope,
    agents: ['codex', 'claude', 'opencode'], client_discovery: 'not_run' };
  const completed = [];
  const previews = [];
  let active, activeTarget, writing = false;
  try {
    // Inspect every destination before the first write. Dry-run also validates
    // existing receipts and an optional CLI manifest, without starting programs.
    for (active of groups) {
      activeTarget = undefined;
      activeTarget = destination({ ...options, agent: active.agent }, context.home, context.env).target;
      const preview = await install('setup', { ...options, agent: active.agent, dryRun: true }, context);
      previews.push({ ...preview, agents: active.agents });
    }
    if (options.dryRun) return { ...result, action: 'preview', targets: previews };

    writing = true;
    for (active of groups) {
      activeTarget = previews.find(item => item.agent === active.agent).target;
      const installed = await install('setup', { ...options, agent: active.agent }, context);
      completed.push({ ...installed, agents: active.agents });
    }
    return { ...result, action: completed.some(item => item.action === 'installed') ? 'installed' : 'unchanged', targets: completed };
  } catch (cause) {
    const rollback = [];
    // The failing target normally handles its own rollback (checked below). Only remove installations
    // successfully created by this batch; never remove a pre-existing target.
    for (const item of [...completed].reverse()) {
      if (item.action !== 'installed') continue;
      try {
        const removed = await install('uninstall', { agent: item.agent, scope: options.scope, project: options.project }, context);
        rollback.push({ agent: item.agent, target: item.target, action: removed.action });
      } catch (error) {
        rollback.push({ agent: item.agent, target: item.target, action: 'failed', code: error.code || 'INSTALL_FAILED' });
      }
    }
    // A failure in single-target lock cleanup can escape after committing. Do
    // not claim rollback completed or remove a directory with uncertain state.
    let failedTargetState;
    if (writing && previews.find(item => item.agent === active.agent)?.exists === false) {
      try { failedTargetState = await stat(activeTarget) ? 'present' : 'absent'; }
      catch { failedTargetState = 'unavailable'; }
    }
    const incomplete = cause.code === 'ROLLBACK_INCOMPLETE' || rollback.some(item => item.action === 'failed')
      || ['present', 'unavailable'].includes(failedTargetState);
    const error = new InstallError(incomplete ? 'ROLLBACK_INCOMPLETE' : (cause.code || 'BATCH_SETUP_FAILED'),
      incomplete ? 'Batch setup failed and rollback is incomplete; preserve and inspect the reported targets.'
        : 'Batch setup failed; completed new installations were rolled back and existing installations were preserved.');
    error.details = { agent: 'all', integration: 'skill', failed_agent: active?.agent, failed_target: activeTarget,
      cause_code: cause.code || 'INSTALL_FAILED', cause_message: cause instanceof InstallError ? cause.message : 'Installation failed.',
      ...(failedTargetState ? { failed_target_state: failedTargetState } : {}),
      rollback_status: incomplete ? 'incomplete' : 'complete', completed_targets: completed.map(({ agent, target, action }) => ({ agent, target, action })), rollback };
    throw error;
  }
}
