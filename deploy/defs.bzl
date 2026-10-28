"""Publishes a Bazel-built executable where basin launch's Node(package_name=, executable=) looks."""

load("@rosdistro//ament:defs.bzl", "ament_index")

def launch_exe(name, binary, package, executable):
    """Makes `binary` launchable as Node(package_name = package, executable = executable).

    launch_api resolves a node to <prefix>/lib/<package>/<executable>. Putting the binary itself
    there (ament_index's `lib_data`) strips it of its runfiles, so a binary that finds its
    libraries through its own RUNPATH, or its data through the Bazel runfiles library (webrtc_streamer's
    GStreamer plugins, image_transport's plugin index), would fail to start. Publish a shell script
    there instead, which execs the real binary inside the runfiles tree that every launcher
    (`bazel run`, and the bin/ launchers of //tools/runfiles_tar) exports as RUNFILES_DIR.

    Args:
      name: Name of the ament_index target.
      binary: The cc_binary to run.
      package: ROS package name of the Node().
      executable: Executable name of the Node(); the script's file name.
    """
    wrapper = name + "_wrapper"
    native.genrule(
        name = wrapper,
        srcs = [binary],
        outs = [name + "_bin/" + executable],
        cmd = """cat > $@ <<'EOF'
#!/bin/sh
exec "$${RUNFILES_DIR:?RUNFILES_DIR is not set}/$(rlocationpath %s)" "$$@"
EOF""" % binary,
        executable = True,
    )

    # `deps` brings the binary and its runfiles along, which the script only names.
    ament_index(
        name = name,
        export_name = package,
        lib_data = [":" + wrapper],
        deps = [binary],
    )
