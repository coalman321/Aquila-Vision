# Field Map Handler API

The Field Map Handler component allows users to manage a list of positions on the field. Each position includes a 3D pose (position and orientation) using `basin::jsonable::Pose3d`.

## API Endpoints

### List All Positions
**GET** `/api/field_map/positions`

Returns an array of all configured field positions.

**Response:**
```json
[
  {
    "id": "position1",
    "pose": {
      "x": 0.0,
      "y": 0.0,
      "z": 0.0,
      "roll": 0.0,
      "pitch": 0.0,
      "yaw": 0.0
    },
    "description": "Starting position"
  },
  {
    "id": "position2",
    "pose": {
      "x": 10.5,
      "y": 5.2,
      "z": 0.0,
      "roll": 0.0,
      "pitch": 0.0,
      "yaw": 1.57
    },
    "description": "First target location"
  }
]
```

### Get Specific Position
**GET** `/api/field_map/position?id={position_id}`

Returns details for a specific position.

**Query Parameters:**
- `id` (required): Unique identifier for the position

**Response:**
```json
{
  "id": "position1",
  "pose": {
    "x": 0.0,
    "y": 0.0,
    "z": 0.0,
    "roll": 0.0,
    "pitch": 0.0,
    "yaw": 0.0
  },
  "description": "Starting position"
}
```

**Error Response (404):**
```json
{
  "error": "Position not found."
}
```

### Create or Update Position
**POST** `/api/field_map/position`

Creates a new position or updates an existing one. If a position with the given `id` exists, it will be updated; otherwise, a new position is created.

**Request Body:**
```json
{
  "id": "position1",
  "pose": {
    "x": 0.0,
    "y": 0.0,
    "z": 0.0,
    "roll": 0.0,
    "pitch": 0.0,
    "yaw": 0.0
  },
  "description": "Starting position"
}
```

**Response:**
```json
{
  "message": "Position updated successfully.",
  "id": "position1"
}
```

**Error Response (400):**
```json
{
  "error": "Position ID is required."
}
```

### Delete Position
**DELETE** `/api/field_map/position?id={position_id}`

Deletes a position by its ID.

**Query Parameters:**
- `id` (required): Unique identifier for the position to delete

**Response:**
```json
{
  "message": "Position deleted successfully."
}
```

**Error Response (404):**
```json
{
  "error": "Position not found."
}
```

## Configuration

The Field Map Handler stores position data as JSON files in the configured directory (default: `/etc/app/field_positions`).

Each position is stored in a separate file named `{id}.json`.

## Usage Example

```cpp
// In camera_web_node.cpp
field_map::FieldMapHandlerConfig field_map_config;
field_map_config.config_dir = app_path / "field_positions";
field_map_handler_ = std::make_unique<field_map::FieldMapHandler>(web_node, field_map_config);
```

## Field Position Structure

- `id` (string, required): Unique identifier for the position
- `pose` (Pose3d): 3D pose containing:
  - `x` (float): X coordinate on the field
  - `y` (float): Y coordinate on the field
  - `z` (float): Z coordinate (height)
  - `roll` (float): Roll angle in radians
  - `pitch` (float): Pitch angle in radians
  - `yaw` (float): Yaw angle in radians
- `description` (string): Optional description
