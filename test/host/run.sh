#!/bin/bash
# Host unit tests for the platform-independent logic modules.
set -e
cd "$(dirname "$0")"
ROOT=../..
CJSON=$ROOT/../third_party/cJSON-master
INC="-I$ROOT/components/common/include -I$ROOT/components/netease/include -I$CJSON -I."
SRC="$ROOT/components/common/src/*.cpp $ROOT/components/netease/netease_parse.cpp $CJSON/cJSON.c"
CXX=${CXX:-g++}
FLAGS="-std=c++17 -Wall -Wextra -Wno-unused-parameter -g -fsanitize=address,undefined"
# The workspace lives on a FUSE object-store mount where ld's random writes
# fail ("file truncated"); build and run from local disk instead.
BUILD=${LAZER_TEST_BUILD:-/tmp/lazer-host-build}
mkdir -p "$BUILD"
status=0
for t in test_*.cpp; do
    name="${t%.cpp}"
    echo "== $name =="
    if ! $CXX $FLAGS $INC "$t" $SRC -o "$BUILD/$name" 2> "$BUILD/$name.compile.log"; then
        echo "COMPILE/LINK FAILED (expected RED before implementation):"
        tail -5 "$BUILD/$name.compile.log"
        status=1
        continue
    fi
    "$BUILD/$name" || status=1
done
exit $status
