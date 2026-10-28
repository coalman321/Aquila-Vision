# camera_app

The camera webserver stack, vendored from the standalone
[camera-webserver](https://gitlab.tuckers-workshop.xyz/robotics/wide-eyes/camera-webserver) repo (now
**superseded for development**) and Bazel-only (no `package.xml`/`CMakeLists.txt`).

- **[camera_webserver](camera_webserver/)** (`//camera_app/camera_webserver`) — `CameraWebNode`, a
  `//basin/web/web_core` `WebNode` that manages camera sources, pipelines, field maps and calibrations over
  HTTP and serves the UI. `:camera_web` runs it standalone; `:camera_web_component` + `:ament_index` is the
  `basin_container` form (package `camera_webserver`, class `camera_web::CameraWebNode`). The `.prod.py`
  launch descriptions are in `launch/`.
- **camera_cal** now lives at [perception/camera_cal](../perception/camera_cal/) (`//perception/camera_cal`):
  `CameraCalNode`, chessboard calibration over a service; `:camera_cal_tui` is its curses client.
- **[camera-app](camera-app/)** (`//camera_app/camera-app:build`) — the SvelteKit UI, built by Vite to a static
  site. `camera_webserver` publishes it as `share/camera_webserver/static_content`.

`bazel build //camera_app/...`

## Things that differ from the originals

- **Aravis instead of Pylon.** The original listed cameras with Basler's proprietary Pylon SDK, which can't
  be fetched by Bazel. `BaslerCameraSource` now uses [Aravis](https://registry.bazel.build/modules/aravis)
  (open source, built from the BCR), which finds GigE Vision and USB3 Vision cameras and reports the same
  serial number and model name; Basler cameras are the ones whose vendor says so. Untested against hardware.
  The camera driver `application.prod.py` launches is [//drivers/basler_driver](../drivers/basler_driver/),
  an Aravis-based replacement for Basler's Pylon-based `pylon_ros2_camera_wrapper`, for USB3 Vision cameras,
  with that driver's parameter names.
- **camera_cal has no CUDA path.** The OpenCV in this workspace has no `cudawarping`/`cudaimgproc`, so
  `CAMERA_CAL_HAVE_CUDA` is never defined; it is the CPU path the original fell back to.
- **Lyrical API changes.** `BASIN_PARAM`/`BASIN_DYN_PARAM` became `createParam`/`createDynParam`, and tf2's
  headers are `.hpp`. The launch files import `launch_api` rather than `production_launch_api`.
- **The frontend is a standalone pnpm project** (its own `pnpm-lock.yaml` in `camera-app/`, the `@npm_camera_app`
  repo in MODULE.bazel) built by `aspect_rules_js`. It compiles [//basin/web/svelte_framework](../basin/web/svelte_framework/)
  in from source through a `$framework` alias in `svelte.config.js`, instead of a `file:` dependency on basin-web.
  After editing `package.json`, run `pnpm install --lockfile-only` in `camera-app/`.
- `launch/base.launch.py` is the old `ros2 launch` entry for `production_launch`, which no longer exists; it
  isn't used by anything.
- The `exec_depend`s in the old `package.xml` (`webrtc_streamer`, `landmark_detector_ros`, `chassis_proxy_client`,
  ...) are only named by `application.prod.py`/`daemon.prod.py`. `chassis_proxy_client` is not in this
  workspace.
