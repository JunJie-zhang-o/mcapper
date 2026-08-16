#!/usr/bin/env bash




./build-ros1/ros1_dynamic_recorder --output=./logs/run01 \
    --pre-trigger-sec=60 \
    --post-trigger-sec=10 \
    ros1:/fake/upperlimb/uplimb_state \
    ros1:/fake/upperlimb/joint_states

