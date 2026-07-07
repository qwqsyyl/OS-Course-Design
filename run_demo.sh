#!/bin/sh
set -eu

DEV=/dev/vchar_device
SYSFS=/sys/class/vchar_device/vchar_device/status

make

if lsmod | grep -q '^vchar_device'; then
  sudo rmmod vchar_device
fi

sudo insmod vchar_device.ko

if [ ! -e "$DEV" ]; then
  echo "$DEV was not created"
  exit 1
fi

sudo chmod 666 "$DEV"

./test_vchar "course design virtual char device test"

echo
echo "status:"
cat "$SYSFS"

echo
echo "kernel log:"
sudo dmesg | tail -n 20

echo
echo "unload module:"
sudo rmmod vchar_device
