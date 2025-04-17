#!/system/bin/sh
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. 
# SPDX-License-Identifier: BSD-3-Clause-Clear

AUDIO_DATA_PIPE_PATH="/dev/fifo/audio_data_pipe"
AUDIO_PLAY_PIPE_PATH="/dev/fifo/audio_play_pipe"
CMD_PLAY="agmplay"
SIG_PLAY="play"

play() {
    echo "play audio begin"
    tinymix "WSA RX0 MUX" "AIF1_PB"
    tinymix "WSA RX1 MUX" "AIF1_PB"
    tinymix "WSA_RX0 INP0" "RX0"
    tinymix "WSA_RX1 INP0" "RX1"
    tinymix "WSA_COMP1 Switch" "1"
    tinymix "WSA_COMP2 Switch" "1"
    tinymix "SpkrLeft COMP Switch" "1"
    tinymix "SpkrLeft VISENSE Switch" "1"
    tinymix "SpkrLeft SWR DAC_Port Switch" "1"
    tinymix "SpkrRight COMP Switch" "1"
    tinymix "SpkrRight VISENSE Switch" "1"
    tinymix "SpkrRight SWR DAC_Port Switch" "1"
    $CMD_PLAY $AUDIO_DATA_PIPE_PATH -D 100 -d 100 -num_intf 1 -i CODEC_DMA-LPAIF_WSA-RX-0 -skv 0xA100000E -dkv 0xA2000001 -ikv 1 -dppkv 0xAC000002
    echo "play audio end"
}

while true; do
    echo "wait signal ...."
    if read line < $AUDIO_PLAY_PIPE_PATH; then
        if [ "$line" == $SIG_PLAY ]; then
            echo "receive msg: $line"
            play
        fi
    else
        sleep 2
    fi
done
