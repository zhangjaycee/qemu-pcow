/***
    20181223 by Jaycee Zhang
***/

#ifdef QEMU
#include "qemu/pcow-config.h"
#else
#include "config.h"
#endif

//#define BUCKET_NUM 128
extern pthread_mutex_t cp_lock;

struct Cow_entry {
    unsigned long fault_voff_aligned; // the offset is key, aligned by s->block_size
    struct Meta *m;
    unsigned long *meta_entry;
    unsigned long block_off;
    void *map_addr;
    void *map_addr2;
    int copied_counter; // -1 if full copied
    struct Cow_entry *next;
};
struct Cow_bucket {
    struct Cow_entry *head;
};

/*
The hash table is to find the blocks without full COW,
the key is fault_voff_aligned, vaule is cow_entry, in which the block_off stored
*/
struct Cow_hashtable {
    struct Cow_bucket buckets[BUCKET_NUM];
    int size;
};

struct Cow_l_entry {
    unsigned long voff;
    int copy_size; // maybe we can derivate the size by voff alignment... 4k 8k .....s->block_size
    struct Cow_l_entry *next;
    struct Cow_entry *cow_entry;
};

/*
We can call the list current list, because the list is used to record the fault_voff_4k_aligned and its copy size that is under COW
or COWed recently. 
(The max size of the list can be the max thread number we support.)
*/
struct Cow_list {
    int size;
    struct Cow_l_entry *head;
    struct Cow_l_entry *tail;
};

int init_fgcow(void);
struct Cow_entry *insert_ht(unsigned long fault_voff_aligned);
struct Cow_entry *search_ht(unsigned long fault_voff_aligned, int *full_cow_flag);
int delete_ht(unsigned long fault_voff_aligned);
int final_cow(void);
struct Cow_l_entry *insert_list(unsigned long fault_voff_4k_aligned);
int del_list(unsigned long to_del_voff);
int in_list(unsigned long fault_voff_4k_aligned);
