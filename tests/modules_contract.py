"""Exercise module observations in an owned host; optionally a second-bitness CLI."""
import json
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import tempfile
import threading

from jsonschema import Draft202012Validator

cli, host, fixture, root = map(Path, sys.argv[1:5])
# Cross test reuses this test with a second-bitness CLI and expected result.
other_cli = Path(sys.argv[5]) if len(sys.argv) > 5 else None
other_code = int(sys.argv[6]) if len(sys.argv) > 6 else None
schema = json.loads((root / "schemas/cli-v1.5.json").read_text(encoding="utf-8"))
Draft202012Validator.check_schema(schema)
validator = Draft202012Validator(schema)


def run(executable, *args, code=0, json_mode=True):
    command = [str(executable), *map(str, args)] + (["--json"] if json_mode else [])
    result = subprocess.run(command, capture_output=True, timeout=15)
    assert result.returncode == code, (command, result.returncode, result.stderr.decode("utf-8"))
    out, err = result.stdout.decode("utf-8"), result.stderr.decode("utf-8")
    if not json_mode:
        return out
    obj = json.loads(out)
    validator.validate(obj)
    assert obj["schema_version"] == "1.5"
    assert obj["status"] == ("success" if code == 0 else "error")
    assert obj["execution"]["outcome_verified"] == (code == 0)
    assert bool(err) == (code != 0 or "--verbose" in args)
    if code:
        assert obj["data"] is None
    return obj


assert "process modules" in run(cli, "--help")["data"]["text"]
for args in (["process", "modules"], ["process", "modules", "--pid", "0"],
             ["process", "modules", "--pid", "-1"], ["process", "modules", "--pid", "4294967296"],
             ["process", "modules", "--pid", "1", "extra"],
             ["process", "modules", "--pid", "1", "--pid", "2"]):
    assert not run(cli, *args, code=2)["execution"]["command_accepted"]
unavailable = run(cli, "process", "modules", "--pid", "4294967295", code=3)
assert unavailable["errors"][0]["stage"] == "OpenProcess"

fixture_doc = json.loads((root / "tests/fixtures/json/modules.json").read_text(encoding="utf-8"))
fixture_doc["schema_version"] = "1.5"
fixture_doc["target"]["path"] = None
validator.validate(fixture_doc)
fixture_doc["data"]["modules"][0]["base_address"] = 9007199254740993
assert not validator.is_valid(fixture_doc), "addresses cannot be lossy JSON numbers"

with tempfile.TemporaryDirectory(prefix="dllinjector-modules-") as tmp:
    folder = Path(tmp) / "模块 space 🚀"
    folder.mkdir()
    copied_host, copied_dll = folder / "host 测试.exe", folder / "插件 🚀.dll"
    shutil.copyfile(host, copied_host)
    shutil.copyfile(fixture, copied_dll)
    child = subprocess.Popen([str(copied_host), str(copied_dll)], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        ready = queue.Queue()
        threading.Thread(target=lambda: ready.put(child.stdout.readline()), daemon=True).start()
        handshake = ready.get(timeout=10).split()
        assert len(handshake) == 2 and handshake[0] == b"READY", "owned host failed to load fixture"
        fixture_address = int(handshake[1], 16)
        report_path = folder / "模块.json"
        doc = run(cli, "--verbose", "process", "modules", "--pid", child.pid, "--output", report_path)
        assert json.loads(report_path.read_text(encoding="utf-8")) == doc
        data = doc["data"]
        assert data["pid"] == child.pid and doc["target"]["pid"] == child.pid
        assert data["creation_time_filetime"].isdigit() and data["scope"] == "toolhelp_snapshot"
        rows = data["modules"]
        addresses = [int(row["base_address"], 16) for row in rows]
        assert addresses == sorted(addresses) and all(addresses)
        assert all(row["size_bytes"] > 0 for row in rows)
        # The cooperative host identifies the fixture independently of loader text.
        plugin = next(row for row in rows if int(row["base_address"], 16) == fixture_address)
        if Path(plugin["path"]) != copied_dll or plugin["name"] != copied_dll.name:
            assert plugin["text_may_be_lossy"] and "?" in plugin["path"] + plugin["name"], plugin
        else:
            assert not plugin["text_may_be_lossy"]
        assert not plugin["path_may_be_truncated"] and not plugin["name_may_be_truncated"]
        assert any(row["name"].startswith("host ") and row["name"].endswith(".exe") for row in rows)
        text = run(cli, "process", "modules", "--pid", child.pid, json_mode=False)
        assert plugin["name"] in text and "BASE_ADDRESS" in text
        if plugin["text_may_be_lossy"]:
            assert "possibly lossy" in text
        timeout = run(cli, "process", "modules", "--pid", child.pid, "--timeout-ms", "0", code=5)
        assert timeout["errors"][0]["category"] == "timeout"
        if other_cli:
            cross = run(other_cli, "process", "modules", "--pid", child.pid, code=other_code)
            if other_code == 0:
                assert cross["data"]["architecture"] == "x86"
                assert any(int(row["base_address"], 16) == fixture_address for row in cross["data"]["modules"])
            else:
                error = cross["errors"][0]
                assert error["category"] == "unsupported" and error["stage"] == "modules.architecture"
                assert error["win32"] is None and "x64 CLI" in error["message"]
    finally:
        if child.poll() is None:
            child.stdin.write(b"\n")
            child.stdin.flush()
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            child.terminate()  # Only this test's own child.
            child.wait(timeout=5)
        child.stdin.close()
        child.stdout.close()
        child.stderr.close()
    assert child.returncode == 0, "fixture should unload and host should exit cleanly"

print("PASS module CLI contract" + (" and cross-bitness" if other_cli else ""))
