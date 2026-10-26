# name of the camera being calibrated
string camera_name

# Session id for the active calibration
uint64 session_id
uint64 INACTIVE_SESSION=0

# number of calibration samples collected
uint16 cal_samples

# minimum and maximum x spread [-1, 1]
float32 min_x 
float32 max_x

# minimum and maximum y spread [-1, 1]
float32 min_y
float32 max_y

# skew and scale
float32 max_skew
float32 max_scale

# -1 if calibration isnt complete, if > 0 value
float32 reproj_err

