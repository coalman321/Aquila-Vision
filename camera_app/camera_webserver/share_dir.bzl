"""Publishes a generated directory under a package's ament share directory."""

def _share_dir_impl(ctx):
    dirs = ctx.attr.src[DefaultInfo].files.to_list()
    if len(dirs) != 1 or not dirs[0].is_directory:
        fail("%s must be a single directory" % ctx.attr.src.label)
    return [DefaultInfo(
        files = depset(dirs),
        runfiles = ctx.runfiles(symlinks = {
            "share/%s/%s" % (ctx.attr.package_name, ctx.attr.dest): dirs[0],
        }),
    )]

# ament_index() strips a literal prefix from each data file's path, which a generated file's
# bazel-out/<config>/bin/ directory makes unusable, so this places the directory itself. Pass it
# in an ament_index's `deps` for the same package_name.
share_dir = rule(
    implementation = _share_dir_impl,
    attrs = {
        "dest": attr.string(mandatory = True),
        "package_name": attr.string(mandatory = True),
        "src": attr.label(mandatory = True),
    },
)
