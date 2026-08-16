#!/usr/bin/env python3
"""Publish one ROS1 topic at 10 Hz with Header + sine data."""

import argparse
import math

import rospy
from sensor_msgs.msg import Temperature


def parse_args():
    parser = argparse.ArgumentParser(description="Publish a 10 Hz Header + sine data topic.")
    parser.add_argument("--topic", default="/fake/sine", help="topic name to publish")
    parser.add_argument("--rate", type=float, default=10.0, help="publish rate in Hz")
    parser.add_argument("--amplitude", type=float, default=1.0, help="sine amplitude")
    parser.add_argument("--frequency", type=float, default=1.0, help="sine frequency in Hz")
    parser.add_argument("--frame-id", default="sine", help="Header frame_id")
    return parser.parse_args(rospy.myargv()[1:])


def main():
    args = parse_args()
    rospy.init_node("dynamic_sine_pub", anonymous=True)

    pub = rospy.Publisher(args.topic, Temperature, queue_size=10)
    rate = rospy.Rate(args.rate)
    start_time = rospy.Time.now()
    seq = 0

    while not rospy.is_shutdown():
        now = rospy.Time.now()
        t = (now - start_time).to_sec()

        msg = Temperature()
        msg.header.seq = seq
        msg.header.stamp = now
        msg.header.frame_id = args.frame_id
        msg.temperature = args.amplitude * math.sin(2.0 * math.pi * args.frequency * t)
        msg.variance = 0.0

        pub.publish(msg)
        seq += 1
        rate.sleep()


if __name__ == "__main__":
    main()
