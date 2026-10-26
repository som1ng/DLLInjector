// Use the installed client's resource loader; do not create a session or call a model.
import { pathToFileURL } from 'node:url';
import { join } from 'node:path';

const [packageDir, cwd, agentDir, trust, mode] = process.argv.slice(2);
const load = (file) => import(pathToFileURL(join(packageDir, 'dist/core', file)).href);
const { DefaultResourceLoader } = await load('resource-loader.js');
const { SettingsManager } = await load('settings-manager.js');
const { formatSkillsForPrompt } = await load('skills.js');
const loader = new DefaultResourceLoader({
    cwd, agentDir,
    settingsManager: mode === 'packages'
        ? SettingsManager.create(cwd, agentDir, { projectTrusted: trust === 'true' })
        : SettingsManager.inMemory({}, { projectTrusted: trust === 'true' }),
    noExtensions: true,
    disabledBuiltinExtensions: ['mcp'],
    noPromptTemplates: true,
    noThemes: true,
    noContextFiles: true,
});
await loader.reload();
const { skills, diagnostics } = loader.getSkills();
const selected = skills.filter((skill) => skill.name === 'windows-dll-doctor');
console.log(JSON.stringify({
    skills: selected.map((skill) => ({
        name: skill.name,
        description: skill.description,
        path: skill.filePath,
        enabled: !skill.disableModelInvocation,
        scope: skill.sourceInfo.scope,
        advertised: formatSkillsForPrompt([skill]).includes('<name>windows-dll-doctor</name>'),
    })),
    diagnostics: diagnostics.filter((item) => item.path?.includes('windows-dll-doctor')),
}));
