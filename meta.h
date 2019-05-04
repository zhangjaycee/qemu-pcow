/***
    Metadata block and Super block struct.
    Jaycee Zhang, 20181107     <jczhang@nbjl.nankai.edu.cn>
***/

#include <pthread.h>
#ifdef QEMU
#include "qemu/pcow-config.h"
#else
#include "config.h"
#endif

#ifndef __META_H
#define __META_H


#define DEFAULT_BLOCK_SIZE 64L // default block size in kb
#define DEFAULT_MAX_FILE_SIZE 30L // in GB
#define CL_SIZE 64  // cache line length
#define MAX_UFFD_NUM (10)

#ifdef FINEGRAINED_COW
#define ENTRY_SIZE 8    // sizeof(unsigned long)
#else
#define ENTRY_SIZE 4    // sizeof(unsigned int)
#endif /*FINEGRAINED_COW*/
#define ADDR_MASK 0x00000000ffffffff

/*
+---------+---------+----------+---------+---------+---------+----------+---------+---------+
|  Super  |  Meta0  |   data0  |  data1  |  data2  |  Meta1  |   data3  |  data4  |  data5  |   ...........
+---------+---------+----------+---------+---------+---------+----------+---------+---------+
(super meta)
*/

/* Super
+---------------+
|block_size(int)|  
+---------------+ 4 Bytes
| max block num | 
+---------------+ 8 
|active_meta_num|
+---------------+ 12
|     "NBJL"    |
+---------------+ 16
|base_image_name|
|               |
+---------------+ 64
|               |
|               |
|    padding    |
|               |
+---------------+ BLOCK_SIZE Bytes
*/


/*  Meta
+---------------+
|  counter(int) |   
+---------------+ 4 Bytes
| fg-cow flag   |  
+---------------+ 8 
|    padding    |                    top
+-------+-------+ 64               ----------
|cow-BM | addr  |  entry            bottom 
+-------+-------+ 72
|cow-BM | addr  |  entry    
+-------+-------+ 80
|cow-BM | addr  |  entry 
+-------+-------+ 88
|               | .
+---------------+ .
|               | .
+---------------+ BLOCK_SIZE Bytes
*/

struct Super_layout {
    int block_size;
    int max_block_num;
    int active_meta_num;
    char magic[4];
    char base_image_name[48];
    //padding: block_size - 64 Bytes
};

struct Super {
    /* pointers to PM file */
    struct Super_layout *pm_super;    // pointer to super block on PM file

    /* pointer to current active struct meta (in DRAM) */
    struct Meta *active_meta;

    /* virtual addr that will be return to pcow caller */
    void *addr;

    /* the opened file fd */
    int fd;

    /* in memory cache of the Super block information  */
    int block_size;         // also on PM file
    int max_block_num;      // also on PM file, max number of data blocks
    int active_meta_num;    // also on PM file, number of active meta block (start from 0)

    int max_per_seg;            // max number of data entry in a meta block, 
                                // can be derived from block_size: 
                                //      (block_size - meta_top_size ) / entry_size

    unsigned long my_off;                 // offset of the this super/snapshot block, 
    unsigned long active_meta_off;        // offset of the current active meta on PM file, 
                                // can be derived from active_meta_num:
                                //      block_size * [1 + active_meta_num * (1 + max_per_seg)]

    unsigned long fmmap_length;           // the fmmap length this time

#ifdef PRE_ALLOC
    // there will be prealloc_n pre-allocated blocks
    unsigned long alloc_off;
#endif /*PRE_ALLOC*/
};



struct Meta_layout_top {
    int counter;
#ifdef FINEGRAINED_COW
    int fg_counter;
#endif
    //char padding[60];
};

// Meta_layout_bottom is int

struct Meta {
    /* pointers to PM file */
    struct Meta_layout_top *pm_top;     // pointer to Meta block top part on PM file
#ifdef FINEGRAINED_COW
    unsigned long *pm_btm;                        // pointer to Meta block bottom part on PM file
#else
    unsigned int *pm_btm;                        // pointer to Meta block bottom part on PM file
#endif /*FINEGRAINED_COW*/

    /* in memory cache of the Super block information  */
    int counter;                        // also in PM file (top)
};

/* read the super header of a file, construct and return a struct Super */
//struct Super *read_super(int fd, int create_flag);

/* read the first super block start from read_off */
struct Super *read_first_super(int fd, unsigned long read_off, int create_flag);

/* read the next super block */
struct Super *read_next_super(struct Super *s);

/* read the last super block */
struct Super *read_last_super(int fd);

/* format a new fmap file, construct and return a struct Super */
struct Super *new_super(int fd, int block_size, int max_block_num);

/* read the n-th meta block, return a struct Meta*/
struct Meta *read_meta(struct Super *s, int n);

/* fallocate a new meta block on PM file, construct a struct Meta*/
struct Meta *new_meta(struct Super *s);

struct New_block {
    unsigned long block_off;
    void *map_addr;
    void *map_addr2;
};
extern struct New_block *nbs[MAX_PRE_NUM];
extern unsigned long nb_count;
extern unsigned long nb_available;
extern pthread_mutex_t nb_lock;
extern pthread_mutex_t nb_lock2;

struct New_block *new_block(struct Super *s, unsigned long fault_addr);

int print_struct(int fd);

int pm_flush(void *addr);
int pm_fence(void);

#ifdef PRE_ALLOC
struct newblk_th_arg {
    struct Super *s;
};

void *new_block_allocator(void *arg);
#endif /*PRE_ALLOC*/

#endif /* __META_H */
