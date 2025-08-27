#!/bin/bash
#
# Test for dir .link and .symlink
#

set -e

MOUNT_POINT=$1
NUMBFS_ROOT=$2
IMAGE_NAME=$3

echo "Testing link and symlink functionality"

sudo umount $MOUNT_POINT

sudo mkfs.numbfs $NUMBFS_ROOT/$IMAGE_NAME
sudo mount -t numbfs -o loop $NUMBFS_ROOT/$IMAGE_NAME $MOUNT_POINT

echo "Creating test files tst1 to tst10"
for i in {1..10}; do
    if ! sudo touch "$MOUNT_POINT/tst$i" 2> /tmp/touch_error_$i.log; then
        echo "FAIL: Failed to create tst$i"
        cat /tmp/touch_error_$i.log
        sudo dmesg | tail -200
        exit 1
    fi
done
echo "SUCCESS: Test files tst1 to tst10 created"

echo "Test 1: Creating hard link"
if ! sudo touch "$MOUNT_POINT/original_file" 2> /tmp/touch_error.log; then
    echo "FAIL: Failed to create original file"
    cat /tmp/touch_error.log
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Original file created"

if ! sudo ln "$MOUNT_POINT/original_file" "$MOUNT_POINT/hard_link" 2> /tmp/link_error.log; then
    echo "FAIL: Failed to create hard link"
    cat /tmp/link_error.log
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Hard link created"

orig_inode=$(sudo ls -i "$MOUNT_POINT/original_file" | awk '{print $1}')
link_inode=$(sudo ls -i "$MOUNT_POINT/hard_link" | awk '{print $1}')

if [ "$orig_inode" != "$link_inode" ]; then
    echo "FAIL: Hard link does not share the same inode as original file"
    echo "Original inode: $orig_inode, Link inode: $link_inode"
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Hard link shares same inode as original file"

echo "Test 2: Creating symbolic link"
if ! sudo ln -s "$MOUNT_POINT/original_file" "$MOUNT_POINT/sym_link" 2> /tmp/symlink_error.log; then
    echo "FAIL: Failed to create symbolic link"
    cat /tmp/symlink_error.log
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Symbolic link created"

if [ "$(sudo readlink "$MOUNT_POINT/sym_link")" != "$MOUNT_POINT/original_file" ]; then
    echo "FAIL: Symbolic link does not point to correct target"
    echo "Expected: original_file, Got: $(sudo readlink "$MOUNT_POINT/sym_link")"
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Symbolic link points to correct target"

echo "Test 3: Testing persistence after remount"
sudo umount $MOUNT_POINT

sudo mount -t numbfs -o loop $NUMBFS_ROOT/$IMAGE_NAME $MOUNT_POINT

if ! sudo test -f "$MOUNT_POINT/hard_link"; then
    echo "FAIL: Hard link does not persist after remount"
    sudo dmesg | tail -200
    exit 1
fi

new_orig_inode=$(sudo ls -i "$MOUNT_POINT/original_file" | awk '{print $1}')
new_link_inode=$(sudo ls -i "$MOUNT_POINT/hard_link" | awk '{print $1}')

if [ "$new_orig_inode" != "$new_link_inode" ]; then
    echo "FAIL: Hard link inode mismatch after remount"
    echo "Original inode: $new_orig_inode, Link inode: $new_link_inode"
    sudo dmesg | tail -200
    exit 1
fi

if ! sudo test -L "$MOUNT_POINT/sym_link"; then
    echo "FAIL: Symbolic link does not persist after remount"
    sudo dmesg | tail -200
    exit 1
fi

if [ "$(sudo readlink "$MOUNT_POINT/sym_link")" != "$MOUNT_POINT/original_file" ]; then
    echo "FAIL: Symbolic link target incorrect after remount"
    echo "Expected: $MOUNT_POINT/original_file, Got: $(sudo readlink "$MOUNT_POINT/sym_link")"
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Both hard and symbolic links persist correctly after remount"

echo "Test 4: Testing link behavior when original file is deleted"
sudo rm -f "$MOUNT_POINT/original_file"

if ! sudo test -f "$MOUNT_POINT/hard_link"; then
    echo "FAIL: Hard link should still be accessible after original file deletion"
    sudo dmesg | tail -200
    exit 1
fi

if sudo test -f "$MOUNT_POINT/sym_link"; then
    echo "FAIL: Symbolic link should not resolve after original file deletion"
    sudo dmesg | tail -200
    exit 1
fi

if ! sudo test -L "$MOUNT_POINT/sym_link"; then
    echo "FAIL: Symbolic link itself should still exist after original file deletion"
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Links behave correctly after original file deletion"

echo "Test 5: Testing directory symbolic link"
sudo mkdir "$MOUNT_POINT/test_dir"
sudo touch "$MOUNT_POINT/test_dir/file"

if sudo ln -s "test_dir" "$MOUNT_POINT/dir_link" 2> /tmp/dirsymlink_error.log; then
    echo "SUCCESS: Directory symbolic link created"

    if ! sudo test -L "$MOUNT_POINT/dir_link"; then
        echo "FAIL: Directory symbolic link does not exist"
        sudo dmesg | tail -200
        exit 1
    fi

    if [ "$(sudo readlink "$MOUNT_POINT/dir_link")" != "test_dir" ]; then
        echo "FAIL: Directory symbolic link does not point to correct target"
        echo "Expected: test_dir, Got: $(sudo readlink "$MOUNT_POINT/dir_link")"
        sudo dmesg | tail -200
        exit 1
    fi
    echo "SUCCESS: Directory symbolic link points to correct target"
else
    echo "WARNING: Directory symbolic links may not be supported"
fi

echo "Test 6: Testing symbolic link to non-existent file"
if ! sudo ln -s "non_existent_file" "$MOUNT_POINT/broken_link" 2> /tmp/broken_link_error.log; then
    echo "FAIL: Should be able to create symbolic link to non-existent file"
    cat /tmp/broken_link_error.log
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Symbolic link to non-existent file created"

if ! sudo test -L "$MOUNT_POINT/broken_link"; then
    echo "FAIL: Broken symbolic link does not exist"
    sudo dmesg | tail -200
    exit 1
fi

if sudo test -f "$MOUNT_POINT/broken_link"; then
    echo "FAIL: Broken symbolic link should not resolve to a file"
    sudo dmesg | tail -200
    exit 1
fi
echo "SUCCESS: Broken symbolic link behaves correctly"

echo "All tests passed for link and symlink functionality"
