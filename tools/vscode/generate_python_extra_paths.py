#!/usr/bin/env python3
"""Regenerates .vscode/settings.json's python.analysis.extraPaths from the current Bazel graph.

Pylance can't see this workspace's third-party Python packages (rclpy, launch, pyyaml, ...)
because rules_python fetches them into Bazel's output base instead of a venv/site-packages
Pylance would find on its own. This script scans every first-party .py file for its top-level
imports, resolves each one against bazel-Aquila-Vision/external (the convenience symlink into that output
base -- see bazel-bin/bazel-out/bazel-testlogs/bazel-Aquila-Vision at the workspace root) or against this
repo's own py_library roots, and writes the result into .vscode/settings.json.

Run it after adding a new import that pulls in a package this hasn't seen before, or after a
`pip_ros` dependency bump changes the hashed wheel directory names under bazel-Aquila-Vision/external. It
requires at least one prior `bazel build` (or `bazel run`) so bazel-Aquila-Vision/external is populated;
targets that were never built won't have resolvable paths yet, and their imports are reported as
unresolved rather than guessed at.
"""

import ast
import json
import sys
from pathlib import Path

# import name -> this repo's own py_library root (the directory imports = ["."] is relative to).
FIRST_PARTY = {
    "launch_api": "basin/launch/api",
    "log_util_py": "basin/logging/log_util_py",
    "ros_test_domain": "tools/ros_test_domain",
    "tarball_test_lib": "tools/runfiles_tar",
}

# import name -> external repo directory name, for repos whose importable package name doesn't
# match their own directory name (rules_python's own repo ships a top-level `python` package).
EXTERNAL_OVERRIDES = {
    "python": "rules_python+",
}

# import name -> pip distribution name, for the pip_ros hub wheels where they differ.
PIP_DIST_OVERRIDES = {
    "yaml": "pyyaml",
    "dateutil": "python_dateutil",
}

EXCLUDED_DIR_NAMES = {".git", "node_modules"}


def find_workspace_root() -> Path:
    here = Path(__file__).resolve().parent
    for candidate in (here, *here.parents):
        if (candidate / "MODULE.bazel").is_file():
            return candidate
    raise SystemExit("could not find MODULE.bazel above this script")


def iter_first_party_py_files(root: Path):
    for path in root.rglob("*.py"):
        rel_parts = path.relative_to(root).parts
        if any(part.startswith("bazel-") or part in EXCLUDED_DIR_NAMES for part in rel_parts):
            continue
        yield path


def top_level_imports(py_file: Path) -> set[str]:
    try:
        tree = ast.parse(py_file.read_text(), filename=str(py_file))
    except (SyntaxError, UnicodeDecodeError):
        return set()
    names = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                names.add(alias.name.split(".")[0])
        elif isinstance(node, ast.ImportFrom):
            if node.level == 0 and node.module:
                names.add(node.module.split(".")[0])
    return names


def is_self_named_package(external_repo: Path, name: str) -> bool:
    pkg_dir = external_repo / name
    if (pkg_dir / "__init__.py").exists() or (pkg_dir / "__init__.pyi").exists():
        return True
    # Some ROS packages (e.g. ament_index_python) ship a plain module file instead of a package.
    return (external_repo / f"{name}.py").exists()


def resolve_external(external_dir: Path, name: str) -> Path | None:
    if name in EXTERNAL_OVERRIDES:
        candidate = external_dir / EXTERNAL_OVERRIDES[name]
        return candidate if candidate.is_dir() else None

    self_named = external_dir / f"{name}+"
    if self_named.is_dir() and is_self_named_package(self_named, name):
        return self_named

    dist = PIP_DIST_OVERRIDES.get(name, name).replace("-", "_").lower()
    matches = sorted(
        p
        for p in external_dir.glob(f"*pip_ros_*{dist}*")
        if p.is_dir() and (p / "site-packages").is_dir()
    )
    if matches:
        return matches[0] / "site-packages"

    return None


def main() -> int:
    workspace_root = find_workspace_root()
    external_dir = workspace_root / "bazel-Aquila-Vision" / "external"
    if not external_dir.is_dir():
        print(
            "bazel-Aquila-Vision/external doesn't exist yet -- run a build (e.g. `bazel build //...`) "
            "at least once so rules_python's fetched packages are on disk, then re-run this script.",
            file=sys.stderr,
        )
        return 1

    stdlib = set(sys.stdlib_module_names) | {"__future__"}

    imported: set[str] = set()
    for py_file in iter_first_party_py_files(workspace_root):
        imported |= top_level_imports(py_file)
    imported -= stdlib

    extra_paths: set[str] = set()
    unresolved: set[str] = set()

    for name in sorted(imported):
        if name in FIRST_PARTY:
            extra_paths.add(FIRST_PARTY[name])
            continue
        resolved = resolve_external(external_dir, name)
        if resolved is not None:
            extra_paths.add(resolved.relative_to(workspace_root).as_posix())
        else:
            unresolved.add(name)

    settings_path = workspace_root / ".vscode" / "settings.json"
    settings = json.loads(settings_path.read_text()) if settings_path.exists() else {}
    settings["python.analysis.extraPaths"] = sorted(extra_paths)

    # bazel-Aquila-Vision mirrors the whole source tree back into itself alongside bazel's build outputs
    # (see its own comment above); without this, Pylance indexes every first-party file twice
    # (once under its real path, once under bazel-Aquila-Vision/<same path>) and duplicates diagnostics
    # and go-to-definition results. extraPaths above still resolves fine through the exclusion --
    # Pylance treats extraPaths as library search dirs, not files it proactively walks.
    exclude = set(settings.get("python.analysis.exclude", []))
    exclude |= {"bazel-bin/**", "bazel-out/**", "bazel-testlogs/**", "bazel-Aquila-Vision/**"}
    settings["python.analysis.exclude"] = sorted(exclude)

    settings_path.write_text(json.dumps(settings, indent=4) + "\n")

    print(f"wrote {len(extra_paths)} extraPaths to {settings_path.relative_to(workspace_root)}")
    if unresolved:
        print(
            "not resolved (not reachable through the current Bazel graph -- "
            "build the target that needs them first, or they may be dead code):",
            file=sys.stderr,
        )
        for name in sorted(unresolved):
            print(f"  {name}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
