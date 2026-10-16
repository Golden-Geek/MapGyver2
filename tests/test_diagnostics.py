#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""End-to-end diagnostic schema, build identity, capabilities and failure exits."""
import argparse
import json
from pathlib import Path
import platform
import re
import struct
import subprocess
import sys
import unittest

parser = argparse.ArgumentParser()
parser.add_argument("--engine", required=True, type=Path)
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--configuration", required=True)
args, remaining = parser.parse_known_args()

def validate(value, schema, document, path="$Report"):
    """Validate only the declared schema's subset; unknown keywords fail closed."""
    supported = {"$schema", "$id", "title", "$defs", "$ref", "type", "const", "enum",
                 "minLength", "pattern", "required", "properties", "additionalProperties"}
    def require(condition, message):
        if not condition:
            raise AssertionError(f"{path}: {message}")
    require(not set(schema) - supported, "unsupported schema keywords")
    if "$ref" in schema:
        require(schema["$ref"].startswith("#/$defs/"), "unsupported schema reference")
        return validate(value, document["$defs"][schema["$ref"].split("/")[-1]], document, path)
    checks = {"object": lambda v: isinstance(v, dict), "string": lambda v: isinstance(v, str),
              "boolean": lambda v: type(v) is bool, "integer": lambda v: type(v) is int,
              "null": lambda v: v is None}
    if "type" in schema:
        kinds = schema["type"] if isinstance(schema["type"], list) else [schema["type"]]
        require(any(checks[kind](value) for kind in kinds), f"expected {kinds}")
    if "const" in schema:
        require(type(value) is type(schema["const"]) and value == schema["const"], "wrong constant")
    if "enum" in schema:
        require(any(type(value) is type(item) and value == item for item in schema["enum"]), "invalid enum")
    if isinstance(value, str):
        require(len(value) >= schema.get("minLength", 0), "string too short")
        if "pattern" in schema:
            require(re.search(schema["pattern"], value) is not None, "invalid pattern")
    if isinstance(value, dict):
        require(set(schema.get("required", [])) <= value.keys(), "missing required properties")
        properties = schema.get("properties", {})
        if schema.get("additionalProperties") is False:
            require(value.keys() <= properties.keys(), "unknown properties")
        for name, item in value.items():
            if name in properties:
                validate(item, properties[name], document, f"{path}.{name}")

class Diagnostics(unittest.TestCase):
    def run_cli(self, *flags):
        return subprocess.run([str(args.engine), *flags], capture_output=True, text=True, timeout=10)

    def report(self, *flags):
        result = self.run_cli("--diagnose", "--json", *flags)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        return json.loads(result.stdout)

    def test_schema_and_actual_build_identity(self):
        report = self.report("--no-gpu")
        schema = json.loads((args.source / "schemas/diagnostics-v1.schema.json").read_text(encoding="utf-8"))
        validate(report, schema, schema)
        self.assertEqual(report["build"]["configuration"], args.configuration)
        self.assertEqual(report["platform"]["os"], platform.system())
        self.assertEqual(report["platform"]["pointerBits"], struct.calcsize("P") * 8)
        self.assertEqual(report["platform"]["byteOrder"], sys.byteorder)
        expected_arch = {"AMD64": "x64", "x86_64": "x64", "arm64": "arm64", "aarch64": "arm64"}
        self.assertEqual(report["platform"]["architecture"], expected_arch.get(platform.machine(), "unknown"))
        if (args.source / ".git").exists():
            sha = subprocess.check_output(["git", "-C", str(args.source), "rev-parse", "HEAD"], text=True).strip()
            self.assertEqual(report["build"]["revision"], sha)
            status = subprocess.check_output(["git", "-C", str(args.source), "status", "--porcelain",
                                              "--untracked-files=normal"], text=True).strip()
            self.assertIs(report["build"]["dirty"], bool(status))
        self.assertEqual(report["gpu"], {"status": "not_probed", "reason": "disabled_by_request"})

    def test_version_and_help(self):
        result = self.run_cli("--version")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr, "")
        self.assertEqual(result.stdout.strip(), f'MapGyver {self.report("--no-gpu")["product"]["version"]}')
        for flags in [(), ("--help",)]:
            result = self.run_cli(*flags)
            self.assertEqual(result.returncode, 0)
            self.assertIn("Usage:", result.stdout)

    def test_capabilities_are_truthful_and_retained(self):
        report = self.report()
        self.assertEqual(report["gpu"], {"status": "unavailable", "reason": "gpu_probe_not_implemented"})
        self.assertEqual(set(report["features"]), {"platform", "media", "ui", "hardwareTests", "ndi"})
        for feature in report["features"].values():
            self.assertIs(feature["configured"], False)
            self.assertIs(feature["available"], False)
            self.assertTrue(feature["reason"])
        self.assertEqual(report["features"]["ndi"]["reason"], "owner_approval_required")

    def test_invalid_arguments_fail_without_success_payload(self):
        for flags in [("--json",), ("--no-gpu",), ("--bogus",), ("--version", "--json"),
                      ("--version", "--diagnose"), ("--help", "--version"),
                      ("--diagnose", "--diagnose"), ("--diagnose", "--no-gpu", "--no-gpu"),
                      ("--diagnose", "unexpected-file"), ("--serve",), ("--pipe-name",),
                      ("--pipe-name", "unused"), ("--serve", "--pipe-name", "../invalid"),
                      ("--serve", "--pipe-name", "one", "--pipe-name", "two"),
                      ("--serve", "--pipe-name", "one", "--diagnose"), ("--oscquery",),
                      ("--diagnose", "--oscquery"), ("--serve", "--pipe-name", "one", "--oscquery", "--oscquery")]:
            with self.subTest(flags=flags):
                result = self.run_cli(*flags)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, "")
                self.assertIn("Usage:", result.stderr)

    if sys.platform != "win32":
        def test_windows_control_transport_reports_unavailable(self):
            result = self.run_cli("--serve", "--pipe-name", "portable-check")
            self.assertEqual(result.returncode, 3)
            self.assertEqual(result.stdout, "")
            self.assertIn("unavailable on this platform", result.stderr)

    def test_schema_rejects_false_types_and_missing_identity(self):
        schema = json.loads((args.source / "schemas/diagnostics-v1.schema.json").read_text(encoding="utf-8"))
        for field, bad in [("schemaVersion", True), ("platform", {}), ("features", {}), ("build", {})]:
            with self.subTest(field=field):
                report = self.report("--no-gpu")
                report[field] = bad
                with self.assertRaises(AssertionError):
                    validate(report, schema, schema)

if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0], *remaining])
