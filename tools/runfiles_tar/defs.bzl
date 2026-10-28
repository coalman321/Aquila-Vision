"""Packs Bazel-built executables into one tarball that runs on a host, outside Bazel.

`pkg_tar(include_runfiles = True)` is not enough for that: it only packs a target's runfiles
`files`, and drops the runfiles `symlinks` -- which is where `ament_index` publishes `lib/`
(the RMW and typesupport libraries rcutils dlopen()s by name), `share/` (the ament resource index
and package data) and `bin/`. An executable unpacked from such a tar can't even find its RMW.

`runfiles_tar` instead merges the complete runfiles of every listed binary (files, symlinks and
root_symlinks) into ONE shared tree, so a library that N binaries depend on is stored once rather
than N times, and adds a `bin/<name>` launcher per binary. Unpacking it gives

    <package_dir>/bin/<name>          launcher: sets the environment, execs the real binary
    <package_dir>/runfiles/_main/     the workspace's runfiles, and the ROS "install prefix"
                                      (its lib/ and share/ come from the ament_index targets)
    <package_dir>/runfiles/<repo>/    every external repository's runfiles

With `strip = "all"` (or "debug") every ELF executable and shared library in the tree is stripped as it
is packed -- including the third-party libraries, which is where nearly all the symbols are, and
which a per-binary strip rule wouldn't reach. `runfiles_tar_pair` builds a stripped `<name>_release`
and an unstripped `<name>_debug` from the same binaries, as separate targets.

The launcher exports what `bazel run` would: RUNFILES_DIR, and AMENT_PREFIX_PATH and
LD_LIBRARY_PATH pointing at `runfiles/_main`, plus the `env` defaults. It doesn't `cd`, so
relative paths on the command line keep meaning what the caller thinks they mean.
"""

# aspect_rules_py's py_binary transitions the whole subgraph under it to a configuration with its
# python_version flag set (see py/private/transitions.bzl there), which makes Bazel compile every C++
# library under a py_binary a second time in that configuration -- a copy that is functionally
# identical but not byte-identical, and that can't share a path with the first in one merged tree.
# Putting the same transition on every binary here builds them all in that one configuration, and
# makes the py_binary's own transition a no-op.
_PYTHON_VERSION_FLAG = "@aspect_rules_py//py/private/interpreter:python_version"
_RULES_PYTHON_VERSION_FLAG = "@rules_python//python/config_settings:python_version"

def _one_python_version_impl(settings, _attr):
    return {
        _PYTHON_VERSION_FLAG: settings[_PYTHON_VERSION_FLAG] or settings[_RULES_PYTHON_VERSION_FLAG],
    }

_one_python_version = transition(
    implementation = _one_python_version_impl,
    inputs = [_PYTHON_VERSION_FLAG, _RULES_PYTHON_VERSION_FLAG],
    outputs = [_PYTHON_VERSION_FLAG],
)

# Bazel makes every action output read-only and executable, so there is nothing to infer from.
_FILE_MODE = 0o555

# Files are dealt into this many strip actions by a hash of their path: few enough to keep the
# action count sane, many enough that touching one binary re-strips 1/16th of the tree rather than
# all of it (the third-party libraries, most of the bytes, come from the cache).
_STRIP_SHARDS = 16

def _runfiles_path(ctx, f):
    # A runfiles tree is <root>/<repo>/<path>. `short_path` is "<pkg>/<file>" for the main
    # repository and "../<repo>/<pkg>/<file>" for an external one.
    if f.short_path.startswith("../"):
        return f.short_path[3:]
    return ctx.workspace_name + "/" + f.short_path

def _add(candidates, dest, f):
    files = candidates.setdefault(dest, [])
    if not [x for x in files if x.path == f.path]:
        files.append(f)

def _collect(ctx):
    """Merges every binary's runfiles into {dest: [File, ...]} plus their repo mappings."""
    candidates = {}
    repo_mappings = []
    empty_names = []
    for binary in ctx.attr.binaries:
        rf = binary[DefaultInfo].default_runfiles
        exe = binary[DefaultInfo].files_to_run.executable

        for f in rf.files.to_list():
            _add(candidates, _runfiles_path(ctx, f), f)
        if exe:
            _add(candidates, _runfiles_path(ctx, exe), exe)

        # ament_index publishes lib/, share/ and bin/ through these.
        for entry in rf.symlinks.to_list():
            _add(candidates, ctx.workspace_name + "/" + entry.path, entry.target_file)
        for entry in rf.root_symlinks.to_list():
            if entry.path == "_repo_mapping":
                repo_mappings.append(entry.target_file)
            else:
                _add(candidates, entry.path, entry.target_file)

        mapping = getattr(rf, "repo_mapping_manifest", None)
        if mapping:
            repo_mappings.append(mapping)
        empty_names += [ctx.workspace_name + "/" + p for p in rf.empty_filenames.to_list()]
    return candidates, repo_mappings, empty_names

def _kind(f):
    if f.is_directory:
        return "tree"
    if getattr(f, "is_symlink", False):
        return "symlink"
    return "file"

def _add_entry(entries, files, dest, f, kind, mode = _FILE_MODE):
    """Appends a manifest entry; `files` remembers the File behind each regular file's or tree's dest."""
    entries.append({"dest": dest, "src": f.path, "kind": kind, "mode": mode})
    if kind in ("file", "tree"):
        files[dest] = f

def _strip(ctx, entries, files):
    """Strips the ELF files among `files` in sharded actions; returns the output directories."""
    shards = {}
    for entry in entries:
        if entry["dest"] in files:
            shards.setdefault(hash(entry["dest"]) % _STRIP_SHARDS, []).append(entry)

    trees = []
    for shard, group in sorted(shards.items()):
        tree = ctx.actions.declare_directory("%s_stripped_%d" % (ctx.label.name, shard))
        manifest = ctx.actions.declare_file("%s_strip_%d.json" % (ctx.label.name, shard))
        ctx.actions.write(manifest, json.encode([[files[e["dest"]].path, e["dest"]] for e in group]))
        ctx.actions.run(
            executable = ctx.executable._strip_elf,
            tools = [ctx.file._strip],
            arguments = [ctx.file._strip.path, ctx.attr.strip, tree.path, manifest.path],
            inputs = [manifest] + [files[e["dest"]] for e in group],
            outputs = [tree],
            mnemonic = "StripElf",
            progress_message = "Stripping %s shard %d (%d files)" % (ctx.label.name, shard, len(group)),
        )
        for entry in group:
            entry["stripped_root"] = tree.path
        trees.append(tree)
    return trees

def _runfiles_tar_impl(ctx):
    candidates, repo_mappings, empty_names = _collect(ctx)
    for dest in list(candidates):
        if [x for x in ctx.attr.exclude if x in dest]:
            candidates.pop(dest)
    inputs = []
    entries = []
    files = {}

    if empty_names:
        # Empty marker files (e.g. Python __init__.py) are listed by name only.
        empty = ctx.actions.declare_file(ctx.label.name + "_empty")
        ctx.actions.write(empty, "")
        for dest in empty_names:
            _add(candidates, dest, empty)

    # The same destination can be reached through different Bazel artifacts -- typically a
    # library's real output and the _solib symlink Bazel makes when linking against it dynamically,
    # or (as with a py_console_script_binary's venv interpreter) a target reached from binaries that
    # Bazel happens to have split into different configurations for unrelated reasons (e.g. two
    # different cc_shared_library dynamic_deps closures), depending on which binary's ament_index
    # published it. That is only harmless if the two artifacts really are the same, so check that at
    # build time, and pack a verified copy.
    conflicts = {}
    symlink_conflicts = {}
    for dest, candidates_files in candidates.items():
        if len(candidates_files) == 1:
            inputs.append(candidates_files[0])
            _add_entry(entries, files, "runfiles/" + dest, candidates_files[0], _kind(candidates_files[0]))
            continue

        kinds = {_kind(f): True for f in candidates_files}
        if kinds == {"file": True}:
            conflicts[dest] = candidates_files
        elif kinds == {"symlink": True}:
            symlink_conflicts[dest] = candidates_files
        else:
            fail("runfiles_tar: <%s> is provided by several different artifacts, one of which is a directory and so can't be compared: %s" % (dest, [x.path for x in candidates_files]))

    if symlink_conflicts:
        lines = []
        merged_symlinks = []
        for dest, candidates_files in sorted(symlink_conflicts.items()):
            out = ctx.actions.declare_symlink(ctx.label.name + "_merged/" + dest)
            merged_symlinks.append(out)
            for path in [out.path] + [f.path for f in candidates_files]:
                if " " in path:
                    fail("runfiles_tar: can't merge a path containing a space: " + path)
            lines.append(" ".join([out.path] + [f.path for f in candidates_files]))
            _add_entry(entries, files, "runfiles/" + dest, out, "symlink")
        manifest = ctx.actions.declare_file(ctx.label.name + "_symlink_merge_manifest")
        ctx.actions.write(manifest, "\n".join(lines) + "\n")
        ctx.actions.run_shell(
            inputs = depset([manifest], transitive = [depset(f) for f in symlink_conflicts.values()]),
            outputs = merged_symlinks,
            arguments = [manifest.path],
            command = """
while read -r out first rest; do
  target=$(readlink "$first")
  mkdir -p "$(dirname "$out")"
  for other in $rest; do
    other_target=$(readlink "$other")
    if [ "$target" != "$other_target" ]; then
      echo "runfiles_tar: $out is reached through symlinks with different targets:" >&2
      echo "  $first -> $target" >&2
      echo "  $other -> $other_target" >&2
      exit 1
    fi
  done
  ln -s "$target" "$out"
done < "$1"
""",
            mnemonic = "MergeRunfilesSymlinks",
            progress_message = "Verifying %d merged runfiles symlinks" % len(symlink_conflicts),
        )
        inputs += merged_symlinks

    if conflicts:
        lines = []
        merged_files = []
        for dest, candidates_files in sorted(conflicts.items()):
            out = ctx.actions.declare_file(ctx.label.name + "_merged/" + dest)
            merged_files.append(out)
            for path in [out.path] + [f.path for f in candidates_files]:
                if " " in path:
                    fail("runfiles_tar: can't merge a path containing a space: " + path)
            lines.append(" ".join([out.path] + [f.path for f in candidates_files]))
            _add_entry(entries, files, "runfiles/" + dest, out, "file")
        manifest = ctx.actions.declare_file(ctx.label.name + "_merge_manifest")
        ctx.actions.write(manifest, "\n".join(lines) + "\n")
        ctx.actions.run_shell(
            inputs = depset([manifest], transitive = [depset(f) for f in conflicts.values()]),
            outputs = merged_files,
            arguments = [manifest.path],
            command = """
while read -r out first rest; do
  mkdir -p "$(dirname "$out")"
  cp -L "$first" "$out"
  for other in $rest; do
    if ! cmp -s "$first" "$other"; then
      echo "runfiles_tar: $out is reached through files with different contents:" >&2
      echo "  $first" >&2
      echo "  $other" >&2
      exit 1
    fi
  done
done < "$1"
""",
            mnemonic = "MergeRunfiles",
            progress_message = "Verifying %d merged runfiles" % len(conflicts),
        )
        inputs += merged_files

    # Each binary's _repo_mapping only lists the repositories in its own runfiles, so they differ.
    # Every line is a fact about global canonical names ("<from repo>,<apparent name>,<canonical
    # name>"), so their union is the mapping for the merged tree -- unless two binaries disagree
    # about what a name means, which would make the merge unsound.
    if repo_mappings:
        merged = ctx.actions.declare_file(ctx.label.name + "_repo_mapping")
        ctx.actions.run_shell(
            inputs = repo_mappings,
            outputs = [merged],
            arguments = [merged.path] + [f.path for f in repo_mappings],
            command = """
out="$1"; shift
cat "$@" | LC_ALL=C sort -u > "$out"
conflicts=$(cut -d, -f1,2 "$out" | uniq -d)
if [ -n "$conflicts" ]; then
  echo "runfiles_tar: binaries disagree on repo mapping for:" >&2
  echo "$conflicts" >&2
  exit 1
fi
""",
            mnemonic = "MergeRepoMapping",
        )
        inputs.append(merged)
        _add_entry(entries, {}, "runfiles/_repo_mapping", merged, "file")

    for name, script in _launchers(ctx).items():
        inputs.append(script)
        _add_entry(entries, {}, "bin/" + name, script, "file", mode = 0o755)

    if ctx.attr.strip != "none":
        inputs += _strip(ctx, entries, files)

    manifest = ctx.actions.declare_file(ctx.label.name + "_manifest.json")
    ctx.actions.write(manifest, json.encode(entries))
    out = ctx.actions.declare_file(ctx.label.name + ".tar.gz")
    ctx.actions.run(
        executable = ctx.executable._build_tar,
        arguments = [out.path, manifest.path, ctx.attr.package_dir, str(ctx.attr.compression_level)],
        inputs = depset(inputs + [manifest]),
        outputs = [out],
        mnemonic = "RunfilesTar",
        progress_message = "Packing %s (%d entries)" % (out.short_path, len(entries)),
    )
    return [DefaultInfo(files = depset([out]))]

_LAUNCHER = """#!/bin/sh
# Generated by //tools/runfiles_tar. Runs {name} from this unpacked tree.
set -e
self=$(readlink -f "$0")
root=$(dirname "$(dirname "$self")")/runfiles
export RUNFILES_DIR="$root"
export AMENT_PREFIX_PATH="$root/{main}${{AMENT_PREFIX_PATH:+:$AMENT_PREFIX_PATH}}"
export LD_LIBRARY_PATH="$root/{main}/lib${{LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}}"
{env}exec "$root/{binary}" "$@"
"""

def _launchers(ctx):
    env = ""
    for key, value in sorted(ctx.attr.env.items()):
        for s in [key, value]:
            for c in s.elems():
                if not (c.isalnum() or c in "_./:+-"):
                    fail("env %s=%s: only [A-Za-z0-9_./:+-] is allowed" % (key, value))
        env += 'export %s="${%s:-%s}"\n' % (key, key, value)

    scripts = {}
    for binary in ctx.attr.binaries:
        name = binary.label.name
        if name in scripts:
            fail("two binaries are both named %s" % name)
        script = ctx.actions.declare_file(ctx.label.name + "_launchers/" + name)
        ctx.actions.write(
            script,
            _LAUNCHER.format(
                name = binary.label,
                main = ctx.workspace_name,
                env = env,
                binary = _runfiles_path(ctx, binary[DefaultInfo].files_to_run.executable),
            ),
            is_executable = True,
        )
        scripts[name] = script
    return scripts

runfiles_tar = rule(
    implementation = _runfiles_tar_impl,
    doc = "Packs `binaries` and their complete runfiles into `<name>.tar.gz`, runnable on a host.",
    attrs = {
        "binaries": attr.label_list(
            mandatory = True,
            cfg = _one_python_version,
            doc = "Executable targets to bundle. Each gets a `bin/<label name>` launcher.",
        ),
        "env": attr.string_dict(
            doc = """Environment variable defaults exported by every launcher (`VAR=${VAR:-value}`,
            so a caller's own value wins). Values are limited to [A-Za-z0-9_./:+-].""",
        ),
        "package_dir": attr.string(
            doc = "Directory everything in the archive is placed under.",
        ),
        "exclude": attr.string_list(
            doc = """Files left out of the archive: any runfiles path (as in `runfiles/<repo>/<path>`,
            without the `runfiles/`) that contains one of these strings. For dead weight in a third-party
            repository that the binaries would otherwise drag along.""",
        ),
        "compression_level": attr.int(
            default = 6,
            values = [1, 2, 3, 4, 5, 6, 7, 8, 9],
            doc = """gzip level. 6 is within half a percent of 9's size at under a third of the
            build time on the basin tree; 1 is about 13% larger and about three times faster.""",
        ),
        "strip": attr.string(
            default = "none",
            values = ["none", "debug", "all"],
            doc = """What to strip from every ELF executable and shared library in the archive.
            `none` keeps everything; `debug` drops only debug info (backtraces keep function names);
            `all` also drops the symbol tables. .dynsym and the unwind tables stay in every mode,
            so stripped binaries link, load and unwind exactly as before.""",
        ),
        "_build_tar": attr.label(
            default = "//tools/runfiles_tar:build_tar",
            executable = True,
            cfg = "exec",
        ),
        "_strip": attr.label(
            default = "@llvm//tools:llvm-strip",
            allow_single_file = True,
            cfg = "exec",
        ),
        "_strip_elf": attr.label(
            default = "//tools/runfiles_tar:strip_elf",
            executable = True,
            cfg = "exec",
        ),
    },
)

def runfiles_tar_pair(name, binaries, **kwargs):
    """Builds a stripped release archive and an unstripped debug archive from the same binaries.

    Creates `<name>_release` (`strip = "all"`) and `<name>_debug` (`strip = "none"`), plus an alias
    `<name>` for the release one, so `bazel build //pkg:<name>` yields only
    `<name>_release.tar.gz`; the (larger, slower to build) debug archive is built only when asked
    for by name. Both are the same code at the same paths; the debug one keeps the symbol tables and
    whatever debug info the build configuration produced, so it can symbolize a crash in the release
    one.

    Args:
      name: Base name of the targets.
      binaries: Executable targets to bundle, as for `runfiles_tar`.
      **kwargs: Passed to both `runfiles_tar`s (`env`, `package_dir`, ...); `visibility` also goes
        on the alias.
    """
    runfiles_tar(name = name + "_release", binaries = binaries, strip = "all", **kwargs)
    runfiles_tar(name = name + "_debug", binaries = binaries, strip = "none", **kwargs)
    native.alias(
        name = name,
        actual = ":" + name + "_release",
        visibility = kwargs.get("visibility"),
    )
