"""Undoes aspect_rules_py's py_binary/py_test self-transition for specific dependency edges.

`py_binary`/`py_test` (@aspect_rules_py//py/private/py_binary.bzl) apply an unconditional
rule-level self-transition that pins Python version/venv/freethreaded config for themselves and,
by default attribute propagation, everything reachable through their `data`/`deps`/`srcs` -- see
the comment on `python_env_binary` in defs.bzl. For `python_env_binary`, that closure is the
entire application (basin_container, every driver, every ament_index), none of which cares about
those flags, so it all gets compiled a second time into a separate `bazel-out/*-ST-<hash>/` tree
instead of sharing the cache with a normal `bazel build //some/driver`.

`untransitioned` re-exports a set of targets through an outgoing transition that resets those same
flags to their stock defaults, landing back in the ordinary (non-transitioned) configuration. Route
`python_env_binary`'s heavy `data` (and `exe`) through it and that duplication goes away; only the
launcher script itself, which is genuinely tiny, stays in the transitioned config.
"""

_PYTHON_VERSION_FLAG = "@aspect_rules_py//py/private/interpreter:python_version"
_RULES_PYTHON_VERSION_FLAG = "@rules_python//python/config_settings:python_version"

def _reset_py_config_impl(settings, _attr):
    # aspect_rules_py//py/private/interpreter:python_version defaults to "". py_binary's own
    # self-transition (python_version_transition, in @aspect_rules_py//py/private/transitions.bzl)
    # mirrors rules_python's ambient `python_version` (pinned repo-wide to "3.12" by
    # MODULE.bazel's `python.toolchain(python_version = "3.12")`) into *this* flag the first time
    # any py_binary/py_test is reached, which is the one actual value change it makes -- everything
    # else it touches (rules_python's own python_version, venv, freethreaded) is a same-value
    # passthrough already matching a plain, non-py_binary build, so only this needs resetting.
    #
    # Resetting straight to "" assumed "the plain build" is the only other consumer of `srcs`.
    # It isn't: //tools/runfiles_tar also builds a `python_env_binary`'s `exe` directly (as one of
    # runfiles_tar's own `binaries`), through its own `_one_python_version` transition, which sets
    # *this same flag* to `settings[_RULES_PYTHON_VERSION_FLAG]` rather than leaving it at "" --
    # so a hardcoded "" here reset to a *different* config than that direct copy shares, and
    # runfiles_tar ended up merging two separately-compiled copies of everything downstream of
    # `exe` (e.g. rosidl-generated interface libraries) into one runfiles tree. Mirroring the same
    # fallback here, instead of hardcoding "", keeps both copies in the same configuration whether
    # or not an outer transition like runfiles_tar's is also in play.
    return {_PYTHON_VERSION_FLAG: settings[_RULES_PYTHON_VERSION_FLAG]}

_reset_py_config = transition(
    implementation = _reset_py_config_impl,
    inputs = [_RULES_PYTHON_VERSION_FLAG],
    outputs = [_PYTHON_VERSION_FLAG],
)

def _untransitioned_impl(ctx):
    files = depset(transitive = [t[DefaultInfo].files for t in ctx.attr.srcs])
    runfiles = ctx.runfiles()
    for t in ctx.attr.srcs:
        runfiles = runfiles.merge(t[DefaultInfo].default_runfiles)
    return [DefaultInfo(files = files, runfiles = runfiles)]

untransitioned = rule(
    doc = "Re-exports `srcs` with the py_binary/py_test config-flag self-transition undone.",
    implementation = _untransitioned_impl,
    attrs = {
        "srcs": attr.label_list(cfg = _reset_py_config, allow_files = True),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)
