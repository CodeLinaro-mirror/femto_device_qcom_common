#!/system/bin/sh
# Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear

mkdir -p /mnt/lxc/run
/system/bin/brctl addbr lxcbr0
lxc-start lv
