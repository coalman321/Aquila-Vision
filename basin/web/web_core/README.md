# web_core Package

## ProcessListComponent

## Overview
The `ProcessListComponent` is an optional component that registers itself to an existing `WebNode` to provide process monitoring capabilities via HTTP.

## Features
- Subscribes to `/state/launch/process_list` topic
- Exposes `/api/process_list` endpoint that returns JSON with process information
- Includes process status, CPU usage, memory, and restart counters
- Registers itself to an existing WebNode instance

## Usage

### Basic Usage - Registering Component to WebNode
```cpp
#include <rclcpp/rclcpp.hpp>
#include "basin/web_core/web_node.hpp"
#include "basin/web_core/process_list_web_node.hpp"

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    
    // Create the base web node
    auto web_node = std::make_shared<basin::web_core::WebNode>();
    
    // Register the process list component to it
    auto process_list_component = std::make_shared<basin::web_core::ProcessListComponent>(web_node);
    
    // Start the server
    web_node->startServer();
    
    rclcpp::spin(web_node);
    rclcpp::shutdown();
    return 0;
}
```

### Using Base WebNode (without process_list)
```cpp
#include <rclcpp/rclcpp.hpp>
#include "basin/web_core/web_node.hpp"

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<basin::web_core::WebNode>();
    // Register the file upload component to expose /upload
    basin::web_core::FileManagerComponentConfig files_cfg;
    files_cfg.base_route = "/upload";
    files_cfg.files_dir = "/tmp/uploads";
    auto files_component = std::make_shared<basin::web_core::FileManagerComponent>(node, files_cfg);
    node->startServer();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
```

### Multiple Components
```cpp
// You can register multiple components to the same WebNode
auto web_node = std::make_shared<basin::web_core::WebNode>();
auto process_list = std::make_shared<basin::web_core::ProcessListComponent>(web_node);
// Add more components as needed...
web_node->startServer();
```

## CMakeLists.txt Integration

To use the process_list component in your package:

```cmake
find_package(web_core REQUIRED)
find_package(process_msgs REQUIRED)

# Link against the process_list component
target_link_libraries(your_target process_list_web_core_lib)
ament_target_dependencies(your_target web_core process_msgs)
```

To use only the base web_core without process_list:

```cmake
find_package(web_core REQUIRED)

target_link_libraries(your_target web_core_lib)
ament_target_dependencies(your_target web_core)
```

## File Manager Component

This component registers file management endpoints for upload, raw PUT, list, download, and delete.

Usage:

```cpp
#include "basin/web_core/web_node.hpp"
#include "basin/web_core/file_manager_component.hpp"

auto web_node = std::make_shared<basin::web_core::WebNode>();
basin::web_core::FileManagerComponentConfig config;
config.base_route = "/api/files";
config.files_dir = "/tmp/your_files";
auto fm = std::make_shared<basin::web_core::FileManagerComponent>(web_node, config);
web_node->startServer();
```

Endpoints
- `GET /api/files`: list files (JSON)
- `GET /api/files/<filename>`: download file
- `POST /api/files`: upload via multipart/form-data
- `PUT /api/files/<filename>`: upload via raw body
- `DELETE /api/files/<filename>`: delete file

## Zip Stream

`//basin/web/web_core:zip_stream` (`basin/web_core/zip_stream.hpp`) builds a ZIP archive of files, and of
content held in memory, for a route to stream. Entries are stored uncompressed, which suits data that is
already compressed (such as MCAP logs). Every file is opened, measured and checksummed when it is added, so the
archive's exact size is known before it is sent, which gives the browser a progress bar. Its headers need no
data descriptors, so every unzipper reads it. Zip64 records are added only when something reaches 4 GiB.

Files stay open until the stream is destroyed, so a file deleted mid-download is still sent whole. Only the
size a file had when it was added is sent.

```cpp
#include "basin/web_core/zip_stream.hpp"

auto zip = std::make_shared<basin::web_core::ZipStream>();
zip->addFile("bundle/data.mcap", "/var/lib/app/data.mcap");  // throws std::runtime_error if unreadable
zip->addData("bundle/info.yaml", "id: bundle\n");
zip->finish();

res.set_header("Content-Disposition", "attachment; filename=\"bundle.zip\"");
res.set_content_provider(zip->size(), "application/zip",
                         [zip](size_t offset, size_t length, httplib::DataSink& sink) {
                             std::vector<char> buffer(std::min<size_t>(length, 256 * 1024));
                             size_t n = zip->read(offset, buffer.data(), buffer.size());
                             return n > 0 && sink.write(buffer.data(), n);
                         });
```

`read()` serves any byte range, so httplib's handling of `Range` requests works with it too. A stream is not
thread safe; give each download its own. The camera web UI's snapshot download (see
`camera_app/camera_webserver/src/logger/logger_handler.cpp`) uses it.

    "system_name": "example_system",
    "active_config": "default",
    "processes": [
      {
        "process_id": 1,
        "process_name": "node_example",
        "process_status": 2,
        "restart_counter": 0,
        "local_pid": 12345,
        "process_cpu_pct": 2.5,
        "process_memory": 45678
      }
    ]
  }
}
```

## JSON Serialization Library

### Overview

The `json_serialization_lib` provides JSON serialization and deserialization for all ROS2 messages in the basin-interfaces repository using the nlohmann/json library.

### Supported Message Types

#### process_msgs
- `ProcessInfo` - Individual process information
- `ProcessList` - List of processes with system info
- `ProcessHeartbeat` - Process heartbeat with header
- `SystemInfo` - System-wide process information
- `RemoteRequest` (Service) - Remote process control requests

#### status_msgs
- `FaultKey` - Fault identification
- `FaultReport` - Detailed fault reporting
- `KillswitchReport` - Killswitch status
- `SafetyStatus` - Overall safety status with fault reports

#### logging_msgs
- `LogMetadata` - Log file metadata
- `SnapshotRequest` (Service) - Snapshot request/response

#### timesync_msgs
- `SyncStatus` - Time synchronization status

### Usage

#### Include the Header

```cpp
#include "basin/web_core/json_serialization.hpp"

using namespace basin::web_core::json_serialization;
```

#### Serialize a Message to JSON String

```cpp
process_msgs::msg::ProcessInfo process_info;
process_info.process_id = 1;
process_info.process_name = "my_node";
process_info.process_status = 2;

// Convert to JSON string
std::string json_str = message_to_json_string(process_info);
std::cout << json_str << std::endl;
```

#### Deserialize from JSON String

```cpp
std::string json_str = "{\"process_id\":1,\"process_name\":\"my_node\",\"process_status\":2,...}";

// Convert from JSON string
auto process_info = json_string_to_message<process_msgs::msg::ProcessInfo>(json_str);
std::cout << "Process name: " << process_info.process_name << std::endl;
```

#### Working with Service Messages

```cpp
// Request
process_msgs::srv::RemoteRequest::Request req;
req.process_name = "target_node";
req.command = "restart";
std::string req_json = message_to_json_string(req);

// Response
process_msgs::srv::RemoteRequest::Response resp;
resp.resp_code = 200;
resp.response_msg = "Success";
std::string resp_json = message_to_json_string(resp);
```

#### Working with Complex Messages

```cpp
// ProcessList contains nested ProcessInfo messages
process_msgs::msg::ProcessList list;
list.header.stamp.sec = 1234567890;
list.header.stamp.nanosec = 123456789;
list.header.frame_id = "system";
list.system_name = "production";
list.active_config = "default";

process_msgs::msg::ProcessInfo proc;
proc.process_name = "node1";
proc.process_id = 1;
list.processes.push_back(proc);

std::string json_str = message_to_json_string(list);
auto deserialized = json_string_to_message<process_msgs::msg::ProcessList>(json_str);
```

### Linking to Your Package

#### CMakeLists.txt

```cmake
find_package(web_core REQUIRED)

add_executable(my_node src/my_node.cpp)
target_link_libraries(my_node json_serialization_lib)

ament_target_dependencies(my_node
  rclcpp
  process_msgs
  status_msgs
  logging_msgs
  timesync_msgs
  web_core
)
```

#### package.xml

```xml
<depend>web_core</depend>
<depend>process_msgs</depend>
<depend>status_msgs</depend>
<depend>logging_msgs</depend>
<depend>timesync_msgs</depend>
```

### Implementation Details

- Uses nlohmann::json library (v3.11.3+)
- All ROS message fields are mapped to JSON fields with the same names
- Nested messages (like Header, Time) are properly serialized
- Arrays of custom types are handled via manual iteration
- Service request/response messages are supported separately

### Files

- `include/basin/web_core/json_serialization.hpp` - Header with function declarations
- `src/json_serialization.cpp` - Implementation file
- `test/test_json_serialization.cpp` - Basic build/link test

### Building

```bash
cd /path/to/workspace
colcon build --packages-select web_core
```

### Testing

```bash
./install/web_core/lib/web_core/test_json_serialization
```

### Notes

- The library handles all standard ROS types (uint8, uint32, int32, uint64, float32, float64, string, bool)
- Header timestamps are serialized with `sec` and `nanosec` fields
- Arrays and vectors are serialized as JSON arrays
- Ensure all message types are properly built before using the serialization library

## Parameter API Component

### Overview

A new `ParameterApiComponent` has been added to web_core that provides REST API endpoints for managing ROS 2 node parameters via HTTP.

### Features

✅ **List Parameters** - Get all parameters on any ROS 2 node  
✅ **Get Parameter** - Retrieve specific parameter values with type info  
✅ **Set Parameter** - Update parameter values with validation  
✅ **Describe Parameter** - Get detailed parameter metadata and constraints  
✅ **Type Support** - All ROS 2 parameter types (bool, int, double, string, arrays)  
✅ **Error Handling** - Proper HTTP status codes and error messages  

### Files Created

1. **Header**: `include/basin/web_core/parameter_api_component.hpp`
2. **Implementation**: `src/parameter_api_component.cpp`
3. **Documentation**: `PARAMETER_API_USAGE.md` (comprehensive guide)
4. **Example Code**: `examples/example_parameter_api.cpp`
5. **Web UI**: `examples/parameter_ui/index.html` (interactive web interface)

### Quick Start

```cpp
#include "basin/web_core/parameter_api_component.hpp"

auto node = std::make_shared<basin::web_core::WebNode>();
auto param_api = std::make_shared<basin::web_core::ParameterApiComponent>(node);
node->startServer();
```

### API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/parameters/:node` | List all parameters |
| GET | `/api/parameters/:node/:param` | Get parameter value |
| PUT | `/api/parameters/:node/:param` | Set parameter value |
| GET | `/api/parameters/:node/:param/describe` | Get parameter metadata |

### Example Usage

#### List all parameters on a node
```bash
curl http://localhost:8080/api/parameters/web_node
```

#### Get a specific parameter
```bash
curl http://localhost:8080/api/parameters/web_node/port
```

#### Set a parameter
```bash
curl -X PUT http://localhost:8080/api/parameters/web_node/port \
  -H "Content-Type: application/json" \
  -d '{"type": "integer", "value": 9090}'
```

#### Using the Web UI

1. Copy the UI to your static directory:
   ```bash
   mkdir -p /tmp/basin_web_static
   cp examples/parameter_ui/index.html /tmp/basin_web_static/
   ```

2. Run your server with both components:
   ```cpp
   auto static_server = std::make_shared<StaticServerComponent>(node);
   auto param_api = std::make_shared<ParameterApiComponent>(node);
   ```

3. Open `http://localhost:8080/` in your browser

### Configuration

```cpp
basin::web_core::ParameterApiComponentConfig config;
config.base_route = "/api/parameters";  // Custom route
config.timeout_ms = 10000;              // Service timeout

auto param_api = std::make_shared<ParameterApiComponent>(node, config);
```

### Supported Parameter Types

- `bool` / `boolean` - Boolean values
- `integer` / `int` - 64-bit integers
- `double` / `float` - Double precision floats
- `string` - Text strings
- `byte_array` - Byte arrays
- `bool_array` - Boolean arrays
- `integer_array` - Integer arrays
- `double_array` - Double arrays
- `string_array` - String arrays

### Integration Example

```cpp
// Complete web server with multiple components
auto node = std::make_shared<basin::web_core::WebNode>();

// Serve web UI
auto static_server = std::make_shared<basin::web_core::StaticServerComponent>(node);

// Parameter management API
auto param_api = std::make_shared<basin::web_core::ParameterApiComponent>(node);

// File management
auto file_mgr = std::make_shared<basin::web_core::FileManagerComponent>(node);

node->startServer();
```

### Testing

After building and running, test with curl:

```bash
# Test health endpoint
curl http://localhost:8080/health

# List parameters on web_node
curl http://localhost:8080/api/parameters/web_node

# Get the port parameter
curl http://localhost:8080/api/parameters/web_node/port

# Set the port parameter
curl -X PUT http://localhost:8080/api/parameters/web_node/port \
  -H "Content-Type: application/json" \
  -d '{"type": "integer", "value": 9000}'
```

### Common Use Cases

1. **Web Dashboard** - Build web UIs to monitor and adjust parameters
2. **Remote Configuration** - Change robot behavior without SSH access
3. **Testing & Debugging** - Quickly tweak parameters during development
4. **Multi-Node Management** - Manage parameters across multiple nodes from one interface
5. **Parameter Monitoring** - Track parameter changes in real-time

### Error Responses

The API uses proper HTTP status codes:
- `200` - Success
- `400` - Bad request (invalid JSON, wrong type)
- `404` - Parameter not found
- `500` - Server error
- `503` - Service unavailable (node not responding)

### Detailed API Documentation

#### 1. List All Parameters
**GET** `/api/parameters/:node_name`

Lists all parameters on the specified node with their types and values.

**Response:**
```json
{
  "node_name": "example_node",
  "parameters": {
    "use_sim_time": {
      "type": "bool",
      "value": false
    },
    "rate": {
      "type": "double",
      "value": 10.0
    },
    "name": {
      "type": "string",
      "value": "robot_1"
    }
  }
}
```

#### 2. Get Parameter Value
**GET** `/api/parameters/:node_name/:param_name`

Gets the value and type of a specific parameter.

**Response:**
```json
{
  "node_name": "example_node",
  "parameter_name": "rate",
  "type": "double",
  "value": 10.0
}
```

#### 3. Set Parameter Value
**PUT** `/api/parameters/:node_name/:param_name`

Sets a parameter value. The request body must specify the type and value.

**Request Body:**
```json
{
  "type": "double",
  "value": 20.0
}
```

**Response:**
```json
{
  "success": true,
  "node_name": "example_node",
  "parameter_name": "rate",
  "type": "double",
  "value": 20.0
}
```

#### 4. Describe Parameter
**GET** `/api/parameters/:node_name/:param_name/describe`

Gets detailed information about a parameter including description, constraints, and ranges.

**Response:**
```json
{
  "node_name": "example_node",
  "parameter_name": "rate",
  "type": "double",
  "description": "Update rate in Hz",
  "read_only": false,
  "dynamic_typing": false,
  "floating_point_range": {
    "from_value": 1.0,
    "to_value": 100.0,
    "step": 0.1
  }
}
```

## Static Server Component

### What was added

A new `StaticServerComponent` has been added to the web_core package. This component allows you to serve static files (HTML, CSS, JavaScript, images, etc.) from a directory through your web server.

### Files Created

1. **Header**: `include/basin/web_core/static_server_component.hpp`
2. **Implementation**: `src/static_server_component.cpp`
3. **Documentation**: `STATIC_SERVER_USAGE.md` (detailed usage guide)
4. **Example**: `examples/example_static_server.cpp`
5. **Test Page**: `examples/static_test/index.html`

### Quick Example

```cpp
#include "basin/web_core/static_server_component.hpp"

// In your main():
auto node = std::make_shared<basin::web_core::WebNode>();

// Serve static files from root route
basin::web_core::StaticServerComponentConfig config;
config.base_route = "/";
config.static_dir = "/tmp/basin_web_static";
auto static_server = std::make_shared<basin::web_core::StaticServerComponent>(node, config);

node->startServer();
```

### Testing

1. Place the test HTML file in your static directory:
   ```bash
   mkdir -p /tmp/basin_web_static
   cp examples/static_test/index.html /tmp/basin_web_static/
   ```

2. Run your web server with the StaticServerComponent

3. Open `http://localhost:8080/` in your browser

### Integration with Existing Code

The StaticServerComponent works alongside other components like FileManagerComponent and LaunchComponent. You can have multiple components serving different routes:

```cpp
// Serve static website at "/"
auto static_server = std::make_shared<StaticServerComponent>(node, static_config);

// Serve file API at "/upload"
auto file_manager = std::make_shared<FileManagerComponent>(node, upload_config);
```

### Configuration Options

- `base_route`: URL path where files are served (default: `"/"`)
- `static_dir`: Filesystem directory containing files (default: `"/tmp/basin_web_static"`)
- `index_file`: Default file for directory requests (default: `"index.html"`)
- `enable_directory_listing`: Show directory contents when index missing (default: `false`)

### Detailed Usage

#### Basic Usage (Root Route)

```cpp
#include <rclcpp/rclcpp.hpp>
#include "basin/web_core/web_node.hpp"
#include "basin/web_core/static_server_component.hpp"

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<basin::web_core::WebNode>();
    
    // Serve static files from /tmp/my_website on the root route "/"
    basin::web_core::StaticServerComponentConfig config;
    config.base_route = "/";
    config.static_dir = "/tmp/my_website";
    config.index_file = "index.html";
    
    auto static_server = std::make_shared<basin::web_core::StaticServerComponent>(node, config);
    
    node->startServer();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
```

#### Advanced Usage (Custom Route with Directory Listing)

```cpp
#include <rclcpp/rclcpp.hpp>
#include "basin/web_core/web_node.hpp"
#include "basin/web_core/static_server_component.hpp"

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<basin::web_core::WebNode>();
    
    // Serve static files from /var/www/assets on the "/assets" route
    basin::web_core::StaticServerComponentConfig assets_config;
    assets_config.base_route = "/assets";
    assets_config.static_dir = "/var/www/assets";
    assets_config.enable_directory_listing = true;  // Allow browsing directories
    
    auto assets_server = std::make_shared<basin::web_core::StaticServerComponent>(node, assets_config);
    
    node->startServer();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
```

#### Multiple Static Servers

You can mount multiple static servers on different routes:

```cpp
#include <rclcpp/rclcpp.hpp>
#include "basin/web_core/web_node.hpp"
#include "basin/web_core/static_server_component.hpp"

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<basin::web_core::WebNode>();
    
    // Serve main website
    basin::web_core::StaticServerComponentConfig web_config;
    web_config.base_route = "/";
    web_config.static_dir = "/var/www/html";
    auto web_server = std::make_shared<basin::web_core::StaticServerComponent>(node, web_config);
    
    // Serve documentation
    basin::web_core::StaticServerComponentConfig docs_config;
    docs_config.base_route = "/docs";
    docs_config.static_dir = "/var/www/docs";
    auto docs_server = std::make_shared<basin::web_core::StaticServerComponent>(node, docs_config);
    
    // Serve static assets
    basin::web_core::StaticServerComponentConfig assets_config;
    assets_config.base_route = "/assets";
    assets_config.static_dir = "/var/www/assets";
    auto assets_server = std::make_shared<basin::web_core::StaticServerComponent>(node, assets_config);
    
    node->startServer();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
```

#### Configuration Options

##### StaticServerComponentConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `base_route` | `std::string` | `"/"` | The base URL path where files will be served |
| `static_dir` | `std::string` | `"/tmp/basin_web_static"` | The filesystem directory containing static files |
| `index_file` | `std::string` | `"index.html"` | Default file to serve for directory requests |
| `enable_directory_listing` | `bool` | `false` | Enable automatic directory listing when index file is missing |

#### Supported MIME Types

The component automatically detects and sets appropriate MIME types for:

- **Web**: HTML, CSS, JavaScript, JSON, XML
- **Images**: PNG, JPEG, GIF, SVG, ICO, WebP
- **Fonts**: WOFF, WOFF2, TTF, OTF, EOT
- **Media**: MP4, WebM, MP3, WAV, OGG
- **Archives**: ZIP, TAR, GZ
- **Documents**: PDF

Unknown file types default to `application/octet-stream`.

#### Security

The component includes path traversal protection to prevent serving files outside the configured `static_dir`. Any attempt to access files outside this directory will result in a 403 Forbidden response.

#### Example Directory Structure

```
/var/www/html/
├── index.html
├── about.html
├── css/
│   ├── style.css
│   └── theme.css
├── js/
│   ├── app.js
│   └── utils.js
├── images/
│   ├── logo.png
│   └── background.jpg
└── fonts/
    └── custom.woff2
```

With `base_route = "/"` and `static_dir = "/var/www/html"`:
- `http://localhost:8080/` → serves `index.html`
- `http://localhost:8080/about.html` → serves `about.html`
- `http://localhost:8080/css/style.css` → serves `css/style.css`
- `http://localhost:8080/images/logo.png` → serves `images/logo.png`
```
