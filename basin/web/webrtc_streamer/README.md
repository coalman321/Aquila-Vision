# webrtc_streamer

Bazel-only, like the rest of the repo: `bazel run //basin/web/webrtc_streamer:webrtc_streamer_node`.
GStreamer, its plugins and everything they need are built from source by
the workshop registry's [gstreamer module](https://gitlab.tuckers-workshop.xyz/robotics/workshop-bazel-registry/-/tree/master/sources/gstreamer/1.28.7.workshop.1/README.md), so nothing has to be installed on
the host, and the node finds the plugins in its own runfiles (`GST_PLUGIN_PATH` is set at startup
unless you set it yourself).

The sections below are from the original repo's README. The `colcon`, `apt` and `ros2 run`/`ros2
launch` commands in them are the original ones; `test/publish_dummy_image.py` and
`test/test_signaling.py` are its manual scripts, which Bazel doesn't run.
`bazel test //basin/web/webrtc_streamer:streamer_test` is the automated equivalent: it starts the node
against a fake camera on `/debug_img` and has it answer a real offer.

## Features

- **Multiple Streams**: Support for multiple independent image streams from different ROS 2 topics
- **Multiple Peers per Stream**: Each stream can serve multiple WebRTC clients simultaneously
- **Individual Bandwidth Control**: Configure bitrate, framerate, resolution, and encoder settings per peer
- **Dynamic Configuration**: Update bandwidth settings for active connections in real-time
- **RESTful API**: Complete HTTP API for connection management and stream monitoring
- **VP8 Encoding**: Hardware-accelerated VP8 video encoding via GStreamer
- **Backward Compatible**: Single-stream mode works with existing configurations

## Architecture

The package consists of three main components:

- **WebRTCStreamer** (`webrtc_streamer_node`): Main ROS 2 node that subscribes to image topics and manages the WebRTC server.
- **WebRTCComponent**: Core WebRTC functionality handling GStreamer pipelines, peer connections, and stream management. Creates a separate pipeline for each stream source with a tee element that distributes frames to multiple peers.
- **WebRTCServer**: HTTP/REST API server for WebRTC signaling, bandwidth control, and stream management.

### Stream and Peer Model

- **Stream**: A source of image data from a ROS 2 topic. Each stream has a unique `stream_id` and maintains its own GStreamer pipeline with an `appsrc` element for frame ingestion and a `tee` element for distribution.
- **Peer**: A WebRTC client connection. Multiple peers can connect to the same stream, each with independent encoding settings.
- Each peer gets its own encoder (`vp8enc`) and `webrtcbin` element, branched from the stream's tee, allowing individual bandwidth and quality control without affecting other peers.

## Usage

### Single Stream Mode (Backward Compatible)

Run the node with a single image topic:

```bash
ros2 launch webrtc_streamer webrtc_streamer.launch.py
```

### Multiple Stream Mode

Run the node with multiple streams:

```bash
ros2 launch webrtc_streamer webrtc_multi_stream.launch.py
```

Or configure manually:

```bash
ros2 run webrtc_streamer webrtc_streamer_node --ros-args \
  -p streams:="['camera1:/camera1/image_raw','camera2:/camera2/image_raw']" \
  -p port:=8080
```

### Parameters

| Parameter | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `image_topic` | string | `/camera/image_raw` | Single image topic (legacy mode). If neither `image_topic` nor `streams` is set, defaults to `/camera/image_raw` with stream_id "default". |
| `streams` | string array | `[]` | Array of stream configurations in format `"stream_id:topic"` (e.g., `"camera1:/camera1/image_raw"`). |
| `port` | int | `8080` | The port for the HTTP signaling server (inherited from `web_core`). |
| `target_bitrate` | int | `2000000` | Default target bitrate in bits per second (2 Mbps). |
| `max_framerate` | int | `30` | Default maximum framerate. |
| `resolution_scale` | double | `1.0` | Default resolution scale factor (0.25 to 1.0). |
| `encoder_speed` | int | `4` | Default VP8 encoder speed preset (0=best quality, 16=fastest). |

## Testing

The package includes test scripts to verify functionality.

### Single Stream Test

1. **Start the Node**
   ```bash
   ros2 launch webrtc_streamer webrtc_streamer.launch.py
   ```

2. **Publish Dummy Video** (in a separate terminal)
   ```bash
   python3 src/basin-web/webrtc_streamer/test/publish_dummy_image.py
   ```

3. **Run Signaling Test** (in another terminal)
   ```bash
   python3 src/basin-web/webrtc_streamer/test/test_signaling.py
   ```
   If successful, the script will print `SUCCESS: Received valid SDP answer.`

### Multiple Stream Test

1. **Start the Multi-Stream Node**
   ```bash
   ros2 launch webrtc_streamer webrtc_multi_stream.launch.py
   ```
   This launches two image publishers (`camera1` and `camera2`) and the WebRTC streamer.

2. **Verify Streams**
   ```bash
   curl http://localhost:8080/webrtc/streams
   ```
   Should return a list of available streams.

3. **Test Multiple Connections**
   Connect multiple clients to different streams using the JavaScript examples above, or test with multiple signaling requests:
   ```bash
   # Connect to camera1
   curl -X POST http://localhost:8080/webrtc/offer \
     -H "Content-Type: application/json" \
     -d '{"type":"offer","sdp":"...","stream_id":"camera1"}'
   
   # Connect to camera2
   curl -X POST http://localhost:8080/webrtc/offer \
     -H "Content-Type: application/json" \
     -d '{"type":"offer","sdp":"...","stream_id":"camera2"}'
   ```

## Use Cases

### Multi-Camera Robot Dashboard
Stream multiple camera feeds from a robot (front, rear, manipulator) to a web dashboard where operators can view all feeds simultaneously.

```bash
ros2 run webrtc_streamer webrtc_streamer_node --ros-args \
  -p streams:="['front:/camera/front/image_raw','rear:/camera/rear/image_raw','arm:/camera/arm/image_raw']"
```

### Adaptive Streaming for Mobile Clients
Detect client network conditions and adjust bandwidth dynamically:

```javascript
// Reduce quality for poor connections
if (networkSpeed < 1000000) {  // < 1 Mbps
  await fetch('http://localhost:8080/webrtc/bandwidth/peer', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      peer_id: currentPeerId,
      target_bitrate: 300000,
      max_framerate: 10,
      resolution_scale: 0.5
    })
  });
}
```

### Multi-User Viewing
Multiple operators viewing the same camera feed, each with independent connection quality based on their network conditions.

## Best Practices

- **Stream IDs**: Use descriptive stream IDs (e.g., `front_camera`, `gripper_cam`) rather than generic names
- **Bandwidth Settings**: 
  - Start with higher quality and reduce based on client feedback
  - `target_bitrate`: 500kbps (low), 1Mbps (medium), 2Mbps+ (high)
  - `resolution_scale`: 0.5 for mobile, 1.0 for desktop
  - `encoder_speed`: 0-4 (quality), 8-12 (balanced), 16 (speed)
- **Resource Management**: Monitor peer count and system resources; each peer requires its own encoder
- **Error Handling**: Always handle 404 errors for stream_id and peer_id to gracefully handle disconnections
- **Cleanup**: Explicitly disconnect peers when done to free resources

## API Endpoints

### POST /webrtc/offer

Creates a new peer connection for a specific stream. Accepts a WebRTC SDP offer and returns an SDP answer along with a unique peer_id.

**Request Body:**
```json
{
  "type": "offer",
  "sdp": "v=0\r\n...",
  "stream_id": "camera1"
}
```
*Note: `stream_id` is optional and defaults to "default" for backward compatibility.*

**Response Body:**
```json
{
  "type": "answer",
  "sdp": "v=0\r\n...",
  "peer_id": "peer_1",
  "stream_id": "camera1"
}
```

### POST /webrtc/disconnect

Disconnects a specific peer connection.

**Request Body:**
```json
{
  "peer_id": "peer_1"
}
```

**Response:** `200 OK` with "Disconnected" text.

### GET /webrtc/status

Returns the total number of active peer connections across all streams.

**Response Body:**
```json
{
  "active_peers": 3
}
```

### GET /webrtc/streams

Lists all registered streams and their active peer counts.

**Response Body:**
```json
{
  "streams": [
    {
      "stream_id": "camera1",
      "active_peers": 2
    },
    {
      "stream_id": "camera2",
      "active_peers": 1
    }
  ]
}
```

### GET /webrtc/streams/:stream_id

Returns status information for a specific stream.

**Response Body:**
```json
{
  "stream_id": "camera1",
  "active_peers": 2
}
```

**Error:** `404 Not Found` if stream doesn't exist.

### GET /webrtc/bandwidth

Returns the default bandwidth configuration used for new peer connections.

**Response Body:**
```json
{
  "target_bitrate": 2000000,
  "max_framerate": 30,
  "resolution_scale": 1.0,
  "encoder_speed": 4
}
```

### POST /webrtc/bandwidth

Updates the default bandwidth configuration for new peer connections. Existing connections are not affected.

**Request Body:**
```json
{
  "target_bitrate": 1000000,
  "max_framerate": 15,
  "resolution_scale": 0.5,
  "encoder_speed": 8
}
```
*Note: All fields are optional. Only specified fields will be updated.*

**Response Body:**
```json
{
  "status": "success",
  "config": {
    "target_bitrate": 1000000,
    "max_framerate": 15,
    "resolution_scale": 0.5,
    "encoder_speed": 8
  }
}
```

### POST /webrtc/bandwidth/peer

Updates bandwidth settings for a specific active peer connection.

**Request Body:**
```json
{
  "peer_id": "peer_1",
  "target_bitrate": 500000,
  "max_framerate": 10
}
```
*Note: All bandwidth fields except `peer_id` are optional.*

**Response Body:**
```json
{
  "status": "success",
  "peer_id": "peer_1"
}
```

**Error:** `404 Not Found` if peer doesn't exist.

## Example Clients

### JavaScript (Browser)

Minimal example connecting to a specific stream:

```javascript
const pc = new RTCPeerConnection({
  iceServers: [{ urls: 'stun:stun.l.google.com:19302' }]
});

let peerId = null;

pc.ontrack = (event) => {
  const video = document.createElement('video');
  video.srcObject = event.streams[0];
  video.autoplay = true;
  document.body.appendChild(video);
};

// Create Offer
const offer = await pc.createOffer({ offerToReceiveVideo: true });
await pc.setLocalDescription(offer);

// Wait for ICE gathering to complete
await new Promise(resolve => {
  if (pc.iceGatheringState === 'complete') resolve();
  pc.onicecandidate = e => { if (!e.candidate) resolve(); };
});

// Send Offer with stream_id
const response = await fetch('http://localhost:8080/webrtc/offer', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({
    type: pc.localDescription.type,
    sdp: pc.localDescription.sdp,
    stream_id: 'camera1'  // Specify which stream to connect to
  })
});

const answer = await response.json();
peerId = answer.peer_id;  // Save peer_id for later disconnect
await pc.setRemoteDescription(answer);

// Optional: Disconnect when done
// await fetch('http://localhost:8080/webrtc/disconnect', {
//   method: 'POST',
//   headers: { 'Content-Type': 'application/json' },
//   body: JSON.stringify({ peer_id: peerId })
// });
```

### Multiple Streams Example

Connect to multiple streams simultaneously:

```javascript
async function connectToStream(streamId) {
  const pc = new RTCPeerConnection({
    iceServers: [{ urls: 'stun:stun.l.google.com:19302' }]
  });

  pc.ontrack = (event) => {
    const video = document.createElement('video');
    video.srcObject = event.streams[0];
    video.autoplay = true;
    video.style.width = '320px';
    document.body.appendChild(video);
    
    const label = document.createElement('div');
    label.textContent = streamId;
    document.body.appendChild(label);
  };

  const offer = await pc.createOffer({ offerToReceiveVideo: true });
  await pc.setLocalDescription(offer);

  await new Promise(resolve => {
    if (pc.iceGatheringState === 'complete') resolve();
    pc.onicecandidate = e => { if (!e.candidate) resolve(); };
  });

  const response = await fetch('http://localhost:8080/webrtc/offer', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      type: pc.localDescription.type,
      sdp: pc.localDescription.sdp,
      stream_id: streamId
    })
  });

  const answer = await response.json();
  await pc.setRemoteDescription(answer);
  
  return { pc, peerId: answer.peer_id };
}

// Connect to multiple streams
const connections = await Promise.all([
  connectToStream('camera1'),
  connectToStream('camera2')
]);
```

### Bandwidth Control Example

Adjust bandwidth for low-bandwidth connections:

```javascript
// Get current default bandwidth config
const configResponse = await fetch('http://localhost:8080/webrtc/bandwidth');
const config = await configResponse.json();
console.log('Default config:', config);

// Set lower bandwidth for new connections (e.g., mobile users)
await fetch('http://localhost:8080/webrtc/bandwidth', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({
    target_bitrate: 500000,      // 500 kbps
    max_framerate: 15,            // 15 fps
    resolution_scale: 0.5,        // Half resolution
    encoder_speed: 8              // Faster encoding, lower quality
  })
});

// Or adjust bandwidth for an existing peer
await fetch('http://localhost:8080/webrtc/bandwidth/peer', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({
    peer_id: 'peer_1',
    target_bitrate: 1000000
  })
});
```
