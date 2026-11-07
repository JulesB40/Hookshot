#!/usr/bin/env python3
"""Safely reuse upstream's lifter. Original ExeFS is read-only; unpack only independent copies.

No NUP extraction here: upstream's general-purpose extraction and desktop make_game
entry points have not been audited for Android use. No game files are included in the APK.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
NAMES = ("rtld", "main", *(f"subsdk{i}" for i in range(10)), "sdk", "main.npdm")


def snapshot_exefs(source, workspace_parent):
    source = Path(source).resolve(strict=True)
    parent = Path(workspace_parent).resolve(strict=True)
    if not source.is_dir() or not parent.is_dir():
        raise ValueError("Source and workspace parent must be directories")
    if source == parent or source in parent.parents or parent in source.parents:
        raise ValueError("The workspace and original dump must have separate directory trees")
    # All outputs are under a newly created directory, mode 0700; no cleanup, hard
    # links, symlinks, moves, chmods or writes to source paths.
    output = Path(tempfile.mkdtemp(prefix="hookshot-lift-", dir=parent))
    copied = output / "exefs"
    copied.mkdir(mode=0o700)
    hashes = {}
    source_fd = os.open(source, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        for name in NAMES:
            try:
                fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=source_fd)
            except FileNotFoundError:
                continue
            with os.fdopen(fd, "rb") as src:
                before = os.fstat(src.fileno())
                if not stat.S_ISREG(before.st_mode):
                    raise ValueError(f"Not a regular module: {name}")
                digest = hashlib.sha256()
                with (copied / name).open("xb") as dst:
                    while chunk := src.read(1024 * 1024):
                        digest.update(chunk)
                        dst.write(chunk)
                after = os.fstat(src.fileno())
                if (before.st_size, before.st_mtime_ns, before.st_ino) != (after.st_size, after.st_mtime_ns, after.st_ino):
                    raise ValueError(f"Original module changed during read: {name}")
                hashes[name] = digest.hexdigest()
    finally:
        os.close(source_fd)
    if "main" not in hashes or "sdk" not in hashes:
        raise ValueError("Expected decrypted ExeFS modules main and sdk")
    (output / "original-sha256.json").write_text(json.dumps(hashes, indent=2) + "\n")
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exefs", type=Path, required=True)
    parser.add_argument("--workspace-parent", type=Path, required=True, help="Existing directory in Termux private home, outside dump")
    parser.add_argument("--lift", action="store_true", help="Unpack copies, then run existing standalone C lifter")
    args = parser.parse_args()
    output = snapshot_exefs(args.exefs, args.workspace_parent)
    print(f"Independent snapshot: {output}", flush=True)
    if not args.lift:
        return
    env = dict(os.environ)
    # The lifter reads these, including at import time. Keep all generated data
    # isolated from existing builds and never inherit a user-provided OOT_WORK.
    env.update(OOT_WORK=str(output / "work"), OOT_EXEFS=str(output / "exefs"))
    env.pop("OOT_DUMP", None)
    env.pop("OOT_ROMFS", None)
    for script, arguments in (("unpack.py", [str(output / "exefs")]),
                              ("lift.py", ["--standalone", "--write", "--exefs", str(output / "exefs"), "--work", str(output / "work")])):
        subprocess.run([sys.executable, "-I", str(ROOT / "nx2" / script), *arguments], env=env, cwd=ROOT, check=True)
    print("C emitted using the existing lifter. Android game runtime/linking is not implemented.")


if __name__ == "__main__":
    main()
