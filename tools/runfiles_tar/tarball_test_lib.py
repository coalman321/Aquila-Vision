"""Shared harness for a `//<package>/tarball_test.py`: unpack an archive built by
`runfiles_tar_pair` and run binaries from it the way a host would.

Nothing here inherits Bazel's environment (no LD_LIBRARY_PATH, RUNFILES_DIR, AMENT_PREFIX_PATH,
RMW_IMPLEMENTATION) and the working directory is not inside the unpacked tree, so everything a
binary finds, it finds through the launcher in bin/ and the runfiles tree beside it. That is what
fails when the archive is missing the runfiles symlinks (lib/, share/) that ament_index publishes
-- a plain pkg_tar(include_runfiles = True) drops them -- or a launcher stops exporting what the
binary needs.

A package's tarball_test.py subclasses TarballTestCase once per archive (setting PACKAGE and
VARIANT, and wrapping the class in `only_for(VARIANT)` so testing one archive doesn't build the
other), adds whatever run-it checks are specific to the binaries that archive packages, and mixes
in ReleaseArchiveTests or DebugArchiveTests for the stripped/debug-info checks.
"""
import os
import re
import signal
import stat
import struct
import subprocess
import tarfile
import tempfile
import threading
import time
import unittest

import ros_test_domain
from python.runfiles import runfiles

STARTUP_TIMEOUT_S = 60
SHUTDOWN_TIMEOUT_S = 15

# The libraries an archive may take from the host: glibc's, which the toolchain links against
# (glibc 2.17 or newer) rather than bundling.
HOST_LIBRARIES = frozenset([
    "libc.so.6",
    "libdl.so.2",
    "libm.so.6",
    "libpthread.so.0",
    "libresolv.so.2",
    "librt.so.1",
])


def elf_section_names(path):
    """Section names of an ELF executable or shared object, or None for any other file."""
    with open(path, "rb") as f:
        head = f.read(64)
        if len(head) < 64 or head[:4] != b"\x7fELF" or head[4] != 2:  # 64-bit only
            return None
        order = "<" if head[5] == 1 else ">"
        if struct.unpack_from(order + "H", head, 16)[0] not in (2, 3):  # ET_EXEC, ET_DYN
            return None
        shoff = struct.unpack_from(order + "Q", head, 0x28)[0]
        entsize, count, strndx = struct.unpack_from(order + "HHH", head, 0x3A)
        if not shoff:
            return []
        f.seek(shoff)
        table = f.read(entsize * count)
        sections = [struct.unpack_from(order + "IIQQQQIIQQ", table, i * entsize) for i in range(count)]
        f.seek(sections[strndx][4])
        names = f.read(sections[strndx][5])
    return [names[s[0]:names.index(b"\0", s[0])].decode() for s in sections]


def only_for(variant):
    return unittest.skipUnless(os.environ.get("TARBALL_VARIANT") == variant, f"{variant} archive only")


def _set_writable(top, writable):
    """Adds or removes write permission on every directory under (and including) `top`."""
    for base, _, _ in os.walk(top):
        mode = os.stat(base).st_mode
        os.chmod(base, mode | stat.S_IWUSR if writable else mode & ~0o222)


class TarballTestMixin:
    """Unpacks `PACKAGE`'s tarball_<VARIANT> and runs binaries from it outside the unpacked tree.

    Not a unittest.TestCase itself -- mix it into one with `unittest.TestCase` (see
    ReleaseArchiveTests/DebugArchiveTests below) only at the leaf classes a package's
    tarball_test.py defines. unittest.main()'s default loader runs every TestCase subclass it can
    see by name in a test module's namespace, including ones merely imported; a shared base that
    inherited TestCase directly would get collected and run on its own, with PACKAGE/VARIANT unset.

    Subclasses set PACKAGE (the archive's `package_dir`, e.g. "basin") and VARIANT ("release" or
    "debug"), plus BAZEL_PACKAGE when the `tarball_release`/`tarball_debug` targets live in a bazel
    package other than PACKAGE (e.g. "myapp/app"; it defaults to PACKAGE).
    """

    PACKAGE = None
    BAZEL_PACKAGE = None
    VARIANT = None

    @classmethod
    def setUpClass(cls):
        workspace = os.environ.get("TEST_WORKSPACE", "_main")
        bazel_package = cls.BAZEL_PACKAGE or cls.PACKAGE
        tar = runfiles.Create().Rlocation(f"{workspace}/{bazel_package}/tarball_{cls.VARIANT}.tar.gz")
        assert tar and os.path.exists(tar), f"{bazel_package} {cls.VARIANT} tarball not in runfiles"
        cls._tmp = tempfile.TemporaryDirectory(dir=os.environ.get("TEST_TMPDIR"))
        cls.root = cls._tmp.name
        with tarfile.open(tar) as t:
            cls.members = t.getmembers()
            t.extractall(cls.root, filter="tar")
        cls.prefix = os.path.join(cls.root, cls.PACKAGE)
        # Deployed archives sit on a read-only rootfs (and are owned by someone other than the
        # service user), so nothing may write into the unpacked tree at runtime -- aspect_rules_py's
        # py_binary launcher, which rebuilds its venv under the runfiles on every start, is what this
        # catches. HOME stays outside it, in `root`.
        _set_writable(cls.prefix, False)
        # Concurrent test processes on one DDS domain deadlock each other's node construction. This
        # class starts its processes one at a time, so one claimed domain serves all of them.
        cls.domain = ros_test_domain.claim()

    @classmethod
    def tearDownClass(cls):
        cls.domain.close()
        _set_writable(cls.prefix, True)
        cls._tmp.cleanup()

    def start(self, name, *args):
        # HOME is only there because rcl needs somewhere to put logs.
        proc = subprocess.Popen(
            [os.path.join(self.prefix, "bin", name), *args],
            cwd=self.root,
            env=self.domain.env({"PATH": "/usr/bin:/bin", "HOME": self.root}),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        lines = []

        def drain():
            with proc.stdout:
                lines.extend(proc.stdout)

        threading.Thread(target=drain, daemon=True).start()
        self.addCleanup(self.stop, proc)
        return proc, lines

    def stop(self, proc):
        if proc.poll() is None:
            proc.send_signal(signal.SIGINT)
            try:
                proc.wait(SHUTDOWN_TIMEOUT_S)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

    def wait_for(self, proc, lines, pattern):
        deadline = time.monotonic() + STARTUP_TIMEOUT_S
        while time.monotonic() < deadline:
            if any(re.search(pattern, line) for line in list(lines)):
                return
            if proc.poll() is not None:
                break
            time.sleep(0.1)
        self.fail(f"never saw /{pattern}/ (exit={proc.poll()}); output:\n" + "".join(lines))

    def elf_files(self):
        for base, _, names in os.walk(os.path.join(self.prefix, "runfiles")):
            for name in names:
                path = os.path.join(base, name)
                if not os.path.islink(path) and (sections := elf_section_names(path)) is not None:
                    yield path, sections

    def test_layout(self):
        # The archive carries the runfiles symlink trees, not just the files.
        main = os.path.join(self.prefix, "runfiles", "_main")
        for path in ("lib/librmw_fastrtps_cpp.so", "share/ament_index", "bin"):
            self.assertTrue(os.path.exists(os.path.join(main, path)), path)

    def test_no_symlink_leaves_the_tree(self):
        # An absolute target is a path on the build machine (the sandbox stages a tree artifact's
        # files as symlinks into the execroot), which won't exist where the archive is unpacked.
        # Checks the archive's members, not the unpacked tree, which running binaries adds to.
        for member in self.members:
            if member.issym():
                self.assertFalse(os.path.isabs(member.linkname), member.name)
                path = os.path.join(self.root, member.name)
                self.assertTrue(os.path.exists(path), f"{member.name} -> {member.linkname} dangles")

    def test_ament_executables_load_their_libraries_from_the_tree(self):
        # A launch file runs lib/<package>/<executable>, which in the archive is a copy, so its
        # $ORIGIN-relative RUNPATH points nowhere and every library it links has to come from lib/
        # (the launcher's LD_LIBRARY_PATH) instead. One that isn't there either fails to load or,
        # worse, quietly resolves to whatever the host has. Only glibc may come from the host.
        lib = os.path.join(self.prefix, "runfiles", "_main", "lib")
        env = {"PATH": "/usr/bin:/bin", "LD_LIBRARY_PATH": lib}
        for package in sorted(os.listdir(lib)):
            package_dir = os.path.join(lib, package)
            if os.path.islink(package_dir) or not os.path.isdir(package_dir):
                continue
            for name in sorted(os.listdir(package_dir)):
                path = os.path.join(package_dir, name)
                if elf_section_names(path) is None or not os.access(path, os.X_OK):
                    continue
                with self.subTest(executable=f"{package}/{name}"):
                    # Not check=True: ldd exits 1 for a statically linked executable, with no output.
                    out = subprocess.run(["ldd", path], env=env, capture_output=True, text=True).stdout
                    for line in out.splitlines():
                        soname, _, resolved = line.strip().partition(" => ")
                        resolved = resolved.split(" (")[0]
                        if not resolved or soname in HOST_LIBRARIES:
                            continue  # linux-vdso, the dynamic loader
                        self.assertTrue(
                            os.path.realpath(resolved).startswith(os.path.realpath(self.prefix) + os.sep),
                            f"{soname} => {resolved}",
                        )


class ReleaseArchiveTests:
    """Mix into a `strip = "all"` archive's TestCase (alongside TarballTestCase)."""

    def test_every_elf_file_is_stripped(self):
        checked = 0
        for path, sections in self.elf_files():
            checked += 1
            self.assertNotIn(".symtab", sections, path)
            self.assertFalse([n for n in sections if n.startswith(".debug")], path)
        self.assertGreater(checked, 100, "found suspiciously few ELF files")


class DebugArchiveTests:
    """Mix into a `strip = "none"` archive's TestCase (alongside TarballTestCase).

    Subclasses set FIRST_PARTY_DEBUG_PATHS: paths, relative to `self.prefix`, of first-party
    binaries that must carry DWARF, not just a symbol table.
    """

    FIRST_PARTY_DEBUG_PATHS = ()

    def test_symbols_are_kept(self):
        self.assertTrue(any(".symtab" in sections for _, sections in self.elf_files()))

    def test_first_party_code_carries_debug_info(self):
        # .bazelrc compiles //basin with -g and turns off link-time stripping; without both, the
        # DWARF is either never generated or thrown away at the link, and this archive would have
        # symbol tables and nothing to debug with.
        self.assertTrue(self.FIRST_PARTY_DEBUG_PATHS, "no FIRST_PARTY_DEBUG_PATHS set")
        for path in self.FIRST_PARTY_DEBUG_PATHS:
            sections = elf_section_names(os.path.join(self.prefix, path))
            self.assertIn(".debug_info", sections, path)
            self.assertIn(".debug_line", sections, path)
