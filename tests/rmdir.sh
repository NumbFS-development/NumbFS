#!/bin/bash
#
# Test for dir .rmdir
#

set -e

MOUNT_POINT=$1
NUMBFS_ROOT=$2
IMAGE_NAME=$3

echo "Testing rmdir functionality"

echo "Test 1: Creating test directory"
if ! sudo mkdir "$MOUNT_POINT/dir" 2> /tmp/mkdir_error.log; then
    echo "FAIL: Failed to create test directory"
    cat /tmp/mkdir_error.log
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Test directory created successfully"

echo "Test 2: Creating file inside test directory"
if ! sudo touch "$MOUNT_POINT/dir/test_file" 2> /tmp/touch_error.log; then
    echo "FAIL: Failed to create test file inside directory"
    cat /tmp/touch_error.log
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Test file created inside directory"

echo "Test 3: Attempting to remove non-empty directory (should fail)"
if sudo rmdir "$MOUNT_POINT/dir" 2> /tmp/rmdir_error.log; then
    echo "FAIL: Should not be able to remove non-empty directory"
    sudo dmesg | tail -200
    exit 1
else
    echo "SUCCESS: Correctly failed to remove non-empty directory"
    if ! grep -q "not empty\|NotEmpty\|ENOTEMPTY" /tmp/rmdir_error.log; then
        echo "WARNING: Unexpected error when trying to remove non-empty directory"
        cat /tmp/rmdir_error.log
    fi
fi

echo "Test 4: Removing file from directory"
if ! sudo unlink "$MOUNT_POINT/dir/test_file" 2> /tmp/unlink_error.log; then
    echo "FAIL: Failed to remove test file"
    cat /tmp/unlink_error.log
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Test file removed successfully"

echo "Test 5: Removing empty directory"
if ! sudo rmdir "$MOUNT_POINT/dir" 2> /tmp/rmdir_error2.log; then
    echo "FAIL: Failed to remove empty directory"
    cat /tmp/rmdir_error2.log
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Empty directory removed successfully"

sudo umount $MOUNT_POINT

echo "Test 6: Remounting filesystem"
sudo mount -t numbfs -o loop $NUMBFS_ROOT/$IMAGE_NAME $MOUNT_POINT

echo "Test 7: Verifying directory no longer exists"
if sudo test -d "$MOUNT_POINT/dir"; then
    echo "FAIL: Directory still exists after removal and remount"
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Directory no longer exists after removal and remount"

echo "All tests passed for rmdir functionality"
