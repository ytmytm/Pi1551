#!/usr/bin/env python3
"""Apply the local graphify ENOLCK workaround; rerun after package upgrades."""
from importlib.util import find_spec
from pathlib import Path

MARKER = "# Pi1551: use a host-local lock when NFS has no lock service."


def patch():
    spec = find_spec("graphify.watch")
    if spec is None or spec.origin is None:
        raise SystemExit("Install graphifyy in this Python environment first.")
    path = Path(spec.origin)
    source = path.read_text()
    if MARKER in source:
        print("graphify NFS lock workaround already installed:", path)
        return
    replacements = [
        ('    acquired = False\n    try:\n        flags = fcntl.LOCK_EX',
         '    lock_fh = fh\n    acquired = False\n    try:\n        flags = fcntl.LOCK_EX'),
        ('            fcntl.flock(fh.fileno(), flags)\n', '''            try:
                fcntl.flock(fh.fileno(), flags)
            except OSError as error:
                import errno
                if error.errno != errno.ENOLCK:
                    raise
                # Pi1551: use a host-local lock when NFS has no lock service.
                # Keep its inode after unlocking, so waiters share one lock.
                import hashlib
                local_dir = Path("/tmp") / f"graphify-locks-{os.getuid()}"
                local_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
                key = hashlib.sha256(os.fsencode(out_dir.resolve())).hexdigest()
                lock_fh = open(local_dir / (key + ".lock"), "a+", encoding="utf-8")
                fcntl.flock(lock_fh.fileno(), flags)
'''),
        ('                fcntl.flock(fh.fileno(), fcntl.LOCK_UN)\n',
         '                fcntl.flock(lock_fh.fileno(), fcntl.LOCK_UN)\n'),
        ('        fh.close()\n        # Signal "rebuild done"',
         '        if lock_fh is not fh:\n            lock_fh.close()\n        fh.close()\n        # Signal "rebuild done"'),
    ]
    for old, new in replacements:
        if source.count(old) != 1:
            raise SystemExit("graphify lock implementation changed; review patch before applying: " + path.as_posix())
        source = source.replace(old, new, 1)
    compile(source, str(path), "exec")
    path.write_text(source)
    print("Installed graphify NFS lock workaround:", path)


if __name__ == "__main__":
    patch()
