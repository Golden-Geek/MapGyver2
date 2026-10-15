#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The stable gate succeeds only if every required job actually succeeded."""
import os
import sys

REQUIRED = ("PLAN_RESULT", "WINDOWS_RESULT", "PORTABLE_RESULT")

def main():
    bad = {name: os.environ.get(name) for name in REQUIRED if os.environ.get(name) != "success"}
    if bad:
        print("Required checks did not succeed:", bad, file=sys.stderr)
        return 1
    print("Every required CI prerequisite succeeded.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
