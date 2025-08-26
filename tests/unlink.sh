#!/bin/bash
#
# Test for dir .unlink
#

set -e

MOUNT_POINT=$1
NUMBFS_ROOT=$2
IMAGE_NAME=$3

echo "Testing unlink functionality"

echo "Test 1: Creating test file"
if ! sudo touch "$MOUNT_POINT/test_file" 2> /tmp/touch_error.log; then
    echo "FAIL: Failed to create test file"
    cat /tmp/touch_error.log
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Test file created successfully"

sudo umount $MOUNT_POINT

echo "Test 2: Remounting and verifying file exists"
sudo mount -t numbfs -o loop $NUMBFS_ROOT/$IMAGE_NAME $MOUNT_POINT
if ! sudo test -f "$MOUNT_POINT/test_file"; then
    echo "FAIL: Test file does not exist after remount"
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Test file exists after remount"

echo "Test 3: Removing test file with unlink"
if ! sudo unlink "$MOUNT_POINT/test_file" 2> /tmp/unlink_error.log; then
    echo "FAIL: Failed to unlink test file"
    cat /tmp/unlink_error.log
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Test file unlinked successfully"

sudo umount $MOUNT_POINT

echo "Test 4: Remounting and verifying file is gone"
sudo mount -t numbfs -o loop $NUMBFS_ROOT/$IMAGE_NAME $MOUNT_POINT
if sudo test -f "$MOUNT_POINT/test_file"; then
    echo "FAIL: Test file still exists after unlink and remount"
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Test file no longer exists after unlink and remount"

echo "All tests passed for unlink functionality"
