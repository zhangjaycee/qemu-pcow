#!/bin/bash


if [ $# != 2 ] ; then 
    echo "USAGE:" 
    echo " $0 QEMU_SRC_PATH QEMU_BIN_PATH" 
exit 1; 
fi 

#QEMU_SRC=~/srcs/pcow/qemu-3.0.0
#QEMU_BIN=~/bin/qemu-pcow

QEMU_SRC=$1
QEMU_BIN=$2

cp -r qemu-diff/* ${QEMU_SRC}

cp pcow.h ${QEMU_SRC}/include/qemu/pcow.h
cp cow.h ${QEMU_SRC}/include/qemu/pcow-cow.h
cp manager.h ${QEMU_SRC}/include/qemu/pcow-manager.h
cp meta.h ${QEMU_SRC}/include/qemu/pcow-meta.h
cp profiler.h ${QEMU_SRC}/include/qemu/pcow-profiler.h
cp config.h ${QEMU_SRC}/include/qemu/pcow-config.h 

cp pcow.c ${QEMU_SRC}/util/pcow.c
cp cow.c ${QEMU_SRC}/util/pcow-cow.c
cp manager.c ${QEMU_SRC}/util/pcow-manager.c
cp meta.c ${QEMU_SRC}/util/pcow-meta.c
cp profiler.c ${QEMU_SRC}/util/pcow-profiler.c

cd ${QEMU_SRC}
./configure --prefix=${QEMU_BIN} --target-list=x86_64-softmmu --disable-werror --enable-debug
make -j && make install
