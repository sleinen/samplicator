#!/bin/bash

V=$(cat VERSION)

apt-get install build-essential autoconf checkinstall flex bison libncurses5-dev libreadline-dev git -y

git clean -dxf
autoconf
./configure
make
checkinstall --pkgversion=`cat VERSION` --pkgname=samplicate
