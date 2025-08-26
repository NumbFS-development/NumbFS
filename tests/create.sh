#!/bin/bash
#
# Test for dir .create
#

set -e

MOUNT_POINT=$1
NUMBFS_ROOT=$2

echo "Testing directory creation functionality"

echo "Test 1: Creating test file in root directory"
if ! sudo touch "$MOUNT_POINT/test_file" 2> /tmp/touch_error.log; then
    echo "FAIL: Failed to create test file"
    sleep 2
    echo "kernel messages:"
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Test file created successfully"

sudo umount $MOUNT_POINT

sudo mount -t numbfs -o loop $NUMBFS_ROOT/$IMAGE_NAME $MOUNT_POINT

echo "Test 2: Verifying test file exists"
if sudo test -f "$MOUNT_POINT/test_file"; then
    echo "PASS: Test file created successfully"
else
    echo "FAIL: Test file was not created"
    exit 1
fi
