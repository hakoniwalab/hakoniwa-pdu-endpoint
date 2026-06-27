#!/usr/bin/env python3
import os
import sys

import rclpy
from rclpy.node import Node
from std_msgs.msg import UInt64


class UInt64Subscriber(Node):
    def __init__(self):
        super().__init__("hakoniwa_rmw_zenoh_uint64_sub")
        self.topic = os.environ.get("HAKO_RMW_ZENOH_TOPIC", "/sample_state")
        self.count_limit = int(os.environ.get("HAKO_RMW_ZENOH_COUNT", "5"))
        self.timeout_sec = float(os.environ.get("HAKO_RMW_ZENOH_TIMEOUT", "8.0"))
        self.values = []
        self.create_subscription(UInt64, self.topic, self.on_message, 10)
        self.create_timer(self.timeout_sec, self.on_timeout)

    def on_message(self, msg):
        self.values.append(msg.data)
        self.get_logger().info(f"received {self.topic}={msg.data}")
        if len(self.values) >= self.count_limit:
            rclpy.shutdown()

    def on_timeout(self):
        self.get_logger().error(
            f"timeout waiting for {self.count_limit} samples on {self.topic}; "
            f"received {len(self.values)}"
        )
        rclpy.shutdown()


def main():
    rclpy.init()
    node = UInt64Subscriber()
    try:
        rclpy.spin(node)
    finally:
        received = len(node.values)
        expected = node.count_limit
        node.destroy_node()
    return 0 if received >= expected else 1


if __name__ == "__main__":
    raise SystemExit(main())
