# GUI regression tests

Run on a Windows machine with Node.js and Microsoft Edge installed:

```powershell
npm ci --ignore-scripts --no-audit --no-fund
npm test
```

The tests extract the embedded HTML from `apps/gui/cc_switch_ui.h`,
launch headless Edge, and use simulated native messages. They cover layout,
navigation, diagnostics presentation, scrolling, dialogs, and window controls.
They do not execute injection or verify native window dragging / monitor DPI.

For an existing Playwright installation, `DLL_GUI_PLAYWRIGHT` may name its
absolute package path instead of installing another local copy.
