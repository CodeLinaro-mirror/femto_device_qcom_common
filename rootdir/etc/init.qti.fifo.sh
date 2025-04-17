#!/system/bin/sh
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. 
# SPDX-License-Identifier: BSD-3-Clause-Clear

if [ ! -d /dev/fifo ] ; then
    mkdir -p /dev/fifo
    chown audio /dev/fifo
fi
if [ ! -p /dev/fifo/audio_data_pipe ] ; then
    mkfifo /dev/fifo/audio_data_pipe
    chown audio /dev/fifo/audio_data_pipe
fi
if [ ! -p /dev/fifo/audio_play_pipe ] ; then
    mkfifo /dev/fifo/audio_play_pipe
    chown audio /dev/fifo/audio_play_pipe
fi
if [ ! -p /dev/fifo/audio_stop_pipe ] ; then
    mkfifo /dev/fifo/audio_stop_pipe
    chown audio /dev/fifo/audio_stop_pipe
fi
