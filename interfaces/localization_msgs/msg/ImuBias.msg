# Online IMU bias estimate produced by the GTSAM fixed-lag backend.
# Consumed by the smooth-pose ESKF as a measurement update on its bias states
# and as the feedforward bias used to correct raw IMU (ramped to avoid steps).
# See basin-smooth-pose/doc/smooth_pose_eskf_design.md §3.

std_msgs/Header header       # stamp = t_anchor on the IMU clock (NOT wall clock).
                             # frame_id = body/IMU frame the biases are expressed in.

float64[3] accel_bias        # Accelerometer bias b_a (m/s^2), body frame.
float64[3] gyro_bias         # Gyroscope bias b_g (rad/s), body frame.

# Row-major 6x6 covariance over [b_a(0..2), b_g(0..2)].
# Diagonal units: m^2/s^4 (accel) and rad^2/s^2 (gyro).
# Used directly as R in the ESKF bias measurement update.
float64[36] covariance

uint64 keyframe_id           # Backend keyframe index. Used by the EKF node to
                             # detect backend stall (non-advancing id). See §7.
