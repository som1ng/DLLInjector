"""Run the real CLI, validate JSON and observable behavior; no privileged targets."""
import json
import copy
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from jsonschema import Draft202012Validator

cli, demo, root = map(Path, sys.argv[1:])
schema = json.loads((root / "schemas/cli-v1.1.json").read_text(encoding="utf-8"))
Draft202012Validator.check_schema(schema)
validator = Draft202012Validator(schema)
old_schema = json.loads((root / "schemas/cli-v1.json").read_text(encoding="utf-8"))
Draft202012Validator.check_schema(old_schema)
old_validator = Draft202012Validator(old_schema)


def run(*args, code=0, json_mode=True):
    command = [str(cli), *map(str, args)]
    if json_mode and "--json" not in args:
        command.append("--json")
    result = subprocess.run(command, capture_output=True, timeout=15)
    assert result.returncode == code, (command, result.returncode, result.stderr.decode("utf-8"))
    out = result.stdout.decode("utf-8")
    err = result.stderr.decode("utf-8")
    assert not out.startswith("\ufeff")
    if not json_mode:
        return out, err
    obj = json.loads(out)  # Rejects log prefixes and multiple documents.
    validator.validate(obj)
    assert obj["duration_ms"] >= 0
    assert obj["schema_version"] == "1.1"
    assert obj["status"] == ("success" if code == 0 else "error")
    assert obj["execution"]["outcome_verified"] == (code == 0)
    assert bool(obj["errors"]) == (code != 0)
    if code:
        assert err, "errors must also have stderr diagnostics"
    elif "--verbose" not in args:
        assert not err, err
    return obj, err


fixtures = sorted((root / "tests/fixtures/json").glob("*.json"))
assert len(fixtures) >= 3, "fixed schema fixtures must be present"
for fixture in fixtures:
    document = json.loads(fixture.read_text(encoding="utf-8"))
    (old_validator if document["schema_version"] == "1.0" else validator).validate(document)

valid = json.loads((root / "tests/fixtures/json/inspect.json").read_text(encoding="utf-8"))
valid["schema_version"] = "1.1"
validator.validate(valid)
invalid = copy.deepcopy(valid)
del invalid["schema_version"]
assert not validator.is_valid(invalid)
invalid = copy.deepcopy(valid)
invalid["data"]["creation_time_filetime"] = 134370000000000001
assert not validator.is_valid(invalid), "FILETIME must not lose precision in JS"
invalid = copy.deepcopy(valid)
invalid["execution"]["outcome_verified"] = False
assert not validator.is_valid(invalid), "success and execution flags must agree"
invalid = copy.deepcopy(valid)
invalid["data"]["architecture"] = "unknown"
assert not validator.is_valid(invalid), "unknown architecture cannot be inspect success"

version, _ = run("--version")
assert version["tool_version"] == "2.0.0-dev.2"
assert "process inspect" in run("--help")[0]["data"]["text"]
assert "2.0.0-dev.2" in run("--version", json_mode=False)[0]
assert "process list" in run("--help", json_mode=False)[0]

listing, _ = run("process", "list")
rows = listing["data"]["processes"]
assert any(p["pid"] == os.getpid() for p in rows)
assert [p["pid"] for p in rows] == sorted(p["pid"] for p in rows)
assert len(rows) == len({p["pid"] for p in rows})
assert all(p["pid"] > 0 for p in rows)

own, logs = run("--verbose", "process", "inspect", "--pid", os.getpid())
assert own["data"]["pid"] == os.getpid()
assert own["data"]["architecture"] in ("x86", "x64", "arm64")
assert own["data"]["creation_time_filetime"].isdigit()
assert "[debug]" in logs
assert own["target"]["pid"] == os.getpid()

invalid = [[], ["process"], ["process", "modules"], ["doctor", "x.dll"],
           ["process", "inspect"], ["process", "list", "extra"],
           ["--help", "process", "list"], ["--help", "--version"],
           ["process", "list", "--pid", "1"], ["--version", "--pid", "1"],
           ["--unknown"], ["--json", "--json"], ["--timeout-ms"],
           ["--output"], ["--output", "--verbose"],
           ["--version", "--verbose", "--verbose"],
           ["process", "inspect", "--pid", "1", "--pid", "2"]]
for number in ("0", "-1", "+1", "1x", "4294967296", "99999999999999999999999"):
    invalid.append(["process", "inspect", "--pid", number])
for number in ("-1", "1.5", "600001", "99999999999999999999999"):
    invalid.append(["process", "list", "--timeout-ms", number])
for args in invalid:
    doc, _ = run(*args, code=2)
    assert not doc["execution"]["command_accepted"]
    assert doc["errors"][0]["category"] == "invalid_argument"

doc, _ = run("process", "inspect", "--pid", "4294967295", code=3)
assert doc["errors"][0]["stage"] == "OpenProcess"
assert doc["errors"][0]["win32"] is not None
for args in (("process", "list"), ("process", "inspect", "--pid", str(os.getpid()))):
    doc, _ = run(*args, "--timeout-ms", "0", code=5)
    assert doc["errors"][0]["category"] == "timeout"
    assert doc["data"] is None
    assert doc["execution"]["command_accepted"]

with tempfile.TemporaryDirectory(prefix="dllinjector-p1-") as tmp:
    folder = Path(tmp) / "中文 space 🚀"
    folder.mkdir()
    output = folder / "结果.json"
    doc, _ = run("process", "inspect", "--pid", os.getpid(), "--output", output)
    assert json.loads(output.read_bytes().decode("utf-8")) == doc
    before = output.read_bytes()
    run("--version", "--output", output, code=4)
    assert output.read_bytes() == before, "existing reports must never be overwritten"
    run("--version", "--output", folder / "missing" / "out.json", code=4)
    run("bad-command", "--output", folder / "invalid.json", code=2)
    assert not (folder / "invalid.json").exists()
    human = folder / "version.txt"
    text, _ = run("--version", "--output", human, json_mode=False)
    assert human.read_bytes().decode("utf-8") == text
    failure_path = folder / "error.json"
    failed, _ = run("process", "inspect", "--pid", "4294967295", "--output", failure_path, code=3)
    assert json.loads(failure_path.read_text(encoding="utf-8")) == failed
    executable = folder / "测试程序 🚀.exe"
    shutil.copyfile(demo, executable)
    child = subprocess.Popen([str(executable)], creationflags=subprocess.CREATE_NO_WINDOW,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        doc, _ = run("process", "inspect", "--pid", child.pid)
        assert doc["data"]["name"] == executable.name
        assert Path(doc["data"]["image_path"]) == executable
        doc, _ = run("process", "list")
        assert any(p["pid"] == child.pid and p["name"] == executable.name
                   for p in doc["data"]["processes"])
    finally:
        child.terminate()  # Only the child created by this test.
        child.wait(timeout=5)

print("PASS CLI subprocess contract and JSON Schema")
