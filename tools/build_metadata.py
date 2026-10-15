#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Refresh build-time Git identity; write only when values change."""
import argparse
from pathlib import Path
import subprocess

def source_identity(root):
    def git(*args):
        return subprocess.run(["git", "-C", str(root), *args], check=True,
                              capture_output=True, text=True).stdout.strip()
    try:
        if Path(git("rev-parse", "--show-toplevel")).resolve() != root.resolve():
            return "", None
        return git("rev-parse", "HEAD"), bool(git("status", "--porcelain", "--untracked-files=normal"))
    except (OSError, subprocess.CalledProcessError):
        return "", None

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    revision, dirty = source_identity(args.root)
    content = ("// Generated; do not edit.\n#pragma once\n#include <string_view>\n"
               "namespace mapgyver {\n"
               f'inline constexpr std::string_view source_revision = "{revision}";\n'
               f'inline constexpr bool source_known = {str(dirty is not None).lower()};\n'
               f'inline constexpr bool source_dirty = {str(bool(dirty)).lower()};\n' + "}\n")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != content:
        args.output.write_text(content, encoding="utf-8")

if __name__ == "__main__":
    main()
