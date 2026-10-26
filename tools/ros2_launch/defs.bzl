"""A `ros2 launch <pkg> <file>` equivalent for this hermetic build, which has no `ros2` CLI.

`ros2_launch()` turns a single `.launch.py` (or `.xml`/`.yaml`) source file into a
`bazel run`-able target that invokes it via `ros2launch.api.launch_a_launch_file` --
the same call the real `ros2 launch` command makes internally -- instead of needing the
ros2cli multiplexer.
"""

load("@aspect_rules_py//py:defs.bzl", "py_binary")
load("@pip_ros//:requirements.bzl", "requirement")

def _ros2_launch_libexec_impl(ctx):
    # launch_ros resolves `Node(package=, executable=)` via
    # launch_ros.substitutions.ExecutableInPackage, which looks under
    # "<ament_prefix>/lib/<package>/<executable>" -- but this workspace's ament_index()
    # rule (see @rosdistro//ament:defs.bzl) only publishes a flat
    # "<ament_prefix>/bin/<executable>". This rule fills that gap by mirroring each
    # given executable into the "lib/<package>/" layout launch_ros actually expects,
    # without needing to patch the vendored ament_index() rule itself.
    symlinks = {}
    for target, package_name in ctx.attr.executables.items():
        for file in target.files.to_list():
            symlinks["lib/" + package_name + "/" + file.basename] = file
    return [DefaultInfo(runfiles = ctx.runfiles(symlinks = symlinks))]

_ros2_launch_libexec = rule(
    implementation = _ros2_launch_libexec_impl,
    attrs = {
        "executables": attr.label_keyed_string_dict(
            allow_files = True,
            doc = "{executable target: ROS package name} to expose for Node() resolution.",
        ),
    },
)

def _ros2_launch_runner_src_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name + ".py")
    ctx.actions.expand_template(
        template = ctx.file._template,
        output = out,
        substitutions = {
            "@LAUNCH_FILE_SHORT_PATH@": ctx.file.launch_file.short_path,
        },
    )
    return [DefaultInfo(files = depset([out]))]

_ros2_launch_runner_src = rule(
    implementation = _ros2_launch_runner_src_impl,
    attrs = {
        "launch_file": attr.label(mandatory = True, allow_single_file = True),
        "_template": attr.label(
            default = "//tools/ros2_launch:run_launch.py.tpl",
            allow_single_file = True,
        ),
    },
)

def ros2_launch(name, launch_file, executables = {}, data = [], deps = [], **kwargs):
    """Defines a `bazel run`-able target that runs `launch_file`.

    Args:
        name: name of the resulting py_binary target.
        launch_file: label of the single `.launch.py` (or `.xml`/`.yaml`) source file to run.
        executables: {executable target label: ROS package name}, one entry for every
            `Node(package=, executable=)` the launch file starts -- see this file's
            comment on `_ros2_launch_libexec` for why this is needed.
        data: extra runtime data, typically the launched package's own
            `:ament_index_launchable` target(s) (for its config files, FindPackageShare, etc).
        deps: extra py deps, if the launch file itself needs any (rare).
        **kwargs: forwarded to the underlying py_binary (e.g. `visibility`).
    """
    runner_src = name + "_runner_src"
    _ros2_launch_runner_src(
        name = runner_src,
        launch_file = launch_file,
    )

    libexec = name + "_libexec"
    _ros2_launch_libexec(
        name = libexec,
        executables = executables,
    )

    py_binary(
        name = name,
        srcs = [":" + runner_src],
        data = [launch_file, ":" + libexec] + data,
        deps = [
            "@ros2launch//:ros2launch",
            # launch_ros imports lifecycle_msgs.srv at runtime but doesn't declare it as
            # a dep of its own py_library -- see the matching MODULE.bazel comment on the
            # lifecycle_msgs bazel_dep.
            "@lifecycle_msgs//:py",
            "@rules_python//python/runfiles:runfiles",
            # launch_ros/utilities/plugin_support.py imports `packaging` but, like
            # lifecycle_msgs above, @launch_ros//:launch_ros doesn't declare it as a dep.
            requirement("packaging"),
        ] + deps,
        **kwargs
    )
