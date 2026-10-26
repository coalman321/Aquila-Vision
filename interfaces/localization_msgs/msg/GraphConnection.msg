int64 parent # parent node to link from
int64 child  # Child node to link to

uint8 link_type # type of the link
uint8 LINK_TYPE_ODOM = 1 # Odom link
uint8 LINK_TYPE_DETECT = 2 # object detection link
uint8 LINK_TYPE_PRIOR = 3 # Prior link (only child is valid)