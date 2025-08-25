#!/bin/bash
set -e

MOUNT_POINT=$1

echo "Testing lost+found directory functionality"

echo "Test 1: Checking if lost+found directory exists"
if sudo test -d "$MOUNT_POINT/lost+found"; then
    echo "PASS: lost+found directory exists"
else
    echo "FAIL: lost+found directory does not exist"
    exit 1
fi
