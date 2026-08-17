#!/usr/bin/env python3
"""Start ros1_dynamic_recorder and trigger it from a ROS topic value."""

from pathlib import Path
import signal
import subprocess
import sys
import time

import rospy
# from std_msgs.msg import Int32
from zj_robot.msg import RobotState


DEFAULT_RECORDER_ARGS = [
    "--recorder.output=./logs/run01",
    "--recorder.post-trigger-timeout-ms=10000",
    "--ros1.spinner-threads=4",
    "--ros1.queue-size=5",
    "--ros1.sources=ros1:/fake/upperlimb/uplimb_state:12000:2000,ros1:/fake/upperlimb/joint_states:12000:2000,ros1:/fake/sine:600:100",
]




RECORD_PRE_TIME  = 2 * 60 * 1.2  # 2 * 60 seconds  
RECORD_POST_TIME = 10 * 1.2      # 10 seconds



class RecorderFrequency:

    uplimb_state = 200
    joint_states = 200
    robot_state  = 5
    cmd_vel      = 10 
    battery_info = 5
    walk_state = 50
    uplimb_occupation = 50
    occupancy_state = 50


TRIGGER_TOPIC = "/zj_humanoid/robot/robot_state"
TRIGGER_VALUE = 9
RECORDER_BIN = "./ros1_dynamic_recorder"


DEFAULT_RECORDER_ARGS = [
    "--recorder.output=./logs/run01",
    f"--recorder.post-trigger-timeout-ms={RECORD_POST_TIME * 1000}",
    "--ros1.spinner-threads=4",
    "--ros1.queue-size=5",
    (
        f"--ros1.sources="
        f"ros1:/zj_humanoid/upperlimb/uplimb_state:{RecorderFrequency.uplimb_state * RECORD_PRE_TIME}:{RecorderFrequency.uplimb_state * RECORD_POST_TIME},"
        f"ros1:/zj_humanoid/upperlimb/joint_states:{RecorderFrequency.joint_states * RECORD_PRE_TIME}:{RecorderFrequency.joint_states * RECORD_POST_TIME},"
        f"ros1:/cmd_vel:{600 * RECORD_PRE_TIME}:{RecorderFrequency.cmd_vel * RECORD_POST_TIME},"
        f"ros1:/zj_humanoid/robot/robot_state:{RecorderFrequency.robot_state * RECORD_PRE_TIME}:{RecorderFrequency.robot_state * RECORD_POST_TIME},"
        f"ros1:/zj_humanoid/robot/battery_info:{RecorderFrequency.battery_info * RECORD_PRE_TIME}:{RecorderFrequency.battery_info * RECORD_POST_TIME},"
        f"ros1:/zj_humanoid/lowerlimb/walk_state:{RecorderFrequency.walk_state * RECORD_PRE_TIME}:{RecorderFrequency.walk_state * RECORD_POST_TIME},"
        f"ros1:/zj_humanoid/upperlimb/uplimb_occupation:{RecorderFrequency.uplimb_occupation * RECORD_PRE_TIME}:{RecorderFrequency.uplimb_occupation * RECORD_POST_TIME},"
        f"ros1:/zj_humanoid/upperlimb/occupancy_state:{RecorderFrequency.occupancy_state * RECORD_PRE_TIME}:{RecorderFrequency.occupancy_state * RECORD_POST_TIME}"
    ),
]

def recorder_command(root):
    recorder_bin = Path(RECORDER_BIN)
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
    rospy.init_node("ros1_dynamic_trigger", anonymous=True, disable_signals=True)

    root = Path(__file__).resolve().parents[0]
    child = subprocess.Popen(recorder_command(root), cwd=str(root))
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

        is_trigger_active = message.data == TRIGGER_VALUE
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
        rospy.Subscriber(TRIGGER_TOPIC, RobotState, trigger_callback, queue_size=10)
        rospy.loginfo("watching %s zj_robot/RobotState for value %s", TRIGGER_TOPIC, TRIGGER_VALUE)

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
