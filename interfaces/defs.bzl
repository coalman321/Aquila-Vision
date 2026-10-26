"""Makes an interface package's definitions and typesupport available to a process at runtime."""

load("@rosdistro//ament:defs.bzl", "ament_index")
load("@rosidl_default_generators//:defs.bzl", "cc_ros_library")

def interface_definitions(kind):
    """Declares what the parent package's `interface_ament_index` publishes for this directory.

    Call it from a package's `msg/` or `srv/` BUILD.bazel, next to its `ros_message`/`ros_service`
    targets (which have to be named after their file: Foo.msg -> :Foo).

    Args:
        kind: "msg" or "srv".
    """
    srcs = native.glob(["*." + kind])
    native.filegroup(
        name = "definitions",
        srcs = srcs,
    )
    cc_ros_library(
        name = "typesupport",
        deps = [":" + src.removesuffix("." + kind) for src in srcs],
    )

def _typesupport_root_impl(ctx):
    package = ctx.attr.package_name
    prefix = "lib%s__" % package
    root_symlinks = {}
    for target in ctx.attr.typesupport:
        for file in target[DefaultInfo].files.to_list():
            # `cc_ros_library` also lists the libraries of every interface these depend on (each
            # of which lives at its own repository's path); only this package's are ours to place.
            if file.basename.startswith(prefix):
                kind = file.basename[len(prefix):].split("__")[0]
                root_symlinks["%s+/%s/%s" % (package, kind, file.basename)] = file
    return [DefaultInfo(runfiles = ctx.runfiles(root_symlinks = root_symlinks))]

_typesupport_root = rule(
    implementation = _typesupport_root_impl,
    attrs = {
        "package_name": attr.string(mandatory = True),
        "typesupport": attr.label_list(mandatory = True),
    },
)

def interface_ament_index(subdirs, name = "ament_index"):
    """Publishes this package's interfaces the way an RCR message package does.

    Something that describes or loads a type it was not linked against needs two things:

      * the `.msg`/`.srv` text, found through `share/<package>/` and the `rosidl_interfaces`
        resource (foxglove_bridge, rosbag2 schema lookup); and
      * the typesupport shared libraries, which rclcpp finds at `<package>+/<msg|srv>/lib*.so`
        relative to the runfiles root (foxglove_bridge creating a service client or subscription).
        RCR packages are repositories of that name, so they are already there; these live in
        `_main` and are put there by `_typesupport_root`.

    Any process with this target in its runfiles can then resolve the package's types. Call it from
    the package's own BUILD.bazel, whose `msg/` and `srv/` directories each call
    `interface_definitions`.

    Args:
        subdirs: which of "msg" and "srv" the package has.
        name: name of the resulting ament_index target.
    """
    package = native.package_name()
    package_name = package.split("/")[-1]
    _typesupport_root(
        name = name + "_typesupport",
        package_name = package_name,
        typesupport = ["//%s/%s:typesupport" % (package, subdir) for subdir in subdirs],
    )
    ament_index(
        name = name,
        data = ["//%s/%s:definitions" % (package, subdir) for subdir in subdirs],
        data_strip_prefix = package,
        deps = [":" + name + "_typesupport"],
        export_name = package_name,
    )
