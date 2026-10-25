"""Contract tests for dllinjector inject command."""
import json
import subprocess
import sys
from pathlib import Path

if len(sys.argv) >= 5:
    cli, noop_dll, demo_target, root = map(Path, sys.argv[1:5])
else:
    root = Path(__file__).resolve().parent.parent
    cli = root / "build/msvc-x64/Release/cli/dllinjector.exe"
    noop_dll = root / "build/msvc-x64/Release/noop_fixture.dll"
    demo_target = root / "build/msvc-x64/Release/demo_target.exe"
    if not (cli.exists() and noop_dll.exists() and demo_target.exists()):
        print(f"Usage: python {Path(__file__).name} <cli_path> <noop_dll_path> <demo_target_path> <repo_root>")
        sys.exit(0)

def run_cmd(*args):
    cmd = [str(cli), *map(str, args)]
    res = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
    return res.returncode, res.stdout, res.stderr

# 1. Help text includes inject
code, out, err = run_cmd("--help")
assert code == 0
assert "dllinjector inject" in out
assert "--method" in out

# 2. Parameter validation (Exit Code 2: Usage / InvalidArgument)
# Missing --dll
code, out, err = run_cmd("inject", "--method", "crt", "--pid", "1234")
assert code == 2 or code == 1
assert "inject requires --dll" in err or "inject requires --dll" in out

# Missing --method
code, out, err = run_cmd("inject", "--dll", str(noop_dll), "--pid", "1234")
assert code == 2 or code == 1
assert "inject requires --method" in err or "inject requires --method" in out

# Missing --pid for CRT
code, out, err = run_cmd("inject", "--dll", str(noop_dll), "--method", "crt")
assert code == 2 or code == 1
assert "requires --pid" in err or "requires --pid" in out

# Hijack requires --target-dir, not --pid
code, out, err = run_cmd("inject", "--dll", str(noop_dll), "--method", "hijack", "--pid", "1234")
assert code == 2 or code == 1
assert "target-dir" in err or "target-dir" in out

# 3. Nonexistent DLL preflight check (Exit Code 1 / error status in JSON)
code, out, err = run_cmd("inject", "--dll", "C:\\nonexistent_test_dll_12345.dll", "--method", "crt", "--pid", "1234", "--json")
assert code != 0
obj = json.loads(out)
assert obj["command"] == "inject"
assert obj["status"] == "error"
assert any(e["category"] == "not_found" for e in obj["errors"])

# 4. Valid DLL with nonexistent PID
code, out, err = run_cmd("inject", "--dll", str(noop_dll), "--method", "crt", "--pid", "999999", "--json")
assert code != 0
obj = json.loads(out)
assert obj["command"] == "inject"
assert obj["status"] == "error"
assert any(e["stage"] == "OpenProcess" for e in obj["errors"])

print("All inject contract assertions passed successfully.")

