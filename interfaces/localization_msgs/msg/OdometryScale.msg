# Online odometry scale factor estimate produced by the odometry_scale node.
# Provides a multiplicative correction s such that v_true ≈ s · v_odom.
# See basin-smooth-pose/doc/scale_factor_estimation.md.

std_msgs/Header header        # stamp of the last accepted GPS update.

float64 scale                 # Current scale estimate s.
float64 variance              # Estimator covariance P (scale units squared).

# Status of the scale estimator.
uint8 STATUS_CONVERGED=0      # Actively accepting GPS updates, P is small.
uint8 STATUS_HOLDING=1        # Gates blocked recent updates; holding last s.
uint8 STATUS_UNOBSERVABLE=2   # No valid updates since init; P growing.

uint8 status                  # One of STATUS_* above.
float64 speed_used            # v_odom (m/s) at the last accepted GPS update.
