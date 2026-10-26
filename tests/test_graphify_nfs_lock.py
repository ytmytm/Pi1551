#!/usr/bin/env python3
"""Run with .venv/bin/python after tools/patch_graphify_nfs_lock.py."""
import errno
import fcntl
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from graphify.watch import _rebuild_lock

REAL_FLOCK = fcntl.flock


def nfs_without_locks(fd, flags):
    if Path(os.readlink(f"/proc/self/fd/{fd}")).name == ".rebuild.lock":
        raise OSError(errno.ENOLCK, "No locks available")
    return REAL_FLOCK(fd, flags)


class RebuildLockTest(unittest.TestCase):
    def test_nfs_fallback_excludes_second_rebuild(self):
        with tempfile.TemporaryDirectory() as directory, patch("fcntl.flock", side_effect=nfs_without_locks):
            out = Path(directory)
            with _rebuild_lock(out, blocking=True) as acquired:
                self.assertTrue(acquired)
                self.assertEqual((out / ".rebuild.lock").read_text(), f"{os.getpid()}\n")
                with _rebuild_lock(out) as second:
                    self.assertFalse(second)
                self.assertTrue((out / ".rebuild.lock").exists())
            self.assertFalse((out / ".rebuild.lock").exists())
            with _rebuild_lock(out) as acquired:
                self.assertTrue(acquired)

    def test_nfs_fallback_releases_after_exception(self):
        with tempfile.TemporaryDirectory() as directory, patch("fcntl.flock", side_effect=nfs_without_locks):
            out = Path(directory)
            with self.assertRaisesRegex(RuntimeError, "rebuild failed"):
                with _rebuild_lock(out) as acquired:
                    self.assertTrue(acquired)
                    raise RuntimeError("rebuild failed")
            self.assertFalse((out / ".rebuild.lock").exists())
            with _rebuild_lock(out) as acquired:
                self.assertTrue(acquired)

    def test_other_errors_propagate(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch("fcntl.flock", side_effect=OSError(errno.EACCES, "Denied")):
                with self.assertRaises(OSError) as caught:
                    with _rebuild_lock(Path(directory)):
                        self.fail("Unexpected lock acquisition")
                self.assertEqual(caught.exception.errno, errno.EACCES)

    def test_normal_lock_still_excludes_second_rebuild(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            with _rebuild_lock(out) as acquired:
                self.assertTrue(acquired)
                with _rebuild_lock(out) as second:
                    self.assertFalse(second)
            self.assertFalse((out / ".rebuild.lock").exists())


if __name__ == "__main__":
    unittest.main()
