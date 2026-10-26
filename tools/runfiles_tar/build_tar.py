"""Writes the gzipped tarball for //tools/runfiles_tar from a JSON manifest.

Usage: build_tar.py OUT MANIFEST PACKAGE_DIR COMPRESSION_LEVEL

MANIFEST is a JSON list of {"dest", "src", "kind", "mode", "stripped_root"} entries, where kind is
  file     a regular file (or a symlink that resolves inside the build), packed by content
  symlink  an unresolved symlink artifact, packed as a symlink with its target text unchanged
           (aspect_rules_py's venvs use targets relative to the runfiles layout, which the tarball
           reproduces)
  tree     a directory artifact, packed recursively (an absolute symlink in it -- which is how the
           sandbox stages a tree artifact's files -- is packed as what it points to)
An entry with a `stripped_root` is packed from <stripped_root>/<dest> instead of `src` when that file
exists, file by file for a tree (see strip_elf.py, which only writes the ELF files it stripped).
Entries with identical contents and mode (a library reached under both lib/ and _solib_aarch64/,
python/python3/python3.12, ...) are stored once and the rest are hard links to it.
Ownership and timestamps are normalized -- including the gzip header's, which would otherwise carry
the time and the output's name -- so the output depends only on the inputs.
"""

import collections
import gzip
import hashlib
import json
import os
import sys
import tarfile


def _info(name, type_, mode, size=0, linkname=""):
    info = tarfile.TarInfo(name)
    info.type = type_
    info.mode = mode
    info.size = size
    info.linkname = linkname
    info.mtime = 0
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    return info


def _walk_tree(items, dest, src, mode, stripped_root):
    """Appends the contents of directory `src` to `items` under `dest`.

    As for a file entry, a file is packed from <stripped_root>/<its dest> when that exists.

    A relative symlink in the tree (libfoo.so -> libfoo.so.1) is kept as a symlink. An absolute one
    is packed as what it points to: the sandbox stages every file of a tree artifact input as a
    symlink to its absolute path in the execroot, which won't exist where the archive is unpacked.
    """
    for root, dirs, files in os.walk(src):
        rel = os.path.relpath(root, src)
        base = dest if rel == "." else dest + "/" + rel
        for name in sorted(dirs + files):
            path = os.path.join(root, name)
            if os.path.islink(path) and not os.path.isabs(os.readlink(path)):
                items.append((base + "/" + name, "symlink", path, 0o777))
            elif os.path.isfile(path):
                stripped = stripped_root and os.path.join(stripped_root, base, name)
                if stripped and os.path.isfile(stripped):
                    path = stripped
                items.append((base + "/" + name, "file", path, mode))
            elif os.path.islink(path) and os.path.isdir(path):
                # os.walk doesn't descend into symlinked directories.
                _walk_tree(items, base + "/" + name, os.path.realpath(path), mode, stripped_root)
            elif os.path.islink(path):
                sys.exit("build_tar: %s is a dangling symlink to %s" % (path, os.readlink(path)))


def main():
    out, manifest = sys.argv[1], sys.argv[2]
    prefix = sys.argv[3].strip("/")
    level = int(sys.argv[4])
    with open(manifest) as f:
        entries = json.load(f)

    # (dest, kind, src, mode), with trees expanded into their contents.
    items = []
    for e in entries:
        dest = e["dest"].strip("/")
        if e["kind"] == "tree":
            _walk_tree(items, dest, e["src"], e["mode"], e.get("stripped_root"))
        else:
            if e["kind"] == "symlink" and os.path.isabs(os.readlink(e["src"])):
                sys.exit("build_tar: %s is a symlink to the absolute path %s, which won't exist "
                         "where the archive is unpacked" % (e["src"], os.readlink(e["src"])))
            src = e["src"]
            if e.get("stripped_root"):
                stripped = os.path.join(e["stripped_root"], dest)
                if os.path.isfile(stripped):
                    src = stripped
            items.append((dest, e["kind"], src, e["mode"]))
    if prefix:
        items = [(prefix + "/" + dest, kind, src, mode) for dest, kind, src, mode in items]
    items.sort(key=lambda i: i[0])

    dirs = set()
    for dest, _, _, _ in items:
        parts = dest.split("/")[:-1]
        for n in range(1, len(parts) + 1):
            dirs.add("/".join(parts[:n]))

    # Only files whose size is shared by another can be duplicates, so only those get hashed.
    sizes = collections.Counter(os.stat(src).st_size for _, kind, src, _ in items if kind == "file")

    def digest(path):
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for block in iter(lambda: f.read(1 << 20), b""):
                h.update(block)
        return h.digest()

    stored = {}  # (mode, contents) -> name of the entry that holds them
    with open(out, "wb") as raw, gzip.GzipFile(
        filename="", mode="wb", compresslevel=level, fileobj=raw, mtime=0
    ) as gz, tarfile.open(fileobj=gz, mode="w", format=tarfile.GNU_FORMAT) as tar:
        for d in sorted(dirs):
            tar.addfile(_info(d, tarfile.DIRTYPE, 0o755))
        for name, kind, src, mode in items:
            if kind == "symlink":
                tar.addfile(_info(name, tarfile.SYMTYPE, 0o777, linkname=os.readlink(src)))
                continue
            size = os.stat(src).st_size
            if size and sizes[size] > 1:
                key = (mode, size, digest(src))
                if key in stored:
                    tar.addfile(_info(name, tarfile.LNKTYPE, mode, linkname=stored[key]))
                    continue
                stored[key] = name
            with open(src, "rb") as f:
                tar.addfile(_info(name, tarfile.REGTYPE, mode, size=size), f)


if __name__ == "__main__":
    main()
