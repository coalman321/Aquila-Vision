# chassis_proxy_client

UDP bridge between this vision stack and the roboRIO, originally vendored from
[MW-Lib](https://github.com/FRC-Team-4143/MW-Lib)'s `src/main/cpp/chassis_proxy_client` and Bazel-only
(no `package.xml`/`CMakeLists.txt`). This is now the only copy; MW-Lib keeps the roboRIO side
(`com.marswars.proxy_server`) and the shared message definitions.

`proxy_client` (`//camera_app/chassis_proxy_client:proxy_client`) sends detections, camera info and tag
solutions to the roboRIO and receives match info, auto-snapshot requests and clock sync. The
[daemon launch](../camera_webserver/launch/daemon.prod.py) starts it as package `chassis_proxy_client`.

`bazel build //camera_app/chassis_proxy_client/...` and `bazel test //camera_app/chassis_proxy_client/...`

## Wire protocol

Each UDP datagram is one protobuf `chassis_proxy.v1.Envelope`. Its definition lives in MW-Lib
(`src/main/proto/chassis_proxy/v1/`: `envelope.proto`, `core.proto` for the shared types, and one
file per payload type), which the roboRIO's `ProxyServerThread` builds with QuickBuffers. This
build fetches MW-Lib at the release tag pinned by the `mwlib` `http_archive` in
[MODULE.bazel](../../MODULE.bazel); [third_party/mwlib](../../third_party/mwlib/) builds the C++
classes from it (`@mwlib//:chassis_proxy_cc_proto`).

To change the protocol:

1. Edit the `.proto` files in MW-Lib, following the rules in `envelope.proto`'s header comment,
   and update its golden frames in `src/test/resources/chassis_proxy/`. MW-Lib's Java tests and
   `:udp_server_test` here both check against those frames.
2. Release MW-Lib, then bump the tag, `strip_prefix` and `sha256` of `mwlib` in MODULE.bazel.
3. Deploy robot code built against the same MW-Lib release. A receiver drops envelopes whose
   `protocol_version` it doesn't support, and `proxy_client` logs a warning when that happens.

## Differences from upstream

- `BASIN_PARAM(name, var, node)` became `createParam(name, var)`, and tf2's headers are `.hpp`
  (the same Lyrical changes as in [camera_app](../README.md)).
- The hand-rolled binary codecs (`messages/`, `BinIStream`/`BinOStream`) are gone. Messages are
  the protobuf types above, and MW-Lib no longer carries its own copy of this client.
- Detection angles are sent in radians, as the roboRIO side always documented, and detection
  stamps are shifted onto the roboRIO clock by the timesync offset, the same as tag solutions.
