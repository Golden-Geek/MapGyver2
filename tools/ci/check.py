#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Project build/test wrapper; retain environment and command logs even on failure."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from build_metadata import source_identity


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", required=True, choices=("windows-local", "windows-ci", "portable-ci"))
    parser.add_argument("--configuration", default="Release", choices=("Debug", "Release"))
    parser.add_argument("--skip-bootstrap", action="store_true", help="Use an already bootstrapped pinned dependency root")
    args = parser.parse_args()
    if args.preset.startswith("windows-") and platform.system() != "Windows":
        parser.error("Windows presets require a native Windows host")
    if args.preset == "portable-ci" and (platform.system() not in {"Linux", "Darwin"} or args.configuration != "Release"):
        parser.error("portable-ci is the native Linux/macOS Release profile")
    build = ROOT / "out" / "build" / args.preset
    reports = ROOT / "out" / "reports" / args.preset / args.configuration
    reports.mkdir(parents=True, exist_ok=True)
    revision, dirty = source_identity(ROOT)
    evidence = {
        "schemaVersion": 1, "evidenceClass": "native-build-and-non-hardware-tests",
        "startedUtc": datetime.now(timezone.utc).isoformat(),
        "sourceRevision": revision or None, "sourceDirty": dirty,
        "hosted": os.environ.get("GITHUB_ACTIONS") == "true",
        "ciRunUrl": (f'https://github.com/{os.environ["GITHUB_REPOSITORY"]}/actions/runs/{os.environ["GITHUB_RUN_ID"]}'
                     if os.environ.get("GITHUB_RUN_ID") and os.environ.get("GITHUB_REPOSITORY") else None),
        "host": {"system": platform.system(), "release": platform.release(),
                 "version": platform.version(), "machine": platform.machine(), "python": platform.python_version()},
        "runner": {name: os.environ.get(name) for name in ("ImageOS", "ImageVersion", "RUNNER_OS", "RUNNER_ARCH")},
        "preset": args.preset, "configuration": args.configuration,
        "dependencyBaseline": json.loads((ROOT / "vcpkg-configuration.json").read_text(encoding="utf-8"))["default-registry"]["baseline"],
        "commands": [], "result": "running"
    }
    def save():
        (reports / "environment.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    def run(name, command, timeout=900):
        command = list(map(str, command))
        print("+", " ".join(command), flush=True)
        entry = {"name": name, "command": command, "exitCode": None, "log": f"{name}.log"}
        evidence["commands"].append(entry)
        save()
        log = reports / entry["log"]
        try:
            with log.open("w", encoding="utf-8") as output:
                result = subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT,
                                        timeout=timeout, check=False)
            entry["exitCode"] = result.returncode
            if result.returncode:
                print(log.read_text(encoding="utf-8", errors="replace")[-16000:], flush=True)
                raise RuntimeError(f"{name} exited {result.returncode}; see {log}")
        except subprocess.TimeoutExpired:
            entry["error"] = "timeout"
            raise
        finally:
            save()
    save()
    try:
        run("cmake-version", ["cmake", "--version"])
        run("git-version", ["git", "--version"])
        if not args.skip_bootstrap:
            triplet = "x64-windows" if args.preset.startswith("windows-") else (
                "arm64-osx" if platform.system() == "Darwin" else "x64-linux")
            command = [sys.executable, ROOT / "tools/ci/bootstrap.py", "--triplet", triplet]
            if args.preset == "portable-ci":
                command.append("--core-only")
            run("bootstrap", command, timeout=1800)
        run("dependency-identity", ["git", "-C", ROOT / "out/deps/vcpkg", "rev-parse", "HEAD"])
        if (reports / "dependency-identity.log").read_text(encoding="utf-8").strip() != evidence["dependencyBaseline"]:
            raise RuntimeError("Bootstrapped dependency tool source does not match the pinned registry")
        run("configure", ["cmake", "--preset", args.preset], timeout=1800)
        run("build", ["cmake", "--build", build, "--config", args.configuration, "--parallel", "2"])
        run("test", ["ctest", "--test-dir", build, "-C", args.configuration,
                     "--output-on-failure", "--no-tests=error", "-LE", "hardware",
                     "--output-junit", reports / "test-results.xml"])
        executable = (build / args.configuration / "mapgyver-engine.exe" if platform.system() == "Windows"
                      else build / "mapgyver-engine")
        run("diagnostics", [executable, "--diagnose", "--json", "--no-gpu"])
        evidence["diagnostics"] = json.loads((reports / "diagnostics.log").read_text(encoding="utf-8"))
        configure_log = (reports / "configure.log").read_text(encoding="utf-8", errors="replace")
        sdk = re.search(r"Selecting Windows SDK version ([0-9.]+)", configure_log)
        evidence["windowsSdk"] = sdk.group(1) if sdk else None
        status = (build / "vcpkg_installed/vcpkg/status").read_text(encoding="utf-8")
        evidence["resolvedPackages"] = []
        for paragraph in status.split("\n\n"):
            fields = dict(line.split(": ", 1) for line in paragraph.splitlines() if ": " in line and not line.startswith(" "))
            if fields.get("Status") == "install ok installed":
                evidence["resolvedPackages"].append({key: fields.get(key) for key in ("Package", "Version", "Port-Version", "Architecture")})
        evidence["result"] = "passed"
        print(f"Passed {args.preset}/{args.configuration}; evidence: {reports}", flush=True)
        return 0
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        evidence["result"] = "failed"
        evidence["error"] = str(error)
        print(f"Validation failed: {error}", file=sys.stderr)
        return 1
    finally:
        evidence["finishedUtc"] = datetime.now(timezone.utc).isoformat()
        save()


if __name__ == "__main__":
    sys.exit(main())
