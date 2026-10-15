#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prove source identity refresh, ignored artifacts and archive identity isolation."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

parser = argparse.ArgumentParser()
parser.add_argument("--script", required=True, type=Path)
args, remaining = parser.parse_known_args()

class Metadata(unittest.TestCase):
    def git(self, root, *flags):
        return subprocess.check_output(["git", "-C", str(root), "-c", "user.name=MapGyver tests",
                                        "-c", "user.email=tests@example.invalid", "-c", "commit.gpgsign=false",
                                        *flags], text=True, stderr=subprocess.STDOUT).strip()

    def generate(self, root, output):
        subprocess.run([sys.executable, str(args.script), "--root", str(root), "--output", str(output)],
                       check=True, timeout=10)
        return output.read_text(encoding="utf-8")

    def test_rebuild_tracks_edits_commits_and_ignored_outputs(self):
        with tempfile.TemporaryDirectory(prefix="mapgyver metadata ") as temp:
            root = Path(temp)
            self.git(root, "init", "-b", "main")
            (root / ".gitignore").write_text("/out/\n", encoding="utf-8")
            owned = root / "owned.txt"
            owned.write_text("first\n", encoding="utf-8")
            self.git(root, "add", ".")
            self.git(root, "commit", "-m", "first")
            first_sha = self.git(root, "rev-parse", "HEAD")
            output = root / "out" / "source_revision.hpp"
            initial = self.generate(root, output)
            self.assertIn(first_sha, initial)
            self.assertIn("source_known = true", initial)
            self.assertIn("source_dirty = false", initial)
            original_time = output.stat().st_mtime_ns
            (root / "out" / "generated.txt").write_text("ignored\n", encoding="utf-8")
            self.assertEqual(self.generate(root, output), initial)
            self.assertEqual(output.stat().st_mtime_ns, original_time, "unchanged metadata rewrote its file")
            owned.write_text("second\n", encoding="utf-8")
            self.assertIn("source_dirty = true", self.generate(root, output))
            self.git(root, "add", "owned.txt")
            self.git(root, "commit", "-m", "second")
            current = self.generate(root, output)
            self.assertIn(self.git(root, "rev-parse", "HEAD"), current)
            self.assertNotIn(first_sha, current)
            self.assertIn("source_dirty = false", current)

    def test_archive_does_not_inherit_parent_repository_identity(self):
        with tempfile.TemporaryDirectory(prefix="mapgyver archive ") as temp:
            parent = Path(temp)
            self.git(parent, "init", "-b", "main")
            (parent / "owned.txt").write_text("parent\n", encoding="utf-8")
            self.git(parent, "add", ".")
            self.git(parent, "commit", "-m", "parent")
            archive = parent / "archive"
            archive.mkdir()
            content = self.generate(archive, archive / "generated.hpp")
            self.assertIn('source_revision = ""', content)
            self.assertIn("source_known = false", content)
            self.assertNotIn(self.git(parent, "rev-parse", "HEAD"), content)

if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0], *remaining])
