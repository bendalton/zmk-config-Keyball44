#!/usr/bin/env python3
"""Kept for muscle memory: same as `tools/flash.py dongle [uf2]`."""
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
os.execv(sys.executable, [sys.executable, os.path.join(here, "flash.py"), "dongle", *sys.argv[1:]])
