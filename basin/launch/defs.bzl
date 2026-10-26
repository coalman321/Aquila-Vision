"""Macros for running a C++ program that embeds a CPython interpreter under Bazel.

See python_env_exec.py.tpl for what the generated launcher does and why it's needed.
"""

load("@aspect_rules_py//py:defs.bzl", "py_test")
load("@aspect_rules_py//py/unstable:defs.bzl", "py_venv_binary")
load(":transitions.bzl", "untransitioned")

def _launcher_src(name, exe, params_file = None, testonly = False):
    src = name + "_launcher_src"
    template = "//basin/launch:python_env_exec.py.tpl"

    # `exe`/`params_file` reached bare here would inherit :name's (transitioned) py_binary
    # config, same as everything below. Only their *rlocationpath string* is needed -- not a
    # real build in that config -- so route them through `untransitioned` first; that also makes
    # sure the actual binary this points at is the same one `data` below (and a plain build of
    # `exe` elsewhere) resolves to, rather than a second, separately-compiled copy.
    exe_ref = src + "_exe"
    untransitioned(name = exe_ref, srcs = [exe], testonly = testonly)
    exe_ref = ":" + exe_ref

    params_ref = None
    if params_file:
        params_ref = src + "_params"
        untransitioned(name = params_ref, srcs = [params_file], testonly = testonly)
        params_ref = ":" + params_ref

    params_rlocationpath = "$(rlocationpath {})".format(params_ref) if params_ref else ""
    native.genrule(
        name = src,
        # `exe_ref`/`params_ref` are only listed so $(rlocationpath) can resolve them; their
        # contents aren't read.
        srcs = [template, exe_ref] + ([params_ref] if params_ref else []),
        outs = [name + "_launcher.py"],
        cmd = ("sed -e 's|@EXE_RLOCATIONPATH@|$(rlocationpath {exe})|g' " +
               "-e 's|@PARAMS_FILE_RLOCATIONPATH@|{params}|g' " +
               "$(location {template}) > $@").format(exe = exe_ref, params = params_rlocationpath, template = template),
        testonly = testonly,
    )
    return src

def python_env_binary(name, exe, params_file = None, data = [], deps = [], **kwargs):
    """A `bazel run`-able (and runfiles-packageable) launcher for `exe`.

    Args:
        name: name of the resulting py_binary target.
        exe: label of the cc_binary that hosts/embeds Python (e.g. a component container).
        params_file: optional label of a ROS parameters file passed to `exe` as
            `--ros-args --params-file <file>` ahead of any arguments given on the command line,
            so a params file or `-p` given there overrides it.
        data: extra runtime data, typically the ament_index targets exe needs.
        deps: py_library targets `exe`'s embedded interpreter imports at runtime.
        **kwargs: forwarded to the underlying py_binary.
    """
    src = _launcher_src(name, exe, params_file = params_file)

    # `data` here is the whole application (basin_container, every driver, every ament_index) --
    # none of it depends on the Python-version/venv flags py_binary's self-transition pins, so
    # route it through `untransitioned` to build it in the ordinary (non-transitioned)
    # configuration instead of a separate copy under bazel-out/*-ST-<hash>/. Only the launcher
    # script itself (:name) stays in the transitioned config.
    data_ref = name + "_data"
    untransitioned(name = data_ref, srcs = [exe] + ([params_file] if params_file else []) + data)

    # py_venv_binary, not py_binary: py_binary's launcher (rules_py's run.tmpl.sh) deletes and
    # rebuilds its venv at $RUNFILES_DIR/.<name>.venv every time it starts, with no way to point it
    # elsewhere, so it can't start from a read-only or root-owned runfiles tree -- which is how
    # //tools/runfiles_tar's archive is deployed. py_venv_binary builds the venv at build time as
    # part of the runfiles (relative symlinks, which runfiles_tar packs as-is) and writes nothing
    # at runtime.
    py_venv_binary(
        name = name,
        srcs = [":" + src],
        main = name + "_launcher.py",
        data = [":" + data_ref],
        deps = ["@rules_python//python/runfiles"] + deps,
        **kwargs
    )

def python_env_test(name, exe, data = [], deps = [], **kwargs):
    """Like python_env_binary, but runs `exe` (a cc_test binary) as a test.

    `exe` should be tagged `manual` so it isn't also run bare, without the
    environment it needs. Bazel's test environment (GTEST_FILTER,
    XML_OUTPUT_FILE, ...) passes through the exec to `exe` unchanged.

    Args:
        name: name of the resulting py_test target.
        exe: label of the cc_test binary that embeds Python.
        data: extra runtime data, typically the ament_index targets exe needs.
        deps: py_library targets `exe`'s embedded interpreter imports at runtime.
        **kwargs: forwarded to the underlying py_test (e.g. `size`).
    """
    src = _launcher_src(name, exe, testonly = True)

    data_ref = name + "_data"
    untransitioned(name = data_ref, srcs = [exe] + data, testonly = True)

    py_test(
        name = name,
        srcs = [":" + src],
        main = name + "_launcher.py",
        data = [":" + data_ref],
        deps = ["@rules_python//python/runfiles"] + deps,
        **kwargs
    )
