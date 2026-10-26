import rclcpp
from rclcpp.node import Node
from sensor_msgs.msg import Image
import cv_bridge
import cv2
import numpy as np

class ImagePublisher(Node):
    def __init__(self):
        super().__init__('image_publisher')
        self.publisher_ = self.create_publisher(Image, '/camera/image_raw', 10)
        self.timer = self.create_timer(0.1, self.timer_callback)
        self.bridge = cv_bridge.CvBridge()
        self.i = 0

    def timer_callback(self):
        # Create a dummy image (green background)
        img = np.zeros((480, 640, 3), dtype=np.uint8)
        img[:] = (0, 255, 0)
        
        # Add a moving rectangle
        x = (self.i * 10) % 640
        cv2.rectangle(img, (x, 200), (x + 50, 250), (0, 0, 255), -1)
        
        msg = self.bridge.cv2_to_imgmsg(img, encoding="bgr8")
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = "camera"
        
        self.publisher_.publish(msg)
        self.i += 1

def main(args=None):
    rclcpp.init(args=args)
    node = ImagePublisher()
    rclcpp.spin(node)
    node.destroy_node()
    rclcpp.shutdown()

if __name__ == '__main__':
    main()
