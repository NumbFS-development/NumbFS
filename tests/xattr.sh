#!/bin/bash
#
# Test for xattr functionality in numbfs
#

set -e

MOUNT_POINT=$1
NUMBFS_ROOT=$2
IMAGE_NAME=$3

echo "Testing xattr functionality"

sudo umount $MOUNT_POINT 2>/dev/null || true

mkfs.numbfs $NUMBFS_ROOT/$IMAGE_NAME
sudo mount -t numbfs -o loop $NUMBFS_ROOT/$IMAGE_NAME $MOUNT_POINT

# 1. Test basic xattr set and get operations
echo "Testing basic xattr set/get operations"
sudo touch $MOUNT_POINT/testfile
sudo setfattr -n user.test -v "test_value" $MOUNT_POINT/testfile
sudo getfattr -n user.test $MOUNT_POINT/testfile | grep -q "test_value" && echo "PASS: Basic xattr test" || echo "FAIL: Basic xattr test"

# 2. Test trusted namespace xattr operations (requires sudo)
echo "Testing trusted namespace xattr operations"
sudo setfattr -n trusted.secure -v "secure_data" $MOUNT_POINT/testfile
sudo getfattr -n trusted.secure $MOUNT_POINT/testfile | grep -q "secure_data" && echo "PASS: Trusted xattr test" || echo "FAIL: Trusted xattr test"

# 3. Test that non-root user cannot access trusted xattrs
echo "Testing non-root access to trusted xattrs"
if getfattr -n trusted.secure $MOUNT_POINT/testfile 2>/dev/null; then
    echo "FAIL: Non-root user should not be able to read trusted xattrs"
else
    echo "PASS: Non-root user correctly denied access to trusted xattrs"
fi

# 4. Test boundary conditions: name length 16, value length 32
echo "Testing boundary conditions for xattr name and value length"
long_name=$(printf '%*s' 16 | tr ' ' 'x')
long_value=$(printf '%*s' 32 | tr ' ' 'y')
sudo setfattr -n user.$long_name -v "$long_value" $MOUNT_POINT/testfile
sudo getfattr -n user.$long_name $MOUNT_POINT/testfile | grep -q "$long_value" && echo "PASS: Max length xattr test" || echo "FAIL: Max length xattr test"

sudo rm -f $MOUNT_POINT/testfile
sudo touch $MOUNT_POINT/testfile

# 5. Test boundary condition: maximum of 9 xattr entries
echo "Testing maximum number of xattr entries"
for i in {1..9}; do
    sudo setfattr -n user.test$i -v "value$i" $MOUNT_POINT/testfile
done
sudo getfattr -d $MOUNT_POINT/testfile | grep -c "user.test" | grep -q "9" && echo "PASS: Max xattr count test" || echo "FAIL: Max xattr count test"

# 5.1 Test adding more than 9 xattrs (should fail)
if sudo setfattr -n user.test10 -v "value10" $MOUNT_POINT/testfile 2>/dev/null; then
    echo "FAIL: Should have rejected more than 9 xattrs"
else
    echo "PASS: Correctly rejected more than 9 xattrs"
fi

sudo rm -f $MOUNT_POINT/testfile
sudo touch $MOUNT_POINT/testfile

# 6. Test expected failures (by design)
echo "Testing expected failure cases"

# 6.1 Test xattr name longer than 16 characters (should fail)
too_long_name=$(printf '%*s' 17 | tr ' ' 'x')
if sudo setfattr -n user.$too_long_name -v "test" $MOUNT_POINT/testfile 2>/dev/null; then
    echo "FAIL: Should have rejected xattr name longer than 16 chars"
else
    echo "PASS: Correctly rejected xattr name longer than 16 chars"
fi

# 6.2 Test xattr value longer than 32 characters (should fail)
too_long_value=$(printf '%*s' 33 | tr ' ' 'y')
if sudo setfattr -n user.longval -v "$too_long_value" $MOUNT_POINT/testfile 2>/dev/null; then
    echo "FAIL: Should have rejected xattr value longer than 32 chars"
else
    echo "PASS: Correctly rejected xattr value longer than 32 chars"
fi

# 6.3 Test non-root user setting trusted xattrs (should fail)
if setfattr -n trusted.user_test -v "test_value" $MOUNT_POINT/testfile 2>/dev/null; then
    echo "FAIL: Non-root user should not be able to set trusted xattrs"
else
    echo "PASS: Non-root user correctly denied setting trusted xattrs"
fi

echo "All xattr tests completed"
