# Fused GNSS measurement: geodetic position + body orientation expressed in ECEF.
#
# Position follows the sensor_msgs/NavSatFix conventions (WGS84 ellipsoid,
# ENU position covariance).  Orientation is the GNSS body frame expressed as
# a unit quaternion in ECEF, with a separate 3×3 covariance.  Set
# orientation_covariance[0] = -1 when the receiver cannot determine orientation.

std_msgs/Header header

# ── Fix status ────────────────────────────────────────────────────────────────
sensor_msgs/NavSatStatus status

# ── Position ──────────────────────────────────────────────────────────────────
float64 latitude        # Geodetic latitude  (degrees, WGS84)
float64 longitude       # Geodetic longitude (degrees, WGS84)
float64 altitude        # Ellipsoidal height (metres, WGS84; not MSL/orthometric)

# Row-major 3×3 ENU position covariance (m²).  Matches NavSatFix convention.
float64[9] position_covariance

uint8 COVARIANCE_TYPE_UNKNOWN        = 0
uint8 COVARIANCE_TYPE_APPROXIMATED   = 1
uint8 COVARIANCE_TYPE_DIAGONAL_KNOWN = 2
uint8 COVARIANCE_TYPE_KNOWN          = 3
uint8 position_covariance_type

# ── Orientation ───────────────────────────────────────────────────────────────
# Body orientation in ECEF as a unit quaternion.
geometry_msgs/Quaternion orientation

# Row-major 3×3 covariance of orientation error about x, y, z axes (rad²).
# Set [0] = -1 when the receiver cannot determine orientation.
float64[9] orientation_covariance
