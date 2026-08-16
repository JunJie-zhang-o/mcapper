#!/usr/bin/env bash




./build-ros1/ros1_dynamic_recorder \
    --recorder.output=./logs/run01 \
    --recorder.post-trigger-timeout-ms=10000 \
    --ros1.spinner-threads=4 \
    --ros1.queue-size=5 \
    --ros1.sources=ros1:/fake/upperlimb/uplimb_state:12000:2000,ros1:/fake/upperlimb/joint_states:12000:2000,ros1:/fake/sine:600:100
