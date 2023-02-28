#! /vendor/bin/sh

# Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear

# Add New Chipsets bases on soc_id when testChipsetinfo Fails

if [ -f /sys/devices/soc0/soc_id ]; then
    soc_id=`cat /sys/devices/soc0/soc_id`
else
    soc_id=`cat /sys/devices/system/soc/soc0/id`
fi

if [ "$soc_id" == "340" ] || [ "$soc_id" == "405" ]; then
    setprop ro.vendor.qti.soc_model SA8195P
elif [ "$soc_id" == "460" ]; then
    setprop ro.vendor.qti.soc_model SA8295P
elif [ "$soc_id" == "367" ] || [ "$soc_id" == "362" ]; then
    setprop ro.vendor.qti.soc_model SA8155P
fi


