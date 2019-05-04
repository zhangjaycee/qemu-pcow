#!/bin/bash

echo "~/bin/qemu-pcow/bin/qemu-system-x86_64 -smp 16 -machine pc,nvdimm -m 4096M,slots=3,maxmem=400000M -enable-kvm \
-drive file=~/centos7.qcow2,if=virtio,cache=none,format=qcow2 \
-object memory-backend-ram,id=ram,size=2048M \
-numa node,memdev=ram,cpus=0-7,nodeid=0 \
-object memory-backend-ram,id=ram2,size=2048M \
-numa node,memdev=ram2,cpus=8-15,nodeid=1 \
-object memory-backend-file,id=pm,mem-path=/pmem0/mypcow.img,format=pcow,share=on,discard-data=off,merge=off \
-device nvdimm,id=pm,memdev=pm \
-net nic -net user,hostfwd=tcp::2222-:22 -nographic "
