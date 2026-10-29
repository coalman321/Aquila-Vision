# basler_driver

ROS 2 driver for Basler **USB3 Vision** cameras (ace, ace 2, boost, dart USB), built on
[Aravis](https://github.com/AravisProject/aravis) instead of Basler's Pylon SDK, which Bazel can't fetch.

It replaces [pylon-ros-camera](https://github.com/coalman321/pylon-ros-camera/tree/humble)'s
`pylon_ros2_camera_wrapper`, which needs Pylon. That driver's startup parameters keep their names,
types and defaults here, so a launch file written for it only needs the package and executable renamed
to `basler_driver` (as [application.prod.py](../../camera_app/camera_webserver/launch/application.prod.py)
now does). Some things are deliberately different:

- **Camera settings are parameters.** Everything the original changed through `set_*` services is a
  ROS parameter here, applied live with `ros2 param set`. The `set_*`, `get_*` and `*_pfs` services,
  the `grab_images_*` actions and the `current_params` topic are gone.
- **USB only.** No GigE (PTP, action commands, packet statistics, `gige/*` parameters) and no blaze ToF.

```sh
bazel run //drivers/basler_driver:basler_driver -- \
    --ros-args -p device_serial:=40123456 -p image_encoding:=rgb8 -p frame_rate:=30.0
ros2 param set /basler_driver exposure 4000.0
```

## How it grabs

As in the original, the camera runs in software-trigger mode: every tick of a `frame_rate` timer fires a
software trigger, waits for that frame, and publishes it. Nothing is grabbed while no one subscribes.

The node is a `basin::node_core::NodeCore`: it sends process heartbeats on the core tick (`core.tick_period`,
0.1 s by default), and frames don't depend on that tick. While there is no camera — not found at startup, or
unplugged (Aravis reports `control-lost`) — the tick raises a `basler_driver/camera_disconnected` fault and
tries to open it every 2 s, without blocking the node. Once it is back, every setting made at launch or since,
including while it was missing, is re-applied.

A brightness search (setting `brightness`) still blocks the node for up to `exposure_search_timeout` (15 s
more for targets above 204), long enough for a launch watchdog to time it out.

## Topics

| Topic | Type | |
| --- | --- | --- |
| `~/image_raw`, `~/camera_info` | `sensor_msgs/Image`, `CameraInfo` | Through `image_transport`. |
| `~/image_rect` | `sensor_msgs/Image` | Only when `camera_info_url` loads a calibration. |
| `~/status` | `basler_driver_msgs/ComponentStatus` | When `enable_status_publisher`. |
| `/diagnostics` | `diagnostic_msgs/DiagnosticArray` | Connection, temperature, stream statistics, calibration. |

## Services (`std_srvs/Trigger`)

`~/execute_software_trigger`, `~/save_user_set` and `~/load_user_set` (the user set named by
`user_set_selector`), `~/reset_device`, `~/start_grabbing`, `~/stop_grabbing`. Loading a user set
refreshes the parameters to the camera's new values.

## Parameters

Set only at launch: `camera_frame`, `device_user_id`, `device_serial` (which camera; the first Basler USB
camera found if both are empty), `camera_info_url`, and `startup_user_set` (`Default`, `UserSet1`..`3` or
`CurrentSetting`).

Applied live:

| Parameter | |
| --- | --- |
| `frame_rate` | Hz; `-1` for the camera's maximum. |
| `image_encoding` | ROS encoding (`mono8`, `rgb8`, `bayer_rggb8`, `mono16`, ...); the camera's pixel formats are logged at startup. |
| `exposure` | Microseconds. Turns auto exposure off. |
| `gain` | Fraction (0..1) of the camera's gain range. Turns auto gain off. |
| `gamma` | |
| `brightness` | Target mean intensity (0..255), reached with the camera's auto functions and a binary exposure search. Ignored at launch when both `exposure` and `gain` are given. |
| `exposure_auto`, `gain_auto`, `brightness_continuous` | Which of exposure and gain the brightness search may vary, and whether the camera keeps adjusting them afterwards. |
| `auto_exposure_upper_limit`, `exposure_search_timeout`, `downsampling_factor_exposure_search` | Brightness search limits. |
| `binning_x`, `binning_y` | 1..32, as far as the camera allows. |
| `roi.x_offset`, `roi.y_offset`, `roi.width`, `roi.height` | Pixels; a 0 width or height means the full sensor. |
| `shutter_mode` | `rolling`, `global`, `global_reset`, or empty to leave it alone. |
| `white_balance_auto` | 0 = Off, 1 = Once, 2 = Continuous. |
| `white_balance_ratio_red`, `_green`, `_blue` | Setting any of them turns `white_balance_auto` off. |
| `grab_strategy` | 0 = OneByOne, 1 = LatestImageOnly, 2 = LatestImages. |
| `max_num_buffer`, `output_queue_size`, `max_transfer_size` | Stream buffers; `output_queue_size` is for grab strategy 2. |
| `grab_timeout`, `trigger_timeout` | Milliseconds. |
| `sleeping` | Stop publishing while true. |
| `trigger_selector` | Which trigger the `trigger_*` parameters configure (default `FrameStart`). |
| `user_set_selector` | The user set the save/load services act on. |
| `pfs_file` | A feature file (`Name<TAB>Value` lines, as Pylon Viewer saves them) loaded into the camera when set. |
| `enable_status_publisher` | |

Declared only when the connected camera has the feature, defaulting to its current value. Each parameter's
description lists the values the camera accepts:

| Parameter | GenICam feature |
| --- | --- |
| `reverse_x`, `reverse_y` | `ReverseX`, `ReverseY` |
| `black_level` | `BlackLevel` |
| `pgi_mode`, `demosaicing_mode`, `noise_reduction`, `sharpness_enhancement` | `PgiMode`, `DemosaicingMode`, `(Bsl)NoiseReduction`, `(Bsl)SharpnessEnhancement` |
| `light_source_preset` | `(Bsl)LightSourcePreset` |
| `sensor_readout_mode` | `SensorReadoutMode` |
| `acquisition_frame_count` | `AcquisitionBurstFrameCount` |
| `trigger_mode`, `trigger_source`, `trigger_activation`, `trigger_delay` | `Trigger*` of `trigger_selector`. Turning `trigger_mode` off makes the camera free-run. |
| `user_set_default` | `UserSetDefault` |
| `device_link_throughput_limit_mode`, `device_link_throughput_limit` | USB bandwidth limit |
| `chunk_mode_active`, `chunks.<Chunk>` | `ChunkModeActive`, `ChunkEnable`. With `chunks.Timestamp`, images are stamped with the camera's clock. |
| `lines.<Line>.mode`, `.source`, `.inverter`, `.debouncer_time` | `Line*` of each `LineSelector` entry |
| `user_outputs.<UserOutput>` | `UserOutputValue` of each `UserOutputSelector` entry |
| `timers.<Timer>.duration`, `.trigger_source` | `Timer*` of each `TimerSelector` entry |

A rejected value (out of range, not an entry the camera offers, or not writable while grabbing) fails the
`ros2 param set` with the camera's reason, and the parameter keeps its old value.

## Differences from pylon-ros-camera worth knowing

- `.pfs` files hold each feature's value under its current selector only. Pylon also saves the
  selector-dependent values (each line, each balance ratio).
- `image_encoding: mono16` uses `Mono16` when the camera has it, not only `Mono12`.
- There is no `get_statistic_resynchronization_count` equivalent: Aravis doesn't report USB resynchronizations.
- Not yet run against a real Basler camera. The tests use Aravis' simulated camera
  (`bazel test //drivers/basler_driver:basler_driver_node_test`).
