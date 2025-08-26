#!/bin/bash
#
# Test for dir .mkdir
#

set -e

MOUNT_POINT=$1
NUMBFS_ROOT=$2
IMAGE_NAME=$3

echo "Testing mkdir functionality"

echo "Test 1: Creating test directory in root directory"
if ! sudo mkdir "$MOUNT_POINT/test_dir" 2> /tmp/mkdir_error.log; then
    echo "FAIL: Failed to create test directory"
    cat /tmp/mkdir_error.log
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Test directory created successfully"

sudo umount $MOUNT_POINT

echo "Remounting filesystem..."
sudo mount -t numbfs -o loop $NUMBFS_ROOT/$IMAGE_NAME $MOUNT_POINT

echo "Test 2: Verifying test directory exists"
if sudo test -d "$MOUNT_POINT/test_dir"; then
    echo "PASS: Test directory persists after remount"
else
    echo "FAIL: Test directory was not persisted"
    sudo dmesg | tail -200
    exit 1
fi

echo "All tests passed for mkdir functionality"
