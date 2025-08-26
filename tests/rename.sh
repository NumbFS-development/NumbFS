#!/bin/bash
#
# Test for dir .rename
#

set -e

MOUNT_POINT=$1
NUMBFS_ROOT=$2
IMAGE_NAME=$3

echo "Testing rename functionality"

echo "Test 1: Renaming a file"
if ! sudo touch "$MOUNT_POINT/original_file" 2> /tmp/touch_error.log; then
    echo "FAIL: Failed to create test file"
    cat /tmp/touch_error.log
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Test file created"

if ! sudo mv "$MOUNT_POINT/original_file" "$MOUNT_POINT/renamed_file" 2> /tmp/rename_error.log; then
    echo "FAIL: Failed to rename file"
    cat /tmp/rename_error.log
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: File renamed successfully"

if ! sudo test -f "$MOUNT_POINT/renamed_file"; then
    echo "FAIL: Renamed file does not exist"
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Renamed file exists"

if sudo test -f "$MOUNT_POINT/original_file"; then
    echo "FAIL: Original file still exists after rename"
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Original file no longer exists"

echo "Test 2: Renaming a directory"
if ! sudo mkdir "$MOUNT_POINT/original_dir" 2> /tmp/mkdir_error.log; then
    echo "FAIL: Failed to create test directory"
    cat /tmp/mkdir_error.log
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Test directory created"

if ! sudo mv "$MOUNT_POINT/original_dir" "$MOUNT_POINT/renamed_dir" 2> /tmp/rename_dir_error.log; then
    echo "FAIL: Failed to rename directory"
    cat /tmp/rename_dir_error.log
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Directory renamed successfully"

if ! sudo test -d "$MOUNT_POINT/renamed_dir"; then
    echo "FAIL: Renamed directory does not exist"
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Renamed directory exists"

if sudo test -d "$MOUNT_POINT/original_dir"; then
    echo "FAIL: Original directory still exists after rename"
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Original directory no longer exists"

echo "Test 3: Moving file to different directory"
if ! sudo mkdir "$MOUNT_POINT/target_dir" 2> /tmp/mkdir2_error.log; then
    echo "FAIL: Failed to create target directory"
    cat /tmp/mkdir2_error.log
    sudo dmesg | tail -200
    exit 1
fi

if ! sudo mv "$MOUNT_POINT/renamed_file" "$MOUNT_POINT/target_dir/moved_file" 2> /tmp/move_error.log; then
    echo "FAIL: Failed to move file to different directory"
    cat /tmp/move_error.log
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: File moved to different directory"

if ! sudo test -f "$MOUNT_POINT/target_dir/moved_file"; then
    echo "FAIL: Moved file does not exist in target directory"
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Moved file exists in target directory"

if sudo test -f "$MOUNT_POINT/renamed_file"; then
    echo "FAIL: File still exists in original location after move"
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: File no longer exists in original location"

echo "Test 4: Testing persistence after remount"
sudo umount $MOUNT_POINT

sudo mount -t numbfs -o loop $NUMBFS_ROOT/$IMAGE_NAME $MOUNT_POINT

if ! sudo test -f "$MOUNT_POINT/target_dir/moved_file"; then
    echo "FAIL: Moved file does not persist after remount"
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Moved file persists after remount"

if ! sudo test -d "$MOUNT_POINT/renamed_dir"; then
    echo "FAIL: Renamed directory does not persist after remount"
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Renamed directory persists after remount"

echo "All tests passed for rename functionality"
