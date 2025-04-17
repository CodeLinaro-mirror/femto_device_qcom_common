#!/system/bin/sh
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. 
# SPDX-License-Identifier: BSD-3-Clause-Clear

CMD_PLAY=agmplay
AUDIO_STOP_PIPE_PATH="/dev/fifo/audio_stop_pipe"
SIG_STOP="stop"

stop() {
    echo "stop audio"
    pkill "$CMD_PLAY"
}


while true; do
    echo "wait signal ...."
    if read line < $AUDIO_STOP_PIPE_PATH; then
        if [ "$line" == $SIG_STOP ]; then
            echo "receive msg: $line"
            stop
        fi
    else
        sleep 2
    fi
done
