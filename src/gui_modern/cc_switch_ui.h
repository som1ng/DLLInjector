#pragma once
#include <string>
#include <string_view>

namespace gui {

inline std::string GetCcSwitchUiHtml() {
    static constexpr std::string_view kChunk1 = R"CCS1(<!doctype html>
<html lang="zh-CN" data-theme="dark">
<head>
<meta charset="utf-8">
<title>DLL Injector</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  /* ============================================================
     CC Switch v7 Design System Tokens (1:1 from farion1231/cc-switch)
     ============================================================ */
  :root {
    --action-bg: #f97316;
    --action-hover: #ea620a;
    --action-fg: #ffffff;
    --action-text: #c2410c;
    --action-soft: #fff1e6;
    --ring: #f97316;

    --bg-app: #ffffff;
    --bg-sidebar: #f5f5f7;
    --bg-subtle: #efeff2;
    --bg-selected: #e4e4ea;
    --bg-card: #ffffff;
    --border: #e4e4e9;
    --border-strong: #cfcfd6;
    --text-1: #18181b;
    --text-2: #52525b;
    --text-3: #65656e;
    --inverse-bg: #18181b;
    --inverse-fg: #ffffff;
    --overlay: rgba(9, 9, 11, .36);
    --control-off: #888890;

    --direct-fill: #0a84ff; --direct-text: #0063c7; --direct-soft: #eaf3ff; --direct-solid: #0066cc; --direct-on: #ffffff;
    --route-fill: #0d9488; --route-text: #06726b; --route-soft: #e7f6f4; --route-solid: #0f766e; --route-on: #ffffff;
    --stack-fill: #7c3aed; --stack-text: #6d28d9; --stack-soft: #f2ecfe; --stack-solid: #6d28d9; --stack-on: #ffffff;

    --success: #16a34a; --success-text: #007634; --success-soft: #e8f7ee;
    --warning: #d97706; --warning-text: #a64a00; --warning-soft: #fef3c7;
    --danger: #dc2626;  --danger-text: #b91c1c;  --danger-soft: #fee2e2;

    --shadow-sm: 0 1px 2px rgba(16, 24, 40, .06);
    --shadow-md: 0 4px 12px rgba(16, 24, 40, .08), 0 1px 3px rgba(16, 24, 40, .06);
    --shadow-lg: 0 20px 40px rgba(16, 24, 40, .16), 0 4px 12px rgba(16, 24, 40, .08);

    --r-control: 6px;
    --r-panel: 10px;
    --r-dialog: 14px;

    --font-sans: -apple-system, BlinkMacSystemFont, "SF Pro Text", "PingFang SC", "Hiragino Sans GB",
      "Segoe UI Variable Text", "Segoe UI", "Microsoft YaHei UI", "Microsoft YaHei", system-ui, sans-serif;
    --font-mono: ui-monospace, SFMono-Regular, "SF Mono", Consolas, "Liberation Mono", Menlo, monospace;

    --dur: 150ms;
    --ease: cubic-bezier(.4, 0, .2, 1);
  }

  :root[data-theme="dark"] {
    --action-bg: #ea580c; --action-hover: #f06d1f; --action-fg: #ffffff; --action-text: #fb923c;
    --action-soft: rgba(249, 115, 21, 0.12); --ring: #d77a3c;
    --bg-app: #1c1c1e; --bg-sidebar: #161618; --bg-subtle: #26262a; --bg-selected: #313136; --bg-card: #2e2e33;
    --border: #3e3e45; --border-strong: #4a4a52;
    --text-1: #e4e4e7; --text-2: #ababb3; --text-3: #9a9aa2;
    --inverse-bg: #f4f4f5; --inverse-fg: #18181b; --overlay: rgba(0, 0, 0, .55); --control-off: #6f6f77;
    --direct-fill: #4a8fd9; --direct-text: #8fb8e6; --direct-soft: rgba(74, 143, 217, 0.12); --direct-solid: #2f6db3; --direct-on: #ffffff;
    --route-fill: #2a9d8f; --route-text: #7cc4ba; --route-soft: rgba(42, 157, 143, 0.12); --route-solid: #1f7d72; --route-on: #ffffff;
    --stack-fill: #8b74d6; --stack-text: #b5a6e3; --stack-soft: rgba(139, 116, 214, 0.12); --stack-solid: #6a52bf; --stack-on: #ffffff;
    --success: #4ade80; --success-text: #4ade80; --success-soft: #0f2b1a;
    --warning: #fbbf24; --warning-text: #fcd34d; --warning-soft: #33260a;
    --danger: #f87171;  --danger-text: #fca5a5;  --danger-soft: #3a1414;
    --shadow-sm: 0 0 0 1px rgba(255, 255, 255, .08), 0 1px 2px rgba(0, 0, 0, .4);
    --shadow-md: 0 4px 12px rgba(0, 0, 0, .45), 0 1px 3px rgba(0, 0, 0, .4);
    --shadow-lg: 0 20px 40px rgba(0, 0, 0, .5), 0 4px 12px rgba(0, 0, 0, .4);
  }

  .theme-switching *, .theme-switching *::before, .theme-switching *::after {
    transition: none !important;
  }

  * { box-sizing: border-box; user-select: none; }
  input, textarea, code, .selectable { user-select: text; }
  html { font-family: var(--font-sans); -webkit-font-smoothing: antialiased; overscroll-behavior: none; }
  body { margin: 0; background: var(--bg-app); color: var(--text-1); font-size: 13px; line-height: 20px; height: 100vh; overflow: hidden; }
  h1, h2, h3, h4, p { margin: 0; }
  code, .mono { font-family: var(--font-mono); font-size: 12px; }
  code { background: var(--bg-subtle); border-radius: 4px; padding: 1px 5px; color: var(--text-1); }

  ::-webkit-scrollbar { width: 8px; height: 8px; }
  ::-webkit-scrollbar-track, ::-webkit-scrollbar-corner { background: transparent; }
  ::-webkit-scrollbar-thumb { border: 2px solid transparent; border-radius: 9999px; background-clip: padding-box; background-color: var(--border-strong); }
  ::-webkit-scrollbar-thumb:hover { background-color: var(--text-3); }

  svg.i { width: 16px; height: 16px; fill: none; stroke: currentColor; stroke-width: 1.75; stroke-linecap: round; stroke-linejoin: round; flex-shrink: 0; }
  svg.i.sm { width: 14px; height: 14px; }
  svg.i.lg { width: 18px; height: 18px; }

  /* Shell Layout: Sidebar (200px / 68px) + Main Content */
  .app-shell { display: flex; height: 100vh; width: 100vw; overflow: hidden; }
  .sidebar {
    width: 200px; flex-shrink: 0; background: var(--bg-sidebar); border-right: 1px solid var(--border);
    display: flex; flex-direction: column; overflow: hidden; white-space: nowrap;
  }
  .sidebar.collapsed { width: 68px; }
  .sidebar-top {
    height: 44px; flex-shrink: 0; display: flex; align-items: center; justify-content: space-between;
    padding: 0 8px 0 12px;
  }
  .sidebar.collapsed .sidebar-top { justify-content: center; padding: 0 8px; }
  .brand { display: flex; align-items: center; gap: 8px; font-size: 14px; font-weight: 600; color: var(--text-1); min-width: 0; }
  .sidebar.collapsed .brand { display: none; }
  .brand-logo {
    width: 22px; height: 22px; border-radius: 6px; background: var(--action-bg); color: var(--action-fg);
    display: grid; place-items: center; flex-shrink: 0;
  }
  .sidebar-scroll { flex: 1; min-height: 0; overflow-y: auto; overflow-x: hidden; padding: 2px 0 8px; display: flex; flex-direction: column; gap: 2px; }
  .sidebar-group-label { padding: 10px 16px 4px; font-size: 11px; font-weight: 500; color: var(--text-3); }
  .sidebar.collapsed .sidebar-group-label { display: none; }
  .sidebar-divider { height: 1px; background: var(--border); margin: 6px 16px; flex-shrink: 0; }
  .sidebar.collapsed .sidebar-divider { margin: 6px 14px; }

  .nav-item {
    margin: 0 8px; height: 28px; padding: 0 8px; border-radius: var(--r-control); border: 0;
    background: transparent; color: var(--text-2); font: 400 13px/20px var(--font-sans);
    display: flex; align-items: center; gap: 8px; cursor: pointer; text-align: left;
    transition: background-color var(--dur), color var(--dur);
  }
  .nav-item:hover { background: var(--bg-subtle); color: var(--text-1); }
  .nav-item.active { background: var(--bg-selected); color: var(--text-1); font-weight: 500; }
  .nav-item .nav-label { flex: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis; }
  .nav-item .nav-trail { font-size: 11px; font-weight: 500; font-variant-numeric: tabular-nums; color: var(--text-3); }
  .nav-item .nav-tag {
    height: 18px; line-height: 18px; padding: 0 6px; border-radius: 9999px; font-size: 11px; font-weight: 500;
  }
  .nav-item .nav-dot { width: 6px; height: 6px; border-radius: 50%; flex-shrink: 0; }
  .sidebar.collapsed .nav-item { width: 48px; height: 32px; margin: 0 auto; justify-content: center; padding: 0; }
  .sidebar.collapsed .nav-item .nav-label,
  .sidebar.collapsed .nav-item .nav-trail,
  .sidebar.collapsed .nav-item .nav-tag { display: none; }

  .sidebar-bottom { display: flex; gap: 4px; padding: 0 8px 8px; flex-shrink: 0; }
  .sidebar.collapsed .sidebar-bottom { flex-direction: column; padding: 0 0 8px; }
  .sidebar-bottom .nav-item { margin: 0; flex: 1; min-width: 0; }
  .sidebar.collapsed .sidebar-bottom .nav-item { margin: 0 auto; flex: none; }
)CCS1";

    static constexpr std::string_view kChunk2 = R"CCS2(
  /* Main Content Area & AppPageHeader (52px) */
  .main-pane { flex: 1; min-width: 0; display: flex; flex-direction: column; height: 100vh; background: var(--bg-app); }
  .page-header {
    height: 52px; flex-shrink: 0; display: flex; align-items: center; gap: 16px;
    padding: 0 18px 0 24px; border-bottom: 1px solid var(--border); background: var(--bg-app);
  }
  .header-left { display: flex; align-items: center; gap: 10px; min-width: 0; flex: 1; }
  .header-glyph {
    width: 24px; height: 24px; border-radius: 6px; background: var(--bg-subtle); color: var(--text-1);
    display: grid; place-items: center; flex-shrink: 0; border: 1px solid var(--border);
  }
  .header-title { font-size: 16px; line-height: 24px; font-weight: 600; color: var(--text-1); white-space: nowrap; }
  .header-sub { font-size: 13px; color: var(--text-3); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .header-actions { display: flex; align-items: center; gap: 8px; flex-shrink: 0; }

  /* Buttons (v7 button.tsx) */
  .btn {
    display: inline-flex; align-items: center; justify-content: center; gap: 6px; white-space: nowrap;
    height: 32px; padding: 0 14px; border-radius: var(--r-control); border: 1px solid transparent;
    font: 500 13px/20px var(--font-sans); cursor: pointer; background: transparent; color: var(--text-1);
    transition: background-color var(--dur), filter var(--dur), color var(--dur), transform var(--dur), border-color var(--dur);
  }
  .btn:active:not(:disabled) { transform: scale(.96); }
  .btn.compact { height: 28px; padding: 0 11px; font-size: 12px; }
  .btn.solid { background: var(--action-bg); color: var(--action-fg); font-weight: 600; }
  .btn.solid:hover:not(:disabled) { background: var(--action-hover); }
  .btn.neutral { border-color: var(--border-strong); background: var(--bg-card); color: var(--text-1); }
  .btn.neutral:hover:not(:disabled) { background: var(--bg-subtle); }
  .btn.quiet { color: var(--text-2); }
  .btn.quiet:hover:not(:disabled) { background: var(--bg-subtle); color: var(--text-1); }
  .btn.danger { background: var(--danger); color: #fff; font-weight: 600; }
  .btn.danger:hover:not(:disabled) { filter: brightness(1.08); }
  .btn.direct { background: var(--direct-solid); color: var(--direct-on); font-weight: 600; }
  .btn.route { background: var(--route-solid); color: var(--route-on); font-weight: 600; }
  .btn.stack { background: var(--stack-solid); color: var(--stack-on); font-weight: 600; }
  .btn.icon { width: 28px; height: 28px; padding: 0; color: var(--text-2); }
  .btn.icon:hover:not(:disabled) { background: var(--bg-subtle); color: var(--text-1); }
  .btn.icon.lg { width: 32px; height: 32px; }
  .btn:disabled { opacity: .45; cursor: not-allowed; }

  /* HoverTip (zero-delay inverse capsule) */
  [data-tip] { position: relative; }
  [data-tip]::after {
    content: attr(data-tip); position: absolute; left: 50%; top: calc(100% + 6px); transform: translateX(-50%);
    z-index: 130; pointer-events: none; white-space: nowrap;
    padding: 5px 10px; border-radius: 9999px; background: var(--inverse-bg); color: var(--inverse-fg);
    font: 500 11.5px/16px var(--font-sans); box-shadow: var(--shadow-md);
    opacity: 0; transition: opacity 80ms;
  }
  [data-tip]:hover::after { opacity: 1; }
  [data-tip-pos="top"]::after { top: auto; bottom: calc(100% + 6px); }
  [data-tip-pos="right"]::after { left: calc(100% + 8px); top: 50%; transform: translateY(-50%); }

  /* Chips / Badges (v7 badge.tsx) */
  .chip {
    display: inline-flex; align-items: center; gap: 5px; height: 20px; padding: 0 8px;
    border-radius: 9999px; font-size: 11px; line-height: 16px; font-weight: 500; white-space: nowrap;
  }
  .chip.outline { border: 1px solid var(--border-strong); color: var(--text-2); background: transparent; }
  .chip.direct { background: var(--direct-soft); color: var(--direct-text); }
  .chip.route { background: var(--route-soft); color: var(--route-text); }
  .chip.stack { background: var(--stack-soft); color: var(--stack-text); }
  .chip.success { background: var(--success-soft); color: var(--success-text); }
  .chip.warning { background: var(--warning-soft); color: var(--warning-text); }
  .chip.danger { background: var(--danger-soft); color: var(--danger-text); }
  .chip.action { background: var(--action-soft); color: var(--action-text); }
  .chip .d { width: 6px; height: 6px; border-radius: 50%; background: currentColor; }

  /* Segmented Control (v7 segmented-control.tsx with sliding thumb) */
  .seg {
    position: relative; display: inline-flex; align-items: stretch; height: 36px; padding: 3px; gap: 2px;
    border-radius: 10px; background: var(--bg-subtle); flex-shrink: 0;
  }
  .seg .thumb {
    position: absolute; left: 0; top: 0; border-radius: 7px; background: var(--bg-card);
    box-shadow: var(--shadow-sm); pointer-events: none; transition: transform 200ms var(--ease), width 200ms var(--ease);
  }
  .seg.sm { height: 28px; border-radius: 8px; }
  .seg.sm .thumb { border-radius: 5px; }
  .seg button {
    position: relative; z-index: 1; display: inline-flex; align-items: center; justify-content: center; gap: 6px;
    padding: 0 14px; border: 0; border-radius: 7px; background: transparent; color: var(--text-2);
    font: 500 13px var(--font-sans); cursor: pointer; transition: color var(--dur); white-space: nowrap;
  }
  .seg.sm button { padding: 0 10px; font-size: 12px; border-radius: 5px; }
  .seg button:hover { color: var(--text-1); }
  .seg button[aria-pressed="true"] { color: var(--text-1); font-weight: 600; }
  .seg.modes button { min-width: 108px; padding: 0 14px; }
  .seg.modes button .dot { width: 6px; height: 6px; border-radius: 50%; }
  .seg.modes button.act-direct { color: var(--direct-text); }
  .seg.modes button.act-direct .dot { background: var(--direct-fill); }
  .seg.modes button.act-route { color: var(--route-text); }
  .seg.modes button.act-route .dot { background: var(--route-fill); }
  .seg.modes button.act-stack { color: var(--stack-text); }
  .seg.modes button.act-stack .dot { background: var(--stack-fill); }

  /* Switch (v7 switch.tsx 30x18) */
  .switch {
    position: relative; width: 30px; height: 18px; border-radius: 9999px; border: 0; padding: 0;
    cursor: pointer; background: var(--control-off); transition: background-color var(--dur); flex-shrink: 0;
  }
  .switch i {
    position: absolute; top: 2px; left: 2px; width: 14px; height: 14px; border-radius: 50%;
    background: #fff; box-shadow: var(--shadow-sm); transition: transform var(--dur), background-color var(--dur);
  }
  .switch[aria-checked="true"] { background: var(--action-bg); }
  .switch[aria-checked="true"] i { transform: translateX(12px); background: var(--action-fg); }

  /* Inputs (v7 input.tsx) */
  .input {
    height: 32px; width: 100%; padding: 0 10px; border: 1px solid var(--border-strong); border-radius: var(--r-control);
    background: var(--bg-card); color: var(--text-1); font: 13px var(--font-sans); outline: none;
    transition: border-color var(--dur), box-shadow var(--dur);
  }
  .input::placeholder { color: var(--text-3); }
  .input:focus { border-color: var(--ring); box-shadow: 0 0 0 3px color-mix(in srgb, var(--ring) 22%, transparent); }
  .input.mono { font-family: var(--font-mono); font-size: 12px; }
  textarea.input { height: auto; padding: 8px 10px; resize: vertical; line-height: 18px; }
  .input-wrap { position: relative; display: flex; align-items: center; flex: 1; min-width: 0; }
  .input-wrap > svg.search-ic { position: absolute; left: 9px; color: var(--text-3); pointer-events: none; }
  .input-wrap > .input.with-ic { padding-left: 30px; padding-right: 28px; }
  .input-wrap > .clear-btn {
    position: absolute; right: 4px; width: 22px; height: 22px; border: 0; border-radius: 4px;
    background: transparent; color: var(--text-3); display: grid; place-items: center; cursor: pointer;
  }
  .input-wrap > .clear-btn:hover { background: var(--bg-subtle); color: var(--text-1); }
)CCS2";

    static constexpr std::string_view kChunk3 = R"CCS3(
  /* Mode Row & HelpTip */
  .view-body { flex: 1; min-height: 0; overflow-y: auto; padding: 14px 24px 28px; display: flex; flex-direction: column; gap: 12px; }
  .moderow { display: flex; align-items: center; gap: 14px; flex-wrap: wrap; }
  .moderow .status { flex: 1; min-width: 0; font-size: 12px; color: var(--text-2); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .moderow .toggle-lbl { display: inline-flex; align-items: center; gap: 6px; font-size: 12px; color: var(--text-2); cursor: pointer; }

  .helptip { position: relative; display: inline-flex; }
  .helptip > button {
    width: 20px; height: 20px; padding: 0; border: 0; border-radius: 50%; background: none;
    color: var(--text-3); display: grid; place-items: center; cursor: pointer;
  }
  .helptip > button:hover { background: var(--bg-subtle); color: var(--text-1); }
  .helptip .card {
    position: absolute; top: calc(100% + 6px); left: -8px; z-index: 120; width: 310px;
    border: 1px solid var(--border); border-radius: 8px; background: var(--bg-card); box-shadow: var(--shadow-md);
    padding: 10px 12px; font-size: 12px; line-height: 18px; color: var(--text-1);
    opacity: 0; visibility: hidden; transition: opacity 120ms; pointer-events: none;
  }
  .helptip .card b { display: block; margin-bottom: 3px; font-weight: 600; }
  .helptip:hover .card { opacity: 1; visibility: visible; }

  /* Notice Banners (v7 notice.tsx: soft bg, 16px icon, NO left border) */
  .notice-stack { display: flex; flex-direction: column; gap: 8px; }
  .notice {
    display: flex; align-items: center; gap: 10px; border-radius: var(--r-panel);
    padding: 9px 14px; font-size: 13px; min-height: 42px;
  }
  .notice .grow { flex: 1; min-width: 0; }
  .notice .t2 { margin-top: 2px; font-size: 12px; color: var(--text-2); }
  .notice.neutral { background: var(--bg-subtle); } .notice.neutral > svg { color: var(--text-2); }
  .notice.warning { background: var(--warning-soft); } .notice.warning > svg { color: var(--warning-text); }
  .notice.danger { background: var(--danger-soft); } .notice.danger > svg { color: var(--danger-text); }
  .notice.route { background: var(--route-soft); } .notice.route > svg { color: var(--route-text); }
  .notice.direct { background: var(--direct-soft); } .notice.direct > svg { color: var(--direct-text); }
  .notice.stack { background: var(--stack-soft); } .notice.stack > svg { color: var(--stack-text); }

  /* Workbench Two-Column Split */
  .workbench-grid {
    display: grid; grid-template-columns: 370px minmax(0, 1fr); gap: 14px; flex: 1; min-height: 420px;
  }
  .panel-card {
    border: 1px solid var(--border); border-radius: var(--r-panel); background: var(--bg-card);
    padding: 12px 14px; display: flex; flex-direction: column; gap: 10px; min-height: 0;
  }
  .section-head { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
  .section-title { font-size: 14px; font-weight: 600; color: var(--text-1); display: flex; align-items: center; gap: 6px; }

  /* Process List in Workbench */
  .proc-list {
    flex: 1; min-height: 180px; overflow-y: auto; border: 1px solid var(--border);
    border-radius: var(--r-control); background: var(--bg-app);
  }
  .proc-row {
    display: flex; align-items: center; gap: 8px; height: 34px; padding: 0 10px;
    border-bottom: 1px solid var(--border); cursor: pointer; font-size: 12.5px;
    transition: background-color var(--dur);
  }
  .proc-row:last-child { border-bottom: 0; }
  .proc-row:hover { background: var(--bg-subtle); }
  .proc-row.selected { background: var(--direct-soft); }
  .proc-row .pid { width: 54px; font-family: var(--font-mono); font-size: 11.5px; color: var(--text-2); font-variant-numeric: tabular-nums; flex-shrink: 0; }
  .proc-row.selected .pid { color: var(--direct-text); font-weight: 600; }
  .proc-row .pname { flex: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; font-weight: 500; color: var(--text-1); }
  .proc-row.selected .pname { color: var(--direct-text); font-weight: 600; }

  /* ProviderCard-style Method Cards (v7 ProviderCard.tsx .pcard) */
  .pcard-list { display: flex; flex-direction: column; gap: 8px; }
  .pcard {
    position: relative; display: flex; align-items: center; gap: 12px; min-height: 58px; padding: 10px 12px 10px 10px;
    border: 1px solid var(--border); border-radius: var(--r-panel); background: var(--bg-card); cursor: pointer;
    transition: border-color var(--dur), box-shadow var(--dur), background-color var(--dur);
  }
  .pcard:hover:not(.disabled) { border-color: var(--border-strong); box-shadow: var(--shadow-sm); }
  .pcard.t-direct { border-color: var(--direct-fill); background: var(--direct-soft); box-shadow: none; }
  .pcard.t-route { border-color: var(--route-fill); background: var(--route-soft); box-shadow: none; }
  .pcard.t-stack { border-color: var(--stack-fill); background: var(--stack-soft); box-shadow: none; }
  .pcard.disabled { opacity: .45; cursor: not-allowed; }
  .pcard .grip { width: 14px; color: var(--text-3); opacity: 0; transition: opacity var(--dur); display: grid; place-items: center; }
  .pcard:hover:not(.disabled) .grip { opacity: 1; }
  .pcard .logo {
    width: 32px; height: 32px; border-radius: 8px; border: 1px solid var(--border);
    background: #ffffff; color: #171717; display: grid; place-items: center;
    font-family: var(--font-mono); font-weight: 700; font-size: 11px; flex-shrink: 0;
  }
  .pcard .body { min-width: 0; flex: 1; }
  .pcard .name { display: flex; align-items: center; gap: 6px; font-weight: 500; font-size: 14px; color: var(--text-1); }
  .pcard .sub { margin-top: 2px; font-size: 12px; color: var(--text-2); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .pcard .active-indicator {
    display: inline-flex; align-items: center; gap: 6px; font-size: 12px; font-weight: 500; padding: 0 6px; white-space: nowrap;
  }
  .pcard .acts { display: flex; align-items: center; gap: 2px; opacity: 0; transition: opacity var(--dur); }
  .pcard:hover:not(.disabled) .acts { opacity: 1; }

  /* DropZone */
  .dropzone {
    border: 1.5px dashed var(--border-strong); border-radius: var(--r-panel); background: var(--bg-subtle);
    padding: 10px 14px; display: flex; align-items: center; justify-content: space-between; gap: 10px;
    font-size: 12px; color: var(--text-2); transition: border-color var(--dur), background-color var(--dur);
  }
  .dropzone.dragover { border-color: var(--action-bg); background: var(--action-soft); color: var(--action-text); }

  /* Stats & Data Table (v7 usage/ data section) */
  .stats { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 10px; }
  .stat { border: 1px solid var(--border); border-radius: var(--r-panel); padding: 12px 14px; background: var(--bg-card); }
  .stat .k { font-size: 12px; color: var(--text-2); }
  .stat .v { font-size: 24px; line-height: 32px; font-weight: 600; font-variant-numeric: tabular-nums; margin-top: 2px; color: var(--text-1); }
  .stat .d { font-size: 12px; color: var(--text-3); font-variant-numeric: tabular-nums; margin-top: 2px; }

  .ptabs { display: flex; gap: 20px; border-bottom: 1px solid var(--border); position: relative; flex-shrink: 0; }
  .ptabs button {
    height: 36px; border: 0; background: none; padding: 0; color: var(--text-2);
    font: 500 13px var(--font-sans); cursor: pointer; position: relative;
  }
  .ptabs button:hover { color: var(--text-1); }
  .ptabs button.on { color: var(--text-1); font-weight: 600; }
  .ptabs button.on::after {
    content: ""; position: absolute; left: 0; right: 0; bottom: -1px; height: 2px;
    background: var(--text-1); border-radius: 2px;
  }

  table.log { width: 100%; border-collapse: collapse; font-size: 12px; }
  table.log th { text-align: left; font-weight: 500; color: var(--text-2); padding: 8px 10px; border-bottom: 1px solid var(--border); white-space: nowrap; }
  table.log td { padding: 8px 10px; border-bottom: 1px solid var(--border); white-space: nowrap; }
  table.log tr:hover td { background: var(--bg-subtle); }
  table.log .num { text-align: right; font-variant-numeric: tabular-nums; font-family: var(--font-mono); font-size: 11.5px; }

  /* Toast & Dialog Overlays */
  #toaster { position: fixed; bottom: 18px; left: 50%; transform: translateX(-50%); z-index: 200; display: flex; flex-direction: column; gap: 8px; pointer-events: none; }
  .toast {
    pointer-events: auto; position: relative; display: flex; align-items: center; gap: 10px;
    min-width: 320px; max-width: 480px; padding: 10px 16px 10px 12px;
    border: 1px solid var(--border); border-radius: var(--r-panel); background: var(--bg-card);
    color: var(--text-1); box-shadow: var(--shadow-lg); font-size: 13px; font-weight: 500;
    animation: tin 200ms var(--ease);
  }
  @keyframes tin { from { opacity: 0; transform: translateY(8px); } }
  .toast > svg.ok { color: var(--success); }
  .toast > svg.err { color: var(--danger); }
  .toast > svg.warn { color: var(--warning); }
  .toast .grow { flex: 1; }
  .toast .tclose {
    position: absolute; top: -8px; right: -8px; width: 20px; height: 20px; border-radius: 50%;
    border: 1px solid var(--border); padding: 0; display: grid; place-items: center;
    background: var(--bg-card); color: var(--text-2); box-shadow: var(--shadow-sm); cursor: pointer;
  }
  .toast .tclose:hover { color: var(--text-1); }

  .modal-backdrop {
    position: fixed; inset: 0; background: var(--overlay); z-index: 180;
    display: none; align-items: center; justify-content: center;
  }
  .modal-backdrop.open { display: flex; }
  .dialog {
    width: min(440px, 92vw); border: 1px solid var(--border); border-radius: var(--r-dialog);
    background: var(--bg-card); box-shadow: var(--shadow-lg); padding: 20px;
  }
  .dialog h4 { font-size: 16px; line-height: 24px; font-weight: 600; color: var(--text-1); }
  .dialog p { margin-top: 6px; color: var(--text-2); font-size: 13px; line-height: 20px; }
  .dialog .ft { display: flex; justify-content: flex-end; gap: 8px; margin-top: 20px; }
</style>
</head>
<body>
)CCS3";

    static constexpr std::string_view kChunk4 = R"CCS4(
<!-- Lucide SVG Icons (1:1 from cc-switch) -->
<svg width="0" height="0" style="position:absolute" aria-hidden="true">
  <symbol id="plug" viewBox="0 0 24 24"><path d="M12 22v-5M9 8V2M15 8V2M18 8v5a4 4 0 0 1-4 4h-4a4 4 0 0 1-4-4V8Z"/></symbol>
  <symbol id="route" viewBox="0 0 24 24"><circle cx="6" cy="19" r="3"/><path d="M9 19h8.5a3.5 3.5 0 0 0 0-7h-11a3.5 3.5 0 0 1 0-7H15"/><circle cx="18" cy="5" r="3"/></symbol>
  <symbol id="layers" viewBox="0 0 24 24"><path d="M12.83 2.18a2 2 0 0 0-1.66 0L2.6 6.08a1 1 0 0 0 0 1.83l8.58 3.91a2 2 0 0 0 1.66 0l8.58-3.9a1 1 0 0 0 0-1.83Z"/><path d="m22 17.65-9.17 4.16a2 2 0 0 1-1.66 0L2 17.65"/><path d="m22 12.65-9.17 4.16a2 2 0 0 1-1.66 0L2 12.65"/></symbol>
  <symbol id="shuffle" viewBox="0 0 24 24"><path d="m18 14 4 4-4 4M18 2l4 4-4 4M2 18h1.4c1.3 0 2.5-.6 3.3-1.7l6.1-8.6c.7-1.1 2-1.7 3.3-1.7H22M2 6h1.9c1.5 0 2.9.9 3.6 2.2M22 18h-5.9c-1.3 0-2.6-.7-3.3-1.8l-.5-.8"/></symbol>
  <symbol id="settings" viewBox="0 0 24 24"><path d="M20 7h-9M14 17H5"/><circle cx="17" cy="17" r="3"/><circle cx="7" cy="7" r="3"/></symbol>
  <symbol id="plus" viewBox="0 0 24 24"><path d="M5 12h14M12 5v14"/></symbol>
  <symbol id="copy" viewBox="0 0 24 24"><rect x="8" y="8" width="14" height="14" rx="2"/><path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2"/></symbol>
  <symbol id="trash" viewBox="0 0 24 24"><path d="M3 6h18M19 6v14c0 1-1 2-2 2H7c-1 0-2-1-2-2V6M8 6V4c0-1 1-2 2-2h4c1 0 2 1 2 2"/></symbol>
  <symbol id="info" viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><path d="M12 16v-4M12 8h.01"/></symbol>
  <symbol id="alert" viewBox="0 0 24 24"><path d="m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3M12 9v4M12 17h.01"/></symbol>
  <symbol id="circle-alert" viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><path d="M12 8v4M12 16h.01"/></symbol>
  <symbol id="help" viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><path d="M9.09 9a3 3 0 0 1 5.83 1c0 2-3 3-3 3M12 17h.01"/></symbol>
  <symbol id="check" viewBox="0 0 24 24"><path d="M20 6 9 17l-5-5"/></symbol>
  <symbol id="check-circle" viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><path d="m9 12 2 2 4-4"/></symbol>
  <symbol id="x" viewBox="0 0 24 24"><path d="M18 6 6 18M6 6l12 12"/></symbol>
  <symbol id="search" viewBox="0 0 24 24"><circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/></symbol>
  <symbol id="grip" viewBox="0 0 24 24"><circle cx="9" cy="12" r="1"/><circle cx="9" cy="5" r="1"/><circle cx="9" cy="19" r="1"/><circle cx="15" cy="12" r="1"/><circle cx="15" cy="5" r="1"/><circle cx="15" cy="19" r="1"/></symbol>
  <symbol id="refresh" viewBox="0 0 24 24"><path d="M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8M21 3v5h-5M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16M8 16H3v5"/></symbol>
  <symbol id="play" viewBox="0 0 24 24"><path d="M6 3l14 9-14 9V3z"/></symbol>
  <symbol id="folder" viewBox="0 0 24 24"><path d="M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2Z"/></symbol>
  <symbol id="shield" viewBox="0 0 24 24"><path d="M20 13c0 5-3.5 7.5-7.66 8.95a1 1 0 0 1-.67-.01C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 6.24-2.72a1.17 1.17 0 0 1 1.52 0C14.51 3.81 17 5 19 5a1 1 0 0 1 1 1z"/></symbol>
  <symbol id="terminal" viewBox="0 0 24 24"><polyline points="4 17 10 11 4 5"/><line x1="12" x2="20" y1="19" y2="19"/></symbol>
  <symbol id="cpu" viewBox="0 0 24 24"><rect width="16" height="16" x="4" y="4" rx="2"/><rect width="6" height="6" x="9" y="9" rx="1"/><path d="M15 2v2M15 20v2M2 15h2M2 9h2M20 15h2M20 9h2M9 2v2M9 20v2"/></symbol>
  <symbol id="chev-left" viewBox="0 0 24 24"><path d="m11 17-5-5 5-5M18 17l-5-5 5-5"/></symbol>
  <symbol id="chev-right" viewBox="0 0 24 24"><path d="m6 17 5-5-5-5M13 17l5-5-5-5"/></symbol>
  <symbol id="download" viewBox="0 0 24 24"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" x2="12" y1="15" y2="3"/></symbol>
</svg>

<div class="app-shell">
  <!-- Left Sidebar (cc-switch Sidebar.tsx) -->
  <nav class="sidebar" id="sidebar" aria-label="主导航">
    <div class="sidebar-top">
      <div class="brand">
        <span class="brand-logo"><svg class="i sm"><use href="#shuffle"/></svg></span>
        <span style="overflow:hidden;text-overflow:ellipsis">DLL Injector</span>
      </div>
      <button class="btn icon" id="sidebarToggleBtn" onclick="toggleSidebar()" data-tip="收起侧栏" data-tip-pos="right" aria-label="收起侧栏">
        <svg class="i" id="sidebarToggleIcon"><use href="#chev-left"/></svg>
      </button>
    </div>

    <div class="sidebar-scroll">
      <div class="sidebar-group-label">工作区</div>
      <button class="nav-item active" data-view="workbench" onclick="switchView('workbench')">
        <svg class="i lg"><use href="#plug"/></svg>
        <span class="nav-label">注入工作台</span>
        <span class="nav-tag chip direct" id="navModeTag">DLL</span>
      </button>
      <button class="nav-item" data-view="processes" onclick="switchView('processes')">
        <svg class="i lg"><use href="#cpu"/></svg>
        <span class="nav-label">目标进程</span>
        <span class="nav-trail" id="navProcCount">0</span>
      </button>
      <button class="nav-item" data-view="doctor" onclick="switchView('doctor')">
        <svg class="i lg"><use href="#route"/></svg>
        <span class="nav-label">DLL Doctor</span>
        <span class="nav-trail" id="navFindingCount">—</span>
      </button>

      <div class="sidebar-divider"></div>
      <div class="sidebar-group-label">诊断与审计</div>
      <button class="nav-item" data-view="dependencies" onclick="switchView('dependencies')">
        <svg class="i lg"><use href="#layers"/></svg>
        <span class="nav-label">依赖项表</span>
        <span class="nav-trail" id="navDepCount">0</span>
      </button>
      <button class="nav-item" data-view="logs" onclick="switchView('logs')">
        <svg class="i lg"><use href="#terminal"/></svg>
        <span class="nav-label">终端日志</span>
        <span class="nav-trail" id="navLogCount">0</span>
      </button>
    </div>

    <div class="sidebar-divider"></div>
    <div class="sidebar-bottom">
      <button class="nav-item" onclick="sendCmd({cmd:'enable_debug_priv'})" data-tip="申请 SeDebugPrivilege 调试权限" data-tip-pos="top">
        <svg class="i lg"><use href="#shield"/></svg>
        <span class="nav-label" id="navPrivText">提权</span>
      </button>
      <button class="nav-item" onclick="openSettingsModal()" data-tip="界面主题与运行设置" data-tip-pos="top">
        <svg class="i lg"><use href="#settings"/></svg>
        <span class="nav-label">设置</span>
      </button>
    </div>
  </nav>

  <!-- Right Main Content Area -->
  <div class="main-pane">
    <!-- Top AppPageHeader (52px) -->
    <header class="page-header">
      <div class="header-left">
        <span class="header-glyph"><svg class="i"><use href="#plug" id="headerGlyphUse"/></svg></span>
        <h1 class="header-title" id="headerTitle">注入工作台</h1>
        <span class="header-sub" id="headerSubtitle">直连注入 · 静态预检与 6 种注入机制</span>
        <span class="chip outline" id="chipHostArch">宿主 x64</span>
        <span class="chip warning" id="chipPriv" style="cursor:pointer" onclick="sendCmd({cmd:'enable_debug_priv'})">
          <span class="d"></span><span id="chipPrivText">SeDebug 未提权</span>
        </span>
        <span class="chip direct" id="chipTargetProc" style="display:none"></span>
      </div>
      <div class="header-actions">
        <button class="btn icon" onclick="sendCmd({cmd:'refresh_processes'})" data-tip="刷新活跃进程列表" aria-label="刷新">
          <svg class="i"><use href="#refresh"/></svg>
        </button>
        <button class="btn icon" onclick="sendCmd({cmd:'copy_doctor_json'})" data-tip="复制 Core Doctor 诊断 JSON" aria-label="复制诊断">
          <svg class="i"><use href="#copy"/></svg>
        </button>
        <div class="seg sm" id="themeSeg" role="group" aria-label="主题">
          <span class="thumb" id="themeThumb"></span>
          <button type="button" id="btnThemeLight" aria-pressed="false" onclick="setTheme('light')">浅色</button>
          <button type="button" id="btnThemeDark" aria-pressed="true" onclick="setTheme('dark')">深色</button>
        </div>
        <!-- Unique Solid Orange Action Button in View -->
        <button class="btn solid" id="btnExecuteMain" onclick="triggerInject()">
          <svg class="i"><use href="#play"/></svg>
          <span id="btnExecuteText">执行注入</span>
        </button>
      </div>
    </header>
)CCS4";

    static constexpr std::string_view kChunk5 = R"CCS5(
    <!-- View 1: Workbench -->
    <main class="view-body" id="view-workbench">
      <!-- Mode Row (cc-switch SwitchModePanel style) -->
      <div class="moderow">
        <div class="seg modes" id="modeSeg" role="group" aria-label="载荷与工作模式">
          <span class="thumb" id="modeThumb"></span>
          <button type="button" id="modeBtnDll" aria-pressed="true" class="act-direct" onclick="setPayloadMode('dll')">
            <svg class="i sm"><use href="#plug"/></svg>DLL 直连注入<span class="dot"></span>
          </button>
          <button type="button" id="modeBtnDoctor" aria-pressed="false" onclick="switchView('doctor')">
            <svg class="i sm"><use href="#route"/></svg>Doctor 预检
          </button>
          <button type="button" id="modeBtnShell" aria-pressed="false" onclick="setPayloadMode('shellcode')">
            <svg class="i sm"><use href="#layers"/></svg>Shellcode 载荷
          </button>
        </div>
        <span class="helptip">
          <button type="button" aria-label="三种模式的区别"><svg class="i sm"><use href="#help"/></svg></button>
          <span class="card" role="tooltip">
            <b>三种模式的区别</b>
            直连 DLL：解析 PE 架构与依赖后注入动态库。<br>
            Doctor 预检：零权限静态分析 PE 头、导入表与缺失依赖。<br>
            Shellcode 载荷：直接向目标进程写入并执行原始机器码。
          </span>
        </span>
        <span class="status" id="modeStatusText">当前模式：DLL 动态库注入 · 请选择目标进程与载荷文件</span>
        <label class="toggle-lbl" onclick="toggleGuardSwitch()">
          <span>预检拦截保护</span>
          <button type="button" class="switch" id="guardSwitch" role="switch" aria-checked="true" aria-label="预检拦截保护"><i></i></button>
        </label>
      </div>

      <!-- Contextual Notice Slot -->
      <div class="notice-stack" id="noticeStack"></div>

      <!-- Two-Column Workbench -->
      <div class="workbench-grid">
        <!-- Left Column: Process Picker + Payload Card -->
        <div style="display:flex;flex-direction:column;gap:12px;min-height:0">
          <!-- Process Selector Card -->
          <div class="panel-card" style="flex:1;min-height:240px">
            <div class="section-head">
              <div class="section-title">
                <span>目标进程</span>
                <span class="chip outline" id="procTotalChip">0 个</span>
              </div>
              <div class="seg sm" id="archSeg" role="group" aria-label="架构筛选">
                <span class="thumb" id="archThumb"></span>
                <button type="button" data-arch="all" aria-pressed="true" onclick="setArchFilter('all')">全部</button>
                <button type="button" data-arch="x64" aria-pressed="false" onclick="setArchFilter('x64')">x64</button>
                <button type="button" data-arch="x86" aria-pressed="false" onclick="setArchFilter('x86')">x86</button>
              </div>
            </div>
            <div class="input-wrap" style="flex:none">
              <svg class="i sm search-ic"><use href="#search"/></svg>
              <input class="input with-ic" id="procSearchInput" placeholder="搜索进程名称或 PID (Ctrl+F)..." oninput="onProcSearch(this.value)">
              <button class="clear-btn" id="procSearchClear" style="display:none" onclick="onProcSearch('')" aria-label="清空">
                <svg class="i sm"><use href="#x"/></svg>
              </button>
            </div>
            <div class="proc-list" id="workbenchProcList"></div>
          </div>

          <!-- Payload Card -->
          <div class="panel-card" style="flex:none">
            <div id="dllPayloadBox">
              <div class="section-head" style="margin-bottom:8px">
                <div class="section-title">
                  <span>DLL 载荷文件</span>
                  <span class="chip direct" id="dllArchChip">未载入</span>
                </div>
                <div style="display:flex;gap:4px">
                  <button class="btn icon" onclick="sendCmd({cmd:'export_doctor_report'})" data-tip="导出诊断报告 (.json/.txt)" aria-label="导出报告">
                    <svg class="i sm"><use href="#download"/></svg>
                  </button>
                </div>
              </div>
              <div style="display:flex;gap:8px;margin-bottom:8px">
                <input class="input mono" id="dllPathInput" placeholder="输入或浏览选择 .dll 绝对路径..." onchange="sendCmd({cmd:'set_dll_path',path:this.value})">
                <button class="btn neutral" onclick="sendCmd({cmd:'browse_dll'})"><svg class="i sm"><use href="#folder"/></svg>浏览...</button>
              </div>
              <div class="dropzone" id="dropZone">
                <span>支持将 <code>.dll</code> 或 <code>.bin</code> 文件直接拖入窗口完成加载与静态预检</span>
                <span class="chip outline">Drag &amp; Drop</span>
              </div>
            </div>

            <div id="shellcodePayloadBox" style="display:none">
              <div class="section-head" style="margin-bottom:8px">
                <div class="section-title">
                  <span>Shellcode 机器码载荷</span>
                  <span class="chip stack" id="shellByteChip">0 字节</span>
                </div>
              </div>
              <div style="display:flex;gap:8px;margin-bottom:8px">
                <input class="input mono" id="binPathInput" placeholder="载入 .bin 二进制载荷文件 (可选)..." readonly>
                <button class="btn neutral" onclick="sendCmd({cmd:'browse_bin'})"><svg class="i sm"><use href="#folder"/></svg>浏览 .bin</button>
              </div>
              <textarea class="input mono" id="hexTextarea" rows="3" placeholder="输入十六进制字节序列 (如: 90 90 CC 48 31 C0)..." oninput="sendCmd({cmd:'set_hex',hex:this.value})"></textarea>
            </div>
          </div>
        </div>

        <!-- Right Column: ProviderCard-style Injection Methods + Quick Console -->
        <div style="display:flex;flex-direction:column;gap:12px;min-height:0">
          <div class="panel-card" style="flex:1">
            <div class="section-head">
              <div class="section-title">
                <span>注入方法选择</span>
                <span class="chip outline">6 种核心机制</span>
              </div>
              <span style="font-size:12px;color:var(--text-3)">点击卡片切换当前生效机制</span>
            </div>
            <div class="pcard-list" id="methodCardsContainer"></div>
          </div>

          <!-- Compact Log Preview Card -->
          <div class="panel-card" style="height:142px;flex:none;padding:10px 14px">
            <div class="section-head">
              <div class="section-title" style="font-size:13px">
                <svg class="i sm"><use href="#terminal"/></svg>
                <span>最近活动日志</span>
              </div>
              <div style="display:flex;gap:4px">
                <button class="btn quiet compact" onclick="switchView('logs')">查看完整日志</button>
                <button class="btn icon" onclick="sendCmd({cmd:'clear_logs'})" data-tip="清空日志" data-tip-pos="top" aria-label="清空"><svg class="i sm"><use href="#trash"/></svg></button>
              </div>
            </div>
            <div id="miniLogBox" style="flex:1;overflow-y:auto;font-family:var(--font-mono);font-size:11.5px;line-height:18px;color:var(--text-2)"></div>
          </div>
        </div>
      </div>
    </main>
)CCS5";

    static constexpr std::string_view kChunk6 = R"CCS6(
    <!-- View 2: Full Process Explorer -->
    <main class="view-body" id="view-processes" style="display:none">
      <div class="stats">
        <div class="stat"><div class="k">活跃进程总数</div><div class="v" id="statProcTotal">0</div><div class="d">实时快照</div></div>
        <div class="stat"><div class="k">64 位进程 (x64)</div><div class="v" id="statProcX64">0</div><div class="d">PE32+ 架构</div></div>
        <div class="stat"><div class="k">32 位进程 (x86)</div><div class="v" id="statProcX86">0</div><div class="d">WOW64 架构</div></div>
        <div class="stat"><div class="k">受限/系统进程</div><div class="v" id="statProcRestricted">0</div><div class="d">需 SeDebug 权限</div></div>
      </div>
      <div class="panel-card" style="flex:1;overflow:hidden">
        <div class="section-head">
          <div class="input-wrap" style="max-width:320px">
            <svg class="i sm search-ic"><use href="#search"/></svg>
            <input class="input with-ic" id="fullProcSearch" placeholder="按进程名或 PID 过滤..." oninput="onProcSearch(this.value)">
          </div>
          <button class="btn neutral compact" onclick="sendCmd({cmd:'refresh_processes'})"><svg class="i sm"><use href="#refresh"/></svg>刷新列表</button>
        </div>
        <div style="flex:1;overflow-y:auto">
          <table class="log">
            <thead><tr><th style="width:90px">PID</th><th>进程名称</th><th style="width:110px">架构</th><th style="width:130px">载荷兼容性</th><th class="num" style="width:120px">操作</th></tr></thead>
            <tbody id="fullProcTableBody"></tbody>
          </table>
        </div>
      </div>
    </main>

    <!-- View 3: DLL Doctor Static Analysis -->
    <main class="view-body" id="view-doctor" style="display:none">
      <div class="stats">
        <div class="stat"><div class="k">PE 载荷架构</div><div class="v" id="docStatArch">—</div><div class="d" id="docStatPeType">尚未加载文件</div></div>
        <div class="stat"><div class="k">目标兼容性</div><div class="v" id="docStatMatch">待定</div><div class="d" id="docStatTarget">请选定目标进程</div></div>
        <div class="stat"><div class="k">导入动态库</div><div class="v" id="docStatImports">0</div><div class="d" id="docStatDelay">延迟加载 0</div></div>
        <div class="stat"><div class="k">诊断发现项</div><div class="v" id="docStatFindings">0</div><div class="d" id="docStatBlock">无阻断性错误</div></div>
      </div>
      <div class="panel-card" style="flex:1;overflow:hidden">
        <div class="section-head">
          <div class="ptabs">
            <button type="button" class="on" id="docTabBtnFindings" onclick="setDocTab('findings')">诊断规则与建议 (Findings)</button>
            <button type="button" id="docTabBtnJson" onclick="setDocTab('json')">JSON 原始报告</button>
          </div>
          <div style="display:flex;gap:8px">
            <button class="btn neutral compact" onclick="sendCmd({cmd:'copy_doctor_json'})"><svg class="i sm"><use href="#copy"/></svg>复制 JSON</button>
            <button class="btn neutral compact" onclick="sendCmd({cmd:'export_doctor_report'})"><svg class="i sm"><use href="#download"/></svg>导出报告...</button>
          </div>
        </div>
        <div id="docFindingsPane" style="flex:1;overflow-y:auto;display:flex;flex-direction:column;gap:8px;padding-top:4px"></div>
        <pre id="docJsonPane" class="mono selectable" style="display:none;flex:1;overflow:auto;margin:0;padding:12px;background:var(--bg-subtle);border-radius:var(--r-control);color:var(--text-1)"></pre>
      </div>
    </main>

    <!-- View 4: Dependencies Table -->
    <main class="view-body" id="view-dependencies" style="display:none">
      <div class="panel-card" style="flex:1;overflow:hidden">
        <div class="section-head">
          <div class="section-title">
            <span>PE 导入表与依赖解析状态</span>
            <span class="chip outline" id="depTableCountChip">0 项</span>
          </div>
          <button class="btn neutral compact" onclick="sendCmd({cmd:'browse_dll'})"><svg class="i sm"><use href="#folder"/></svg>更换 DLL...</button>
        </div>
        <div style="flex:1;overflow-y:auto">
          <table class="log">
            <thead><tr><th>依赖模块名称 (DLL)</th><th style="width:140px">加载方式</th><th style="width:160px">静态解析状态</th></tr></thead>
            <tbody id="depTableBody"></tbody>
          </table>
        </div>
      </div>
    </main>

    <!-- View 5: Terminal Console Logs -->
    <main class="view-body" id="view-logs" style="display:none">
      <div class="panel-card" style="flex:1;overflow:hidden">
        <div class="section-head">
          <div class="input-wrap" style="max-width:280px">
            <svg class="i sm search-ic"><use href="#search"/></svg>
            <input class="input with-ic" id="logSearchInput" placeholder="搜索日志关键字或时间戳..." oninput="renderLogs()">
          </div>
          <div style="display:flex;align-items:center;gap:8px">
            <button class="btn neutral compact" onclick="sendCmd({cmd:'copy_logs'})"><svg class="i sm"><use href="#copy"/></svg>复制全部</button>
            <button class="btn neutral compact" onclick="sendCmd({cmd:'clear_logs'})"><svg class="i sm"><use href="#trash"/></svg>清空日志</button>
          </div>
        </div>
        <div style="flex:1;overflow-y:auto" id="fullLogScroll">
          <table class="log">
            <thead><tr><th style="width:110px">时间</th><th style="width:90px">级别</th><th>事件详情</th></tr></thead>
            <tbody id="fullLogTableBody"></tbody>
          </table>
        </div>
      </div>
    </main>
  </div>
</div>

<!-- Modal Dialog (Confirm / Settings) -->
<div class="modal-backdrop" id="modalBackdrop" onclick="if(event.target===this)closeModal()">
  <div class="dialog" role="dialog" aria-modal="true">
    <h4 id="modalTitle">提示</h4>
    <p id="modalBody"></p>
    <div class="ft" id="modalFooter"></div>
  </div>
</div>

<!-- Toast Container -->
<div id="toaster" aria-live="polite"></div>
)CCS6";

    static constexpr std::string_view kChunk7 = R"CCS7(
<script>
  const METHODS = [
    { id: 0, code: "CRT", title: "CreateRemoteThread", badge: "通用 · 经典", tone: "direct", dll: true, shell: true,
      desc: "通过 VirtualAllocEx 写入目标内存，由 CreateRemoteThread 唤起远程线程执行" },
    { id: 1, code: "APC", title: "QueueUserAPC", badge: "通用 · 异步队列", tone: "route", dll: true, shell: true,
      desc: "向目标进程可警惕线程投递异步过程调用 (APC)，不创建新远程线程" },
    { id: 2, code: "HK", title: "SetWindowsHookEx", badge: "仅 DLL · 窗口钩子", tone: "stack", dll: true, shell: false,
      desc: "安装 WH_GETMESSAGE 消息钩子并触发窗口事件以驱动目标加载模块" },
    { id: 3, code: "RFL", title: "Reflective Injection", badge: "仅 DLL · 内存自映射", tone: "stack", dll: true, shell: false,
      desc: "内存自解析 PE 头、导入表与重定位项直接映射执行，不落地 LoadLibrary" },
    { id: 4, code: "HJK", title: "DLL Hijacking", badge: "仅 DLL · 目录代理", tone: "stack", dll: true, shell: false,
      desc: "部署同名代理 DLL 至目标程序目录，利用系统搜索顺序接管加载" },
    { id: 5, code: "SYS", title: "Direct Syscall", badge: "实验性 · 系统调用", tone: "warning", dll: true, shell: true,
      desc: "直接 NT 系统调用实验支持（受当前编译构建策略管控）" }
  ];

  let state = {
    hostArch: "x64",
    debugPriv: false,
    hasSyscall: false,
    payloadMode: "dll",
    selectedMethod: 0,
    selectedPid: -1,
    dllPath: "",
    binPath: "",
    shellcodeSize: 0,
    preflight: { valid: false, dllArch: "", pe32Plus: true, importCount: 0, delayCount: 0, matchStatus: "WAIT", blocking: false, error: "", findings: [], dependencies: [], json: "" },
    processes: [],
    logs: []
  };

  let ui = {
    view: "workbench",
    theme: "dark",
    sidebarCollapsed: false,
    archFilter: "all",
    procSearch: "",
    docTab: "findings",
    guardEnabled: true
  };

  function sendCmd(payload) {
    if (window.chrome && window.chrome.webview) {
      window.chrome.webview.postMessage(JSON.stringify(payload));
    }
  }

  function esc(s) {
    return String(s ?? "").replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;");
  }

  function moveSegThumb(segId, thumbId) {
    const seg = document.getElementById(segId);
    const thumb = document.getElementById(thumbId);
    if (!seg || !thumb) return;
    const active = seg.querySelector('button[aria-pressed="true"]');
    if (!active) return;
    thumb.style.width = active.offsetWidth + "px";
    thumb.style.height = active.offsetHeight + "px";
    thumb.style.transform = `translate(${active.offsetLeft}px, ${active.offsetTop}px)`;
  }

  function syncAllThumbs() {
    moveSegThumb("themeSeg", "themeThumb");
    moveSegThumb("modeSeg", "modeThumb");
    moveSegThumb("archSeg", "archThumb");
  }

  function setTheme(mode) {
    ui.theme = mode;
    document.documentElement.classList.add("theme-switching");
    document.documentElement.setAttribute("data-theme", mode);
    document.getElementById("btnThemeLight").setAttribute("aria-pressed", mode === "light");
    document.getElementById("btnThemeDark").setAttribute("aria-pressed", mode === "dark");
    moveSegThumb("themeSeg", "themeThumb");
    sendCmd({ cmd: "set_theme", dark: mode === "dark" });
    requestAnimationFrame(() => requestAnimationFrame(() => {
      document.documentElement.classList.remove("theme-switching");
    }));
  }

  function toggleSidebar() {
    ui.sidebarCollapsed = !ui.sidebarCollapsed;
    const sb = document.getElementById("sidebar");
    sb.classList.toggle("collapsed", ui.sidebarCollapsed);
    document.getElementById("sidebarToggleIcon").innerHTML = `<use href="#${ui.sidebarCollapsed ? 'chev-right' : 'chev-left'}"/>`;
    document.getElementById("sidebarToggleBtn").setAttribute("data-tip", ui.sidebarCollapsed ? "展开侧栏" : "收起侧栏");
    setTimeout(syncAllThumbs, 50);
  }

  function switchView(view) {
    ui.view = view;
    document.querySelectorAll(".sidebar .nav-item[data-view]").forEach(btn => {
      btn.classList.toggle("active", btn.getAttribute("data-view") === view);
    });
    ["workbench", "processes", "doctor", "dependencies", "logs"].forEach(v => {
      const el = document.getElementById("view-" + v);
      if (el) el.style.display = (v === view) ? "flex" : "none";
    });
    const titles = {
      workbench: ["注入工作台", "直连注入 · 静态预检与 6 种注入机制", "plug"],
      processes: ["目标进程浏览", "只读枚举系统活跃进程与 WOW64/PE32+ 架构", "cpu"],
      doctor: ["DLL Doctor 静态诊断", "零权限离线 PE 头、导入表与兼容性证据分析", "route"],
      dependencies: ["依赖项解析表", "导入动态库与延迟加载模块解析状态", "layers"],
      logs: ["终端审计日志", "实时记录预检结果、权限状态与底层 API 调用", "terminal"]
    };
    const info = titles[view] || titles.workbench;
    document.getElementById("headerTitle").textContent = info[0];
    document.getElementById("headerSubtitle").textContent = info[1];
    document.getElementById("headerGlyphUse").setAttribute("href", "#" + info[2]);
    setTimeout(syncAllThumbs, 20);
  }

  function setPayloadMode(mode) {
    sendCmd({ cmd: "set_payload_mode", mode });
  }

  function setArchFilter(arch) {
    ui.archFilter = arch;
    document.querySelectorAll("#archSeg button").forEach(b => {
      b.setAttribute("aria-pressed", b.getAttribute("data-arch") === arch);
    });
    moveSegThumb("archSeg", "archThumb");
    renderProcesses();
  }

  function onProcSearch(val) {
    ui.procSearch = val;
    document.getElementById("procSearchInput").value = val;
    document.getElementById("fullProcSearch").value = val;
    document.getElementById("procSearchClear").style.display = val ? "grid" : "none";
    renderProcesses();
  }

  function toggleGuardSwitch() {
    ui.guardEnabled = !ui.guardEnabled;
    document.getElementById("guardSwitch").setAttribute("aria-checked", ui.guardEnabled);
    showToast(ui.guardEnabled ? "已开启预检拦截保护" : "已关闭预检拦截保护", "ok");
  }

  function setDocTab(tab) {
    ui.docTab = tab;
    document.getElementById("docTabBtnFindings").classList.toggle("on", tab === "findings");
    document.getElementById("docTabBtnJson").classList.toggle("on", tab === "json");
    document.getElementById("docFindingsPane").style.display = (tab === "findings") ? "flex" : "none";
    document.getElementById("docJsonPane").style.display = (tab === "json") ? "block" : "none";
  }
)CCS7";

    static constexpr std::string_view kChunk8 = R"CCS8(
  function showToast(msg, type = "ok") {
    const toaster = document.getElementById("toaster");
    const el = document.createElement("div");
    el.className = "toast";
    const iconId = type === "err" ? "circle-alert" : (type === "warn" ? "alert" : "check-circle");
    el.innerHTML = `<svg class="i ${type}"><use href="#${iconId}"/></svg><span class="grow">${esc(msg)}</span><button class="tclose" aria-label="关闭"><svg class="i sm"><use href="#x"/></svg></button>`;
    el.querySelector(".tclose").onclick = () => el.remove();
    toaster.appendChild(el);
    setTimeout(() => { if (el.parentNode) el.remove(); }, 2800);
  }

  function closeModal() {
    document.getElementById("modalBackdrop").classList.remove("open");
  }

  function showMethodInfo(id) {
    const m = METHODS.find(x => x.id === id);
    if (!m) return;
    document.getElementById("modalTitle").textContent = m.title;
    document.getElementById("modalBody").textContent = m.desc + "。支持模式：" + (m.dll && m.shell ? "DLL 动态库与 Shellcode" : "仅限 PE DLL 动态库") + "。";
    document.getElementById("modalFooter").innerHTML = `<button class="btn neutral compact" onclick="closeModal()">关闭</button><button class="btn solid compact" onclick="sendCmd({cmd:'select_method',method:${m.id}});closeModal()">设为当前方法</button>`;
    document.getElementById("modalBackdrop").classList.add("open");
  }

  function openSettingsModal() {
    document.getElementById("modalTitle").textContent = "界面与运行设置";
    document.getElementById("modalBody").innerHTML = `
      <div style="display:flex;flex-direction:column;gap:12px;margin-top:10px">
        <div style="display:flex;justify-content:space-between;align-items:center">
          <span>界面视觉主题 (CC Switch v7)</span>
          <div style="display:flex;gap:6px">
            <button class="btn neutral compact" onclick="setTheme('light')">浅色</button>
            <button class="btn neutral compact" onclick="setTheme('dark')">深色</button>
          </div>
        </div>
        <div style="display:flex;justify-content:space-between;align-items:center">
          <span>进程调试权限 (SeDebugPrivilege)</span>
          <button class="btn neutral compact" onclick="sendCmd({cmd:'enable_debug_priv'})">${state.debugPriv ? '已启用' : '立即启用'}</button>
        </div>
        <div style="font-size:12px;color:var(--text-3);border-top:1px solid var(--border);padding-top:10px">
          宿主架构：${esc(state.hostArch)} · 渲染引擎：Edge WebView2 · 设计规范：CC Switch v7
        </div>
      </div>`;
    document.getElementById("modalFooter").innerHTML = `<button class="btn neutral compact" onclick="closeModal()">完成</button>`;
    document.getElementById("modalBackdrop").classList.add("open");
  }

  function triggerInject() {
    if (state.selectedPid <= 0) {
      showToast("请先在进程列表中选定一个目标进程", "err");
      return;
    }
    if (ui.guardEnabled && state.payloadMode === "dll" && state.preflight.valid && state.preflight.blocking) {
      document.getElementById("modalTitle").textContent = "发现阻断性诊断项";
      document.getElementById("modalBody").textContent = "Core Doctor 检测到当前 DLL 与目标进程存在架构冲突或严重依赖异常，强制执行可能导致目标进程崩溃。是否仍要继续执行注入？";
      document.getElementById("modalFooter").innerHTML = `<button class="btn neutral compact" onclick="closeModal()">取消</button><button class="btn danger compact" onclick="closeModal();sendCmd({cmd:'inject',force:true})">仍然执行注入</button>`;
      document.getElementById("modalBackdrop").classList.add("open");
      return;
    }
    sendCmd({ cmd: "inject", force: false });
  }

  function renderNotices() {
    const box = document.getElementById("noticeStack");
    const items = [];
    const selectedProc = state.processes.find(p => p.pid === state.selectedPid);

    if (state.payloadMode === "dll" && state.preflight.valid && state.preflight.matchStatus === "MISMATCH") {
      items.push(`<div class="notice danger"><svg class="i"><use href="#circle-alert"/></svg><div class="grow"><div>架构冲突：DLL (${esc(state.preflight.dllArch)}) 与目标进程 (${esc(selectedProc ? selectedProc.arch : '')}) 位数不一致</div><div class="t2">跨位数注入将被系统拒绝，请更换匹配架构的载荷或目标进程。</div></div><button class="btn neutral compact" onclick="switchView('doctor')">查看详情</button></div>`);
    } else if (state.payloadMode === "dll" && state.dllPath && !state.preflight.valid && state.preflight.error) {
      items.push(`<div class="notice danger"><svg class="i"><use href="#circle-alert"/></svg><div class="grow"><div>PE 预检异常：${esc(state.preflight.error)}</div></div></div>`);
    } else if (state.payloadMode === "dll" && state.preflight.valid) {
      const tone = state.preflight.blocking ? "warning" : "route";
      const msg = state.preflight.matchStatus === "MATCH" ? "架构完全匹配" : "等待选定目标进程";
      items.push(`<div class="notice ${tone}"><svg class="i"><use href="#route"/></svg><div class="grow"><div>Core Doctor 静态预检就绪 · ${esc(state.preflight.dllArch)} · ${msg}</div><div class="t2">导入库 ${state.preflight.importCount} 个 · 诊断发现 ${state.preflight.findings.length} 条</div></div><button class="btn neutral compact" onclick="switchView('doctor')">查看预检报告</button></div>`);
    }

    if (!state.debugPriv) {
      items.push(`<div class="notice warning"><svg class="i"><use href="#alert"/></svg><div class="grow"><div>当前尚未启用 SeDebugPrivilege 调试权限</div><div class="t2">普通用户进程可正常操作；如需访问高权限服务进程，请点击右侧启用提权。</div></div><button class="btn neutral compact" onclick="sendCmd({cmd:'enable_debug_priv'})">启用提权</button></div>`);
    }
    box.innerHTML = items.join("");
  }

  function renderMethods() {
    const isDll = state.payloadMode === "dll";
    const container = document.getElementById("methodCardsContainer");
    container.innerHTML = METHODS.map(m => {
      const supported = isDll ? m.dll : m.shell;
      const active = state.selectedMethod === m.id;
      const activeClass = active ? `t-${m.tone === 'warning' ? 'direct' : m.tone}` : "";
      const dotColor = m.tone === "route" ? "var(--route-fill)" : (m.tone === "stack" ? "var(--stack-fill)" : "var(--direct-fill)");
      return `
        <div class="pcard ${activeClass} ${supported ? '' : 'disabled'}" onclick="${supported ? `sendCmd({cmd:'select_method',method:${m.id}})` : ''}">
          <span class="grip"><svg class="i sm"><use href="#grip"/></svg></span>
          <span class="logo">${esc(m.code)}</span>
          <div class="body">
            <div class="name">
              <span>${esc(m.title)}</span>
              <span class="chip ${supported ? m.tone : 'danger'}">${supported ? esc(m.badge) : '仅限 DLL'}</span>
            </div>
            <div class="sub">${esc(m.desc)}</div>
          </div>
          ${active ? `<span class="active-indicator"><span style="width:6px;height:6px;border-radius:50%;background:${dotColor}"></span>当前生效</span>` : ''}
          <div class="acts" onclick="event.stopPropagation()">
            <button class="btn icon" data-tip="切换此注入方法" aria-label="切换" ${supported ? '' : 'disabled'} onclick="sendCmd({cmd:'select_method',method:${m.id}})"><svg class="i"><use href="#check"/></svg></button>
            <button class="btn icon" data-tip="查看技术说明" aria-label="说明" onclick="showMethodInfo(${m.id})"><svg class="i"><use href="#info"/></svg></button>
            <button class="btn icon" data-tip="使用此方法立即注入" aria-label="执行" ${supported ? '' : 'disabled'} onclick="sendCmd({cmd:'select_method',method:${m.id}});triggerInject()"><svg class="i"><use href="#play"/></svg></button>
          </div>
        </div>`;
    }).join("");
  }
)CCS8";

    static constexpr std::string_view kChunk9 = R"CCS9(
  function renderProcesses() {
    const q = ui.procSearch.trim().toLowerCase();
    let countX64 = 0, countX86 = 0, countRest = 0;
    state.processes.forEach(p => {
      if (p.arch === "x64") countX64++;
      else if (p.arch === "x86") countX86++;
      else countRest++;
    });
    document.getElementById("navProcCount").textContent = state.processes.length;
    document.getElementById("procTotalChip").textContent = state.processes.length + " 个";
    document.getElementById("statProcTotal").textContent = state.processes.length;
    document.getElementById("statProcX64").textContent = countX64;
    document.getElementById("statProcX86").textContent = countX86;
    document.getElementById("statProcRestricted").textContent = countRest;

    const filtered = state.processes.filter(p => {
      if (ui.archFilter === "x64" && p.arch !== "x64") return false;
      if (ui.archFilter === "x86" && p.arch !== "x86") return false;
      if (q && !p.name.toLowerCase().includes(q) && !String(p.pid).includes(q)) return false;
      return true;
    });

    const wbList = document.getElementById("workbenchProcList");
    wbList.innerHTML = filtered.map(p => {
      const sel = p.pid === state.selectedPid;
      const archChip = p.arch === "x64" ? `<span class="chip direct">x64</span>` :
                       (p.arch === "x86" ? `<span class="chip warning">x86</span>` : `<span class="chip outline">受限</span>`);
      let compat = "";
      if (state.payloadMode === "dll" && state.preflight.valid && p.arch !== "受限") {
        const dllIs64 = state.preflight.dllArch.includes("x64");
        const procIs64 = p.arch === "x64";
        compat = (dllIs64 === procIs64)
          ? `<span class="chip success"><span class="d"></span>兼容</span>`
          : `<span class="chip danger"><span class="d"></span>冲突</span>`;
      }
      return `<div class="proc-row ${sel ? 'selected' : ''}" onclick="sendCmd({cmd:'select_process',pid:${p.pid}})">
        <span class="pid">${p.pid}</span>
        <span class="pname">${esc(p.name)}</span>
        ${archChip}
        ${compat}
      </div>`;
    }).join("") || `<div style="padding:24px;text-align:center;color:var(--text-3)">无匹配进程</div>`;

    const fullBody = document.getElementById("fullProcTableBody");
    fullBody.innerHTML = filtered.map(p => {
      const sel = p.pid === state.selectedPid;
      const archChip = p.arch === "x64" ? `<span class="chip direct">x64</span>` :
                       (p.arch === "x86" ? `<span class="chip warning">x86</span>` : `<span class="chip outline">受限</span>`);
      return `<tr style="${sel ? 'background:var(--direct-soft)' : ''}">
        <td class="mono">${p.pid}</td>
        <td style="font-weight:500">${esc(p.name)}</td>
        <td>${archChip}</td>
        <td>${sel ? '<span class="chip action"><span class="d"></span>当前目标</span>' : '<span class="chip outline">可选</span>'}</td>
        <td class="num"><button class="btn ${sel ? 'direct' : 'neutral'} compact" onclick="sendCmd({cmd:'select_process',pid:${p.pid}})">${sel ? '已选定' : '选为目标'}</button></td>
      </tr>`;
    }).join("");
  }

  function renderDoctorAndDeps() {
    const pf = state.preflight;
    document.getElementById("navFindingCount").textContent = pf.valid ? pf.findings.length : "—";
    document.getElementById("navDepCount").textContent = pf.valid ? pf.dependencies.length : "0";
    document.getElementById("depTableCountChip").textContent = (pf.valid ? pf.dependencies.length : 0) + " 项";

    document.getElementById("docStatArch").textContent = pf.valid ? pf.dllArch : "—";
    document.getElementById("docStatPeType").textContent = pf.valid ? (pf.pe32Plus ? "PE32+ (64-bit)" : "PE32 (32-bit)") : "尚未加载 DLL";
    document.getElementById("docStatMatch").textContent = pf.matchStatus === "MATCH" ? "完全兼容" : (pf.matchStatus === "MISMATCH" ? "架构冲突" : "待定");
    document.getElementById("docStatImports").textContent = pf.valid ? pf.importCount : 0;
    document.getElementById("docStatDelay").textContent = "延迟加载 " + (pf.valid ? pf.delayCount : 0);
    document.getElementById("docStatFindings").textContent = pf.valid ? pf.findings.length : 0;
    document.getElementById("docStatBlock").textContent = pf.blocking ? "存在阻断性问题" : "无阻断性错误";

    const fPane = document.getElementById("docFindingsPane");
    if (!pf.valid) {
      fPane.innerHTML = `<div class="notice neutral"><svg class="i"><use href="#info"/></svg><div class="grow">请先在「注入工作台」选择或拖入一个 <code>.dll</code> 文件以生成静态诊断报告。</div></div>`;
    } else if (pf.findings.length === 0) {
      fPane.innerHTML = `<div class="notice route"><svg class="i"><use href="#check-circle"/></svg><div class="grow"><div>静态检查全部通过</div><div class="t2">未发现架构不匹配、畸形头部或明显缺失的直接依赖项。</div></div></div>`;
    } else {
      fPane.innerHTML = pf.findings.map(f => {
        const tone = f.severity === "error" ? "danger" : (f.severity === "warning" ? "warning" : "neutral");
        return `<div class="notice ${tone}">
          <svg class="i"><use href="#${f.severity === 'error' ? 'circle-alert' : 'alert'}"/></svg>
          <div class="grow">
            <div style="display:flex;align-items:center;gap:8px">
              <strong>${esc(f.rule_id)}</strong>
              <span class="chip ${tone}">${esc(f.severity)}</span>
              <span class="chip outline">置信度: ${esc(f.confidence)}</span>
            </div>
            <div class="t2" style="color:var(--text-1);margin-top:4px">${esc(f.message)}</div>
            ${f.next_step ? `<div class="t2">建议措施：${esc(f.next_step)}</div>` : ''}
          </div>
        </div>`;
      }).join("");
    }
    document.getElementById("docJsonPane").textContent = pf.json || "// 暂无诊断 JSON 数据";

    const depBody = document.getElementById("depTableBody");
    if (!pf.valid || pf.dependencies.length === 0) {
      depBody.innerHTML = `<tr><td colspan="3" style="text-align:center;padding:24px;color:var(--text-3)">暂无依赖项数据</td></tr>`;
    } else {
      depBody.innerHTML = pf.dependencies.map(d => {
        const isOk = d.status.includes("resolved") || d.status.includes("known") || d.status.includes("apiset");
        const chipTone = isOk ? "success" : (d.status.includes("missing") ? "danger" : "warning");
        return `<tr>
          <td class="mono" style="font-weight:500">${esc(d.name)}</td>
          <td><span class="chip ${d.delay ? 'stack' : 'outline'}">${d.delay ? 'Delay-Load' : 'Standard Import'}</span></td>
          <td><span class="chip ${chipTone}"><span class="d"></span>${esc(d.status)}</span></td>
        </tr>`;
      }).join("");
    }
  }

  function renderLogs() {
    document.getElementById("navLogCount").textContent = state.logs.length;
    const mini = document.getElementById("miniLogBox");
    const recent = state.logs.slice(-6);
    mini.innerHTML = recent.map(l => {
      const col = l.level === "ERROR" ? "var(--danger-text)" : (l.level === "SUCCESS" ? "var(--success-text)" : (l.level === "WARN" ? "var(--warning-text)" : "var(--text-2)"));
      return `<div style="white-space:nowrap;overflow:hidden;text-overflow:ellipsis"><span style="color:var(--text-3)">[${esc(l.time)}]</span> <span style="color:${col};font-weight:600">[${esc(l.level)}]</span> ${esc(l.msg)}</div>`;
    }).join("");
    mini.scrollTop = mini.scrollHeight;

    const q = (document.getElementById("logSearchInput").value || "").trim().toLowerCase();
    const filtered = state.logs.filter(l => !q || l.msg.toLowerCase().includes(q) || l.time.includes(q) || l.level.toLowerCase().includes(q));
    const tbody = document.getElementById("fullLogTableBody");
    tbody.innerHTML = filtered.map(l => {
      const tone = l.level === "ERROR" ? "danger" : (l.level === "SUCCESS" ? "success" : (l.level === "WARN" ? "warning" : (l.level === "DOCTOR" ? "route" : "direct")));
      return `<tr><td class="mono" style="color:var(--text-3)">${esc(l.time)}</td><td><span class="chip ${tone}">${esc(l.level)}</span></td><td class="selectable">${esc(l.msg)}</td></tr>`;
    }).join("");
    const scroll = document.getElementById("fullLogScroll");
    if (scroll) scroll.scrollTop = scroll.scrollHeight;
  }

  function applyState(newState) {
    state = { ...state, ...newState };
    document.getElementById("chipHostArch").textContent = "宿主 " + state.hostArch;
    const privChip = document.getElementById("chipPriv");
    privChip.className = "chip " + (state.debugPriv ? "success" : "warning");
    document.getElementById("chipPrivText").textContent = state.debugPriv ? "SeDebug 已启用" : "SeDebug 未提权";
    document.getElementById("navPrivText").textContent = state.debugPriv ? "已提权" : "提权";

    const selProc = state.processes.find(p => p.pid === state.selectedPid);
    const targetChip = document.getElementById("chipTargetProc");
    if (selProc) {
      targetChip.style.display = "inline-flex";
      targetChip.textContent = `${selProc.name} (PID ${selProc.pid})`;
      document.getElementById("btnExecuteText").textContent = `执行注入 · PID ${selProc.pid}`;
    } else {
      targetChip.style.display = "none";
      document.getElementById("btnExecuteText").textContent = "执行注入";
    }

    const isDll = state.payloadMode === "dll";
    document.getElementById("navModeTag").textContent = isDll ? "DLL" : "HEX";
    document.getElementById("navModeTag").className = "nav-tag chip " + (isDll ? "direct" : "stack");
    document.getElementById("modeBtnDll").setAttribute("aria-pressed", isDll);
    document.getElementById("modeBtnDll").className = isDll ? "act-direct" : "";
    document.getElementById("modeBtnShell").setAttribute("aria-pressed", !isDll);
    document.getElementById("modeBtnShell").className = !isDll ? "act-stack" : "";
    document.getElementById("dllPayloadBox").style.display = isDll ? "block" : "none";
    document.getElementById("shellcodePayloadBox").style.display = isDll ? "none" : "block";

    if (document.activeElement !== document.getElementById("dllPathInput")) {
      document.getElementById("dllPathInput").value = state.dllPath || "";
    }
    document.getElementById("binPathInput").value = state.binPath || "";
    document.getElementById("dllArchChip").textContent = state.preflight.valid ? state.preflight.dllArch : "未载入";
    document.getElementById("shellByteChip").textContent = state.shellcodeSize + " 字节";

    const m = METHODS.find(x => x.id === state.selectedMethod) || METHODS[0];
    document.getElementById("modeStatusText").textContent = `当前机制：${m.title} · ${selProc ? ('目标 ' + selProc.name) : '请选定目标进程'}`;

    renderNotices();
    renderMethods();
    renderProcesses();
    renderDoctorAndDeps();
    renderLogs();
    syncAllThumbs();
  }

  window.addEventListener("DOMContentLoaded", () => {
    if (window.chrome && window.chrome.webview) {
      window.chrome.webview.addEventListener("message", (ev) => {
        const data = typeof ev.data === "string" ? JSON.parse(ev.data) : ev.data;
        if (data.type === "state") applyState(data.state);
        if (data.toast) showToast(data.toast.msg, data.toast.tone || "ok");
      });
    }
    window.addEventListener("resize", syncAllThumbs);
    window.addEventListener("keydown", (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "f") {
        e.preventDefault();
        const inp = document.getElementById("procSearchInput");
        if (inp) inp.focus();
      }
    });

    // Native File Drag & Drop bridge (WebView2 postMessageWithAdditionalObjects)
    const dz = document.getElementById("dropZone");
    window.addEventListener("dragover", (e) => { e.preventDefault(); if (dz) dz.classList.add("dragover"); });
    window.addEventListener("dragleave", () => { if (dz) dz.classList.remove("dragover"); });
    window.addEventListener("drop", (e) => {
      e.preventDefault();
      if (dz) dz.classList.remove("dragover");
      if (e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files.length > 0) {
        if (window.chrome && window.chrome.webview && window.chrome.webview.postMessageWithAdditionalObjects) {
          window.chrome.webview.postMessageWithAdditionalObjects("drop", e.dataTransfer.files);
        }
      }
    });

    syncAllThumbs();
    sendCmd({ cmd: "init" });
  });
</script>
</body>
</html>
)CCS9";

    std::string html;
    html.reserve(kChunk1.size() + kChunk2.size() + kChunk3.size() +
                 kChunk4.size() + kChunk5.size() + kChunk6.size() +
                 kChunk7.size() + kChunk8.size() + kChunk9.size());
    html.append(kChunk1);
    html.append(kChunk2);
    html.append(kChunk3);
    html.append(kChunk4);
    html.append(kChunk5);
    html.append(kChunk6);
    html.append(kChunk7);
    html.append(kChunk8);
    html.append(kChunk9);
    return html;
}

} // namespace gui
