#! /system/bin/sh

for rpc in `ls /sys/class/remoteproc/`
do
 name=$(cat /sys/class/remoteproc/$rpc/name)
 case "$name" in
 *adsp)
 echo start > /sys/class/remoteproc/$rpc/state
 ;;
 esac
done
