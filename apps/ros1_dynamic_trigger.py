#!/usr/bin/env python3
"""Start ros1_dynamic_recorder and trigger it from a ROS topic value."""

import argparse
from pathlib import Path
import signal
import subprocess
import sys
import time

import rospy
from std_msgs.msg import Int32


DEFAULT_RECORDER_ARGS = [
    "--recorder.output=./logs/run01",
    "--recorder.post-trigger-timeout-ms=10000",
    "--ros1.spinner-threads=4",
    "--ros1.queue-size=5",
    "--ros1.sources=ros1:/fake/upperlimb/uplimb_state:12000:2000,ros1:/fake/upperlimb/joint_states:12000:2000,ros1:/fake/sine:600:100",
]


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run ros1_dynamic_recorder and send SIGUSR1 when a ROS topic publishes 9."
    )
    parser.add_argument("--trigger-topic", default="/mcapper/trigger", help="ROS topic to watch for trigger values")
    parser.add_argument("--trigger-value", type=int, default=9, help="integer value that sends SIGUSR1")
    parser.add_argument(
        "--recorder-bin",
        default="./build-ros1/ros1_dynamic_recorder",
        help="path to ros1_dynamic_recorder, relative to the project root unless absolute",
    )
    return parser.parse_args(rospy.myargv()[1:])


def project_root():
    return Path(__file__).resolve().parents[1]


def recorder_command(args, root):
    recorder_bin = Path(args.recorder_bin)
    if not recorder_bin.is_absolute():
        recorder_bin = root / recorder_bin

    return [str(recorder_bin), *DEFAULT_RECORDER_ARGS]


def stop_child(child):
    if child.poll() is not None:
        return child.returncode

    child.send_signal(signal.SIGINT)
    try:
        return child.wait(timeout=20.0)
    except subprocess.TimeoutExpired:
        child.terminate()
        try:
            return child.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            child.kill()
            return child.wait()


def main():
    args = parse_args()
    rospy.init_node("ros1_dynamic_trigger", anonymous=True, disable_signals=True)

    root = project_root()
    child = subprocess.Popen(recorder_command(args, root), cwd=str(root))
    shutdown_requested = False
    trigger_active = False

    def request_shutdown(signum, _frame):
        nonlocal shutdown_requested
        shutdown_requested = True
        rospy.loginfo("received signal %s, stopping recorder", signum)

    signal.signal(signal.SIGINT, request_shutdown)
    signal.signal(signal.SIGTERM, request_shutdown)

    def trigger_callback(message):
        nonlocal trigger_active

        is_trigger_active = message.data == args.trigger_value
        if not is_trigger_active:
            trigger_active = False
            return

        if trigger_active:
            return

        trigger_active = True
        if child.poll() is not None:
            rospy.logwarn("trigger ignored because ros1_dynamic_recorder is not running")
            return

        child.send_signal(signal.SIGUSR1)
        rospy.loginfo("sent SIGUSR1 to ros1_dynamic_recorder pid=%s", child.pid)

    try:
        rospy.Subscriber(args.trigger_topic, Int32, trigger_callback, queue_size=10)
        rospy.loginfo("watching %s std_msgs/Int32 for value %s", args.trigger_topic, args.trigger_value)

        while not rospy.is_shutdown() and not shutdown_requested:
            return_code = child.poll()
            if return_code is not None:
                return return_code
            time.sleep(0.2)

        return stop_child(child)
    except Exception as error:
        rospy.logerr("%s", error)
        stop_child(child)
        return 1


if __name__ == "__main__":
    sys.exit(main())
