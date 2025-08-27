#!/bin/bash
#
# Test for file read/write
#

set -e

MOUNT_POINT=$1
NUMBFS_ROOT=$2
IMAGE_NAME=$3

echo "Testing file read/write functionality"

TEST_CONTENT=$(head -c 1025 /dev/urandom | base64 -w 0)
TEST_FILE="$MOUNT_POINT/test_file_rw"

echo "Test 1: Creating test file"
if ! sudo touch "$TEST_FILE" 2> /tmp/touch_error.log; then
    echo "FAIL: Failed to create test file"
    cat /tmp/touch_error.log
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Test file created successfully"

# run as root
sudo bash -c bash

echo "Test 2: Writing 1025 bytes to test file"
if ! echo "$TEST_CONTENT" | sudo tee "$TEST_FILE" > /dev/null 2> /tmp/write_error.log; then
    echo "FAIL: Failed to write to test file"
    cat /tmp/write_error.log
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: 1025 bytes written successfully"

echo "Test 3: Unmounting filesystem"
sudo umount $MOUNT_POINT
echo "SUCCESS: Filesystem unmounted"

echo "Test 4: Remounting filesystem"
sudo mount -t numbfs -o loop "$NUMBFS_ROOT/$IMAGE_NAME" $MOUNT_POINT
echo "SUCCESS: Filesystem remounted"

echo "Test 5: Reading file content and verifying it matches"
READ_CONTENT=$(sudo cat "$TEST_FILE")
if [ "$READ_CONTENT" != "$TEST_CONTENT" ]; then
    echo "FAIL: File content does not match original content"
    echo "Original length: ${#TEST_CONTENT}, Read length: ${#READ_CONTENT}"
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: File content matches original content"

echo "Test 6: Cleaning up test file"
if ! sudo unlink "$TEST_FILE" 2> /tmp/cleanup_error.log; then
    echo "WARNING: Failed to clean up test file"
    cat /tmp/cleanup_error.log
fi

exit

echo "All tests passed for file read/write functionality"
