"""Strips the ELF executables and shared objects in a list of files, for //tools/runfiles_tar.

Usage: strip_elf.py STRIP MODE OUT_DIR MANIFEST

STRIP is the strip tool (llvm-strip). MODE is `all` (drop the symbol tables and debug info) or
`debug` (drop only debug info, keeping function names for backtraces). MANIFEST is a JSON list of
[src, dest] pairs; for each src that is an ELF executable or shared object the stripped file is
written to OUT_DIR/dest. A src that is a directory (a tree artifact) has each ELF file in it written
to OUT_DIR/dest/<its path in src>, skipping relative symlinks, which the packer keeps as symlinks.
Everything else -- scripts, data, object files, archives -- is skipped, not copied, so the packer
falls back to the original for any dest that isn't in OUT_DIR.

Stripping keeps .dynsym, .dynamic and the unwind tables, so the result links and loads exactly as
the original did.
"""

import json
import os
import struct
import subprocess
import sys

FLAGS = {"all": ["--strip-all"], "debug": ["--strip-debug"]}
ET_EXEC, ET_DYN = 2, 3


def is_elf_image(path):
    """True for an ELF executable or shared object (not a relocatable .o, not an archive)."""
    with open(path, "rb") as f:
        head = f.read(18)
    if len(head) < 18 or head[:4] != b"\x7fELF":
        return False
    order = "<" if head[5] == 1 else ">"
    return struct.unpack_from(order + "H", head, 16)[0] in (ET_EXEC, ET_DYN)


def main():
    strip, mode, out_dir, manifest = sys.argv[1:5]
    with open(manifest) as f:
        pairs = json.load(f)
    for src, dest in pairs:
        if os.path.isdir(src):
            for root, _, names in os.walk(src, followlinks=True):
                for name in names:
                    path = os.path.join(root, name)
                    if os.path.islink(path) and not os.path.isabs(os.readlink(path)):
                        continue
                    rel = os.path.relpath(path, src)
                    strip_one(strip, mode, path, os.path.join(out_dir, dest, rel))
        else:
            strip_one(strip, mode, src, os.path.join(out_dir, dest))


def strip_one(strip, mode, src, out):
    if not is_elf_image(src):
        return
    os.makedirs(os.path.dirname(out), exist_ok=True)
    subprocess.run([strip, *FLAGS[mode], "-o", out, src], check=True)


if __name__ == "__main__":
    main()
