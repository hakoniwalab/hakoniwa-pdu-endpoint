#!/usr/bin/env python3
import os

import rclpy
from rclpy.node import Node
from std_msgs.msg import UInt64


class UInt64Publisher(Node):
    def __init__(self):
        super().__init__("hakoniwa_rmw_zenoh_uint64_pub")
        self.topic = os.environ.get("HAKO_RMW_ZENOH_TOPIC", "/sample_state")
        self.count_limit = int(os.environ.get("HAKO_RMW_ZENOH_COUNT", "5"))
        period = float(os.environ.get("HAKO_RMW_ZENOH_PERIOD", "0.2"))
        self.publisher = self.create_publisher(UInt64, self.topic, 10)
        self.count = 0
        self.timer = self.create_timer(period, self.publish_next)

    def publish_next(self):
        self.count += 1
        msg = UInt64()
        msg.data = self.count
        self.publisher.publish(msg)
        self.get_logger().info(f"published {self.topic}={msg.data}")
        if self.count >= self.count_limit:
            rclpy.shutdown()


def main():
    rclpy.init()
    node = UInt64Publisher()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()


if __name__ == "__main__":
    main()

