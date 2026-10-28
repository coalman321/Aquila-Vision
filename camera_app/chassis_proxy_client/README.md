# chassis_proxy_client

UDP bridge between this vision stack and the roboRIO, vendored from
[MW-Lib](https://github.com/FRC-Team-4143/MW-Lib/tree/develop/src/main/cpp/chassis_proxy_client)
(`develop`, `src/main/cpp/chassis_proxy_client`) and Bazel-only (no `package.xml`/`CMakeLists.txt`).

`proxy_client` (`//camera_app/chassis_proxy_client:proxy_client`) sends detections, camera info and tag
solutions to the roboRIO and receives match info, auto-snapshot requests and clock sync. The
[daemon launch](../camera_webserver/launch/daemon.prod.py) starts it as package `chassis_proxy_client`.

`bazel build //camera_app/chassis_proxy_client/...`

## Differences from upstream

- `BASIN_PARAM(name, var, node)` became `createParam(name, var)`, and tf2's headers are `.hpp`
  (the same Lyrical changes as in [camera_app](../README.md)).
- Nothing else is changed: the message codecs still live as `.cpp` files under `include/`.
