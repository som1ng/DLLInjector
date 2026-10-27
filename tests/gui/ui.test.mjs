import { before, after, beforeEach, afterEach, test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { createRequire } from 'node:module';

const { chromium } = createRequire(import.meta.url)(process.env.DLL_GUI_PLAYWRIGHT || 'playwright');
const source = fs.readFileSync(new URL('../../apps/gui/ui_view.h', import.meta.url), 'utf8');
const chunks = Object.fromEntries([...source.matchAll(/static constexpr std::string_view (\w+) = R"(\w+)\(([\s\S]*?)\)\2";/g)].map(m => [m[1], m[3]]));
chunks.kLogoBase64 = source.match(/kLogoBase64 = "([^"]+)";/)[1];
const html = [...source.matchAll(/html.append\((\w+)\)/g)].map(m => {
  assert.equal(typeof chunks[m[1]], 'string', 'each native HTML chunk must be present');
  return chunks[m[1]];
}).join('');

let browser, page;
before(async () => { browser = await chromium.launch({ channel: 'msedge', headless: true }); });
after(async () => { await browser?.close(); });
beforeEach(async () => {
  page = await browser.newPage({ viewport: { width: 1140, height: 720 } });
  await page.evaluate(() => {
    window.commands = [];
    window.chrome.webview = { postMessage: value => window.commands.push(JSON.parse(value)), addEventListener() {} };
  });
  await page.setContent(html);
  await page.evaluate(() => applyState({
    processes: Array.from({ length: 80 }, (_, i) => ({ pid: 100 + i, name: 'fixture-' + i + '.exe', arch: 'x64' })),
    logs: Array.from({ length: 80 }, (_, i) => ({ time: '12:00:00', level: 'INFO', msg: 'fixture log ' + i })),
  }));
});
afterEach(async () => { await page?.close(); });

test('workbench keeps both columns visible and the process list scrolls independently', async () => {
  const metrics = await page.locator('.workbench-grid').evaluate(grid => ({
    children: [...grid.children].map(child => child.getBoundingClientRect().toJSON()),
    list: { height: workbenchProcList.clientHeight, content: workbenchProcList.scrollHeight },
  }));
  assert.ok(metrics.children[1].left >= metrics.children[0].right);
  assert.equal(metrics.children[0].top, metrics.children[1].top);
  assert.ok(metrics.list.height <= 420 && metrics.list.content > metrics.list.height);
  assert.ok((await page.locator('#dllPayloadBox').boundingBox()).y < 900);
});

test('statistics retain four columns and use two columns in a narrow window', async () => {
  await page.evaluate(() => switchView('processes'));
  const positions = () => page.locator('#view-processes .stat').evaluateAll(items => items.map(item => item.getBoundingClientRect().toJSON()));
  let boxes = await positions();
  assert.equal(boxes[0].top, boxes[3].top);
  await page.setViewportSize({ width: 960, height: 720 });
  boxes = await positions();
  assert.equal(boxes[0].top, boxes[1].top);
  assert.ok(boxes[2].top > boxes[0].top);
  assert.ok((await page.locator('.header-actions').boundingBox()).x >= 0);
  assert.ok(boxes.every(box => box.right <= 960));
});

test('long selected process names stay inside the header without covering controls', async () => {
  await page.evaluate(() => applyState({ selectedPid: 100, processes: [{ pid: 100, name: 'long-process-name-'.repeat(25) + '.exe', arch: 'x64' }] }));
  const overlaps = await page.evaluate(() => {
    const target = chipTargetProc.getBoundingClientRect(), actions = document.querySelector('.header-actions').getBoundingClientRect();
    return { targetRight: target.right, actionsLeft: actions.left, actionsRight: actions.right, width: innerWidth };
  });
  assert.ok(overlaps.targetRight <= overlaps.actionsLeft);
  assert.ok(overlaps.actionsRight <= overlaps.width);
});

test('Doctor selection survives native state updates and returning to workbench restores payload selection', async () => {
  await page.locator('#modeBtnDoctor').click();
  await page.evaluate(() => applyState({ payloadMode: 'dll' }));
  assert.equal(await page.locator('#modeBtnDoctor').getAttribute('aria-pressed'), 'true');
  assert.equal(await page.locator('#modeBtnDll').getAttribute('aria-pressed'), 'false');
  await page.evaluate(() => switchView('workbench'));
  assert.equal(await page.locator('#modeBtnDll').getAttribute('aria-pressed'), 'true');
});

test('ARM64 and unknown architecture never become x86-compatible by boolean comparison', async () => {
  await page.evaluate(() => applyState({
    processes: [{ pid: 1, name: 'arm.exe', arch: 'ARM64' }, { pid: 2, name: 'unknown.exe', arch: '受限' }, { pid: 3, name: 'x86.exe', arch: 'x86' }],
    preflight: { ...state.preflight, valid: true, dllArch: 'x86 (I386)' },
  }));
  const rows = await page.locator('#workbenchProcList .proc-row').allTextContents();
  assert.ok(rows[0].includes('ARM64') && rows[0].includes('冲突') && !rows[0].includes('兼容'));
  assert.ok(!rows[1].includes('兼容'));
  assert.ok(rows[2].includes('兼容'));
});

test('dependency colors reflect the exact Core status instead of substring matches', async () => {
  await page.evaluate(() => applyState({ preflight: { ...state.preflight, valid: true, dependencies:
    ['candidate_found', 'api_set_unresolved', 'not_found_in_search_dirs', 'inconclusive'].map(status => ({ name: status + '.dll', status })) } }));
  const classes = await page.locator('#depTableBody tr td:last-child .chip').evaluateAll(items => items.map(item => item.className));
  assert.deepEqual(classes, ['chip success', 'chip warning', 'chip warning', 'chip warning']);
});

test('reading earlier logs survives state updates and view changes while bottom-follow still works', async () => {
  await page.evaluate(() => { switchView('logs'); fullLogScroll.scrollTop = 120; });
  await page.evaluate(() => applyState({ logs: [...state.logs, { time: '12:01:00', level: 'INFO', msg: 'new log' }] }));
  assert.equal(await page.locator('#fullLogScroll').evaluate(e => e.scrollTop), 120);
  await page.evaluate(() => { switchView('doctor'); applyState({}); switchView('logs'); });
  assert.equal(await page.locator('#fullLogScroll').evaluate(e => e.scrollTop), 120);
  await page.evaluate(() => { fullLogScroll.scrollTop = fullLogScroll.scrollHeight; applyState({ logs: [...state.logs, { time: '12:02:00', level: 'INFO', msg: 'next log' }] }); });
  assert.ok(await page.locator('#fullLogScroll').evaluate(e => e.scrollHeight - e.clientHeight - e.scrollTop < 5));
  await page.locator('#logSearchInput').fill('fixture');
  assert.equal(await page.locator('#fullLogScroll').evaluate(e => e.scrollTop), 0);
});

test('Ctrl+F focuses the visible search input in each searchable view', async () => {
  for (const [view, expected] of [['workbench', 'procSearchInput'], ['processes', 'fullProcSearch'], ['logs', 'logSearchInput']]) {
    await page.evaluate(view => switchView(view), view);
    await page.keyboard.press('Control+f');
    assert.equal(await page.evaluate(() => document.activeElement.id), expected);
  }
});

test('Escape closes a dialog and collapsing the sidebar updates its accessible label', async () => {
  await page.evaluate(() => openSettingsModal());
  await page.keyboard.press('Escape');
  assert.equal(await page.locator('#modalBackdrop').evaluate(e => e.classList.contains('open')), false);
  await page.locator('#sidebarToggleBtn').click();
  assert.equal(await page.locator('#sidebarToggleBtn').getAttribute('aria-label'), '展开侧栏');
});

test('an unavailable method stays disabled in its card and information dialog', async () => {
  const card = page.locator('#methodCardsContainer .pcard').last();
  assert.ok((await card.getAttribute('class')).includes('disabled'));
  assert.equal(await card.locator('button[aria-label="切换"]').isDisabled(), true);
  await page.evaluate(() => showMethodInfo(5));
  assert.equal(await page.locator('#modalFooter .solid').isDisabled(), true);
  assert.equal(await page.evaluate(() => window.commands.some(command => command.cmd === 'select_method' || command.cmd === 'inject')), false);
});

test('window controls remain visible at the minimum window size in both themes', async () => {
  await page.setViewportSize({ width: 880, height: 600 });
  for (const theme of ['light', 'dark']) {
    await page.evaluate(theme => { setTheme(theme); applyState({ selectedPid: 100 }); }, theme);
    for (const selector of ['#btnExecuteMain', '.win-btn.close', '#btnWinMax']) {
      const box = await page.locator(selector).boundingBox();
      assert.ok(box && box.x >= 0 && box.x + box.width <= 880 && box.y + box.height <= 600);
    }
  }
});

test('maximize state updates the icon, tooltip, and accessible action name', async () => {
  for (const maximized of [true, false]) {
    await page.evaluate(isMaximized => applyState({ isMaximized }), maximized);
    assert.equal(await page.locator('#btnWinMax').getAttribute('aria-label'), maximized ? '向下还原' : '最大化');
    assert.equal(await page.locator('#btnWinMax').getAttribute('data-tip'), maximized ? '向下还原' : '最大化');
    assert.equal(await page.locator('#maxIconRestore').isVisible(), maximized);
    assert.equal(await page.locator('#maxIconSquare').isVisible(), !maximized);
  }
  await page.locator('#btnWinMax').click();
  assert.equal(await page.evaluate(() => commands.at(-1).cmd), 'window_max_toggle');
});

test('long modal content scrolls while its footer remains reachable', async () => {
  await page.setViewportSize({ width: 880, height: 600 });
  await page.evaluate(() => { openSettingsModal(); modalBody.innerHTML = '<p>long content</p>'.repeat(200); });
  const body = await page.locator('#modalBody').evaluate(e => ({ height: e.clientHeight, content: e.scrollHeight }));
  assert.ok(body.content > body.height);
  const footer = await page.locator('#modalFooter').boundingBox();
  assert.ok(footer.y >= 0 && footer.y + footer.height <= 600);
  await page.locator('#modalFooter button').click();
  assert.equal(await page.locator('#modalBackdrop').isVisible(), false);
});

test('collapsed navigation and edge controls show tooltips inside the viewport', async () => {
  await page.setViewportSize({ width: 880, height: 600 });
  await page.locator('#sidebarToggleBtn').click();
  for (const selector of ['.nav-item[data-view="doctor"]', '.win-btn.close']) {
    await page.locator(selector).hover();
    assert.equal(await page.locator('#globalHoverTip').getAttribute('aria-hidden'), 'false');
    const box = await page.locator('#globalHoverTip').boundingBox();
    assert.ok(box.x >= 0 && box.y >= 0 && box.x + box.width <= 880 && box.y + box.height <= 600);
  }
});

test('collapsed sidebar navigation items center their svg icons horizontally', async () => {
  await page.locator('#sidebarToggleBtn').click();
  await page.waitForTimeout(350);
  const navItem = page.locator('.sidebar .nav-item[data-view="workbench"]');
  const svg = navItem.locator('svg.i');
  const itemBox = await navItem.boundingBox();
  const svgBox = await svg.boundingBox();
  const itemCenter = itemBox.x + itemBox.width / 2;
  const svgCenter = svgBox.x + svgBox.width / 2;
  assert.ok(Math.abs(itemCenter - svgCenter) <= 2, `Icon should be centered: itemCenter=${itemCenter}, svgCenter=${svgCenter}`);
  assert.equal(await page.locator('.sidebar .nav-label').first().isVisible(), false);
});

test('inline process refresh and F5 shortcut dispatch refresh_processes command', async () => {
  assert.equal(await page.locator('#headerSubtitle').count(), 0);
  await page.locator('#btnRefreshProcesses').click();
  assert.equal(await page.evaluate(() => commands.at(-1)?.cmd), 'refresh_processes');
  await page.keyboard.press('F5');
  assert.equal(await page.evaluate(() => commands.at(-1)?.cmd), 'refresh_processes');
});

test('collapsed sidebar settles at 56px and method cards align text and active dot', async () => {
  await page.locator('#sidebarToggleBtn').click();
  await page.waitForTimeout(300);
  const sidebarWidth = await page.locator('.sidebar').evaluate(el => el.getBoundingClientRect().width);
  assert.equal(Math.round(sidebarWidth), 56);

  // Check method cards logo and active indicator
  const firstCard = page.locator('#methodCardsContainer .pcard').first();
  const logo = firstCard.locator('.logo');
  assert.equal(await logo.textContent(), 'CRT');
  const activeDot = firstCard.locator('.active-dot');
  assert.equal(await activeDot.count(), 1);
});
