# qemu-pcow

QEMU-pcow adds persistent memory image file management feature to QEMU. To support storage virtualization features, we enhanced QEMU's memory management module, and designed a PM image file format called pcow.

The corresponding research paper of this prototype (**Towards Virtual Machine Image Management for Persistent Memory**) is already accepted by MSST 2019 (http://storageconference.us):


> Abstract: Persistent memory's (PM) byte-addressability and high capacity will also make it emerging for virtualized environment. Modern virtual machine monitors virtualize PM using either I/O virtualization or memory virtualization. However, I/O virtualization will sacrifice PM's byte-addressability, and memory virtualization does not get the chance of PM image management. In this paper, we enhance QEMU's memory virtualization mechanism. The enhanced system can achieve both PM's byte-addressability inside virtual machines and PM image management outside the virtual machines. We also design pcow, a virtual machine image format for PM, which is compatible with our enhanced memory virtualization and supports storage virtualization features including thin-provision, base image and snapshot. Address translation is performed with the help of Extended Page Table (EPT), thus much faster than image formats implemented in I/O virtualization. We also optimize pcow considering PM's characteristics. The evaluation demonstrates that our scheme boosts the overall performance by up to 50x compared with qcow2, an image format implemented in I/O virtualization, and brings almost no performance overhead compared with the native memory virtualization.

## Note
We only tested this prototype on QEMU version 3.0.0 and Linux kernel verion 4.16.0 (both Guest and Host). This prototype is only for feature verificaion and performance testing. Please do not use important data for testing, we are not responsible for any data loss caused by this prototype.

## Build

#### 1. Platform

Make sure you use the similar platform as we do:

* QEMU 3.0.0:
```
wget https://download.qemu.org/qemu-3.0.0.tar.xz
tar -xf qemu-3.0.0.tar.xz
```

* Linux 4.16.0
```bash
wget https://github.com/torvalds/linux/archive/v4.16.tar.gz
# ...
```

#### 2. Install to QEMU

Run the `patch_to_qemu.sh` script to patch the diff into QEMU, the script will also build QEMU and install the bins to the specific path.
```
cd qemu-pcow
./patch_to_qemu.sh QEMU_SRC_PATH QEMU_BIN_PATH
```

#### 3. Build `pcow-img`

`pcow-img` is the management tool for the pcow image file, it can create pcow images, take pcow snapshots, roll back to a snapshot, or change the base image of a pcow image file.

```
cd qemu-pcow
make
```

After building, the `pcow-img` tool will reside in `qemu-pcow/build`, you can run `build/pcow-img help` to check its features:
```
$ build/pcow-img help
Help:
   Optaions:
       create   create a new image.
       snapshot create a snapshot on an existing image.
       rollback rollback the image to a specific snapshot.
       rebase   change the base image of an existing image.
       info     check the information of an existing image.
```

#### 4. Increase the system's maximum map count
 
On our platform, the default `vm.max_map_count` is 65535, this is not enough for pcow:

```
sudo sysctl -w vm.max_map_count=65535000
``` 



## Run

#### 1. Mount a DAX FS
Please make sure the filesystem is dax enabled, for example, you should mount a ext4 `/dev/pmemX` partition to a directory with `-o dax`:

```bash
mount -o dax /dev/pmem0 /pmem
```


#### 2. Create a pcow image file on a `dax` filesystem

For example, we can create a 128 GB pcow image consists of 64 KB units (block size):

```
cd /pmem
QEMU_PCOW_SRC/build/pcow-img create 64 128 my_first_pcow.img
```

#### 3. Run QEMU

We add a `format=XXX` command line option for object `memory-backend-file`, and if you specify `format=pcow`, you should not add the `size` option as needed before. Here we provide a example QEMU startup command line:

```bash 
~/qemu-pcow/bin/qemu-system-x86_64 -smp 16 -machine pc,nvdimm -m 4096M,slots=3,maxmem=400000M -enable-kvm \
-drive file=~/my_guestos_image.qcow2,if=virtio,cache=none,format=qcow2 \
-object memory-backend-ram,id=ram,size=2048M \
-numa node,memdev=ram,cpus=0-7,nodeid=0 \
-object memory-backend-ram,id=ram2,size=2048M \
-numa node,memdev=ram2,cpus=8-15,nodeid=1 \
-object memory-backend-file,id=pm,mem-path=/pmem/my_first_pcow.img,format=pcow,share=on,discard-data=off,merge=off \
-device nvdimm,id=pm,memdev=pm \
-net nic -net user,hostfwd=tcp::2222-:22 -nographic
```

Now, in the guest VM, you should get a 128 GB `/dev/pmem0` device, you can use it for the backend for a dax filesystem or PMDK just like using this kind of device in a host server.

```bash
# (in guest:
[root@localhost ~]# lsblk
NAME        MAJ:MIN RM  SIZE RO TYPE MOUNTPOINT
pmem0       259:0    0  128G  0 disk
sr0          11:0    1 1024M  0 rom
fd0           2:0    1    4K  0 disk
vda         252:0    0   30G  0 disk
├─vda2      252:2    0    9G  0 part
│ ├─cl-swap 253:1    0    1G  0 lvm  [SWAP]
│ └─cl-root 253:0    0   28G  0 lvm  /
├─vda3      252:3    0   20G  0 part
│ └─cl-root 253:0    0   28G  0 lvm  /
└─vda1      252:1    0    1G  0 part /boot

# (in host:
zjc@/pmem0$ ls -lsh my_first_pcow.img
1.7M -rwxr-xr-x. 1 zjc zjc 1.7M 5月   4 19:25 my_first_pcow.img
```
