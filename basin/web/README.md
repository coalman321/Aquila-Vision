# Web

HTTP and WebRTC interfaces for the robot

- **[web_core](web_core/)** (`//basin/web/web_core`) — `WebNode`, a ROS 2 node that owns an
  [cpp-httplib](https://registry.bazel.build/modules/cpp-httplib) server, plus components that add
  routes to it: `FileManagerComponent`, `LaunchComponent` (process list and remote requests),
  `ParameterApiComponent`, `StaticServerComponent`, and `json_serialization` for the `//interfaces`
  messages. `bazel test //basin/web/web_core/...`.
- **[webrtc_streamer](webrtc_streamer/)** (`//basin/web/webrtc_streamer`) — streams `sensor_msgs/Image`
  topics to browsers over WebRTC with GStreamer (VP8 through `webrtcbin`). The GStreamer it links is
  built from source by the workshop registry's [gstreamer module](https://gitlab.tuckers-workshop.xyz/robotics/workshop-bazel-registry/-/tree/master/sources/gstreamer/1.28.7.workshop.1/README.md).
  `bazel test //basin/web/webrtc_streamer:streamer_test` starts the node against a fake camera and
  has it answer a real WebRTC offer.

- **[svelte_framework](svelte_framework/)** (`//basin/web/svelte_framework:srcs`) — the shared Svelte components
  (`CameraViewer`, `FileUploader`, `LaunchManager`, ...) the web UIs are built from. It is plain source, not an npm
  package: each app compiles it in through a `$framework` alias in its `svelte.config.js`. See its README for how to
  add an app.

The C++ packages are Bazel-only (no `package.xml`/`CMakeLists.txt`).
