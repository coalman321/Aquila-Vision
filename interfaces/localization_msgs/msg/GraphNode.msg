uint64 node_id # The ID of this node

geometry_msgs/Pose pose # The optimized pose of the node

uint8 node_type # The type of the node
uint8 NODE_TYPE_ODOM = 1 # Odometry nodes
uint8 NODE_TYPE_LAND = 2 # Landmark node
