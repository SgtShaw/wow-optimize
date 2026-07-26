#!/usr/bin/env python3
"""Fails if the launcher offers a switch the DLL never reads.

Four such toggles have shipped: LuaSNewLstrFast, CombatTextCoalescer,
FastMemsetOpt and FastStrnicmpOpt. Each wrote its key to wow_optimize.ini and
each was read by nothing, so the hook installed whatever the user chose. Two of
them were in a bisection list handed to a tester, which is how they were found -
the round produced no signal because half the switches were inert.

The check is mechanical: every key the launcher writes must appear in a
GetPrivateProfileIntA call in config.cpp. Run from the repo root.
"""

import re
import sys
import io
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAUNCHER = os.path.join(ROOT, "src", "launcher", "Launcher.cs")
CONFIG = os.path.join(ROOT, "src", "core", "config.cpp")


def main():
    launcher = io.open(LAUNCHER, encoding="utf-8", errors="surrogateescape").read()
    config = io.open(CONFIG, encoding="utf-8", errors="surrogateescape").read()

    toggles = re.findall(r'new SettingItem\("([^"]+)",\s*"([^"]+)"', launcher)
    if not toggles:
        print("check_toggles: found no toggles in Launcher.cs - parser out of date")
        return 2

    unread = [
        (section, key)
        for section, key in toggles
        if not re.search(r'GetPrivateProfileIntA\(\s*"[^"]+"\s*,\s*"%s"' % re.escape(key), config)
    ]

    print("check_toggles: %d launcher toggles" % len(toggles))
    if not unread:
        print("check_toggles: every toggle is read by the DLL")
        return 0

    print("check_toggles: %d toggle(s) the DLL never reads:" % len(unread))
    for section, key in unread:
        print("    [%s] %s" % (section, key))
    print("A switch that is not read does nothing, and misleads whoever flips it.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
