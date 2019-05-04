/***
    20181223 by Jaycee Zhang
***/
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#ifdef QEMU
#include "qemu/pcow-config.h"
#else 
#include "config.h"
#endif

#ifdef FINEGRAINED_COW
#ifdef QEMU
        #ifdef PROFILE
        #include "qemu/pcow-profiler.h"
        #endif /*PROFILE*/
    #include "qemu/pcow-cow.h"
    #include "qemu/pcow-meta.h"
#else
        #ifdef PROFILE
        #include "profiler.h"
        #endif /*PROFILE*/
    #include "cow.h"
    #include "meta.h"
#endif /*QEMU*/

extern struct Super *cur_s;


// the hashtable is for uncompleted cow block
// the list is for current cow entry

struct Cow_hashtable cow_ht;
struct Cow_list cow_l[MAX_THREAD_NUM];
pthread_mutex_t ht_lock[BUCKET_NUM];
pthread_mutex_t l_lock[MAX_THREAD_NUM];
pthread_mutex_t cp_lock;

int init_fgcow(void)
{
    pthread_mutex_init(&cp_lock, NULL);
    // init the COW hash table
    int i;
    for (i = 0; i < BUCKET_NUM; i++) {
        cow_ht.buckets[i].head = malloc(sizeof(struct Cow_entry));
        cow_ht.buckets[i].head->next = NULL;
        pthread_mutex_init(&ht_lock[i], NULL);
    }
    cow_ht.size = 0;

    // init full COW list
    for (i = 0; i < MAX_THREAD_NUM; i++) {
        cow_l[i].head = malloc(sizeof(struct Cow_l_entry));
        cow_l[i].head->next = NULL;
        cow_l[i].size = 0;
        pthread_mutex_init(&l_lock[i], NULL);
    }
    return 0;
}

static int print_list(void)
{
    int i;
    for (i = 0; i < MAX_THREAD_NUM; i++) {
        printf(" --- list %d:\n", i);
        struct Cow_l_entry *p = cow_l[i].head->next;
        while (p != NULL) {
            printf("p: voff: %lu, copy_size: %d\n", p->voff, p->copy_size);
            p = p->next;
        }
    }
    return 0;
}

int in_list(unsigned long fault_voff_4k_aligned)
{
    int list_num = fault_voff_4k_aligned / COW_UNIT % MAX_THREAD_NUM;

    unsigned long to_copy_voff = fault_voff_4k_aligned;
    int copy_size = COW_UNIT;

    pthread_mutex_lock(&l_lock[list_num]);
    struct Cow_l_entry *p = cow_l[list_num].head;
    while (p->next != NULL) {
        if (to_copy_voff >= p->next->voff && (to_copy_voff + copy_size) <= (p->next->voff + p->next->copy_size)) { 
            pthread_mutex_unlock(&l_lock[list_num]);
            return 1; // we return NULL if this range is in cow_l and copying now
        }
        p = p->next;
    }
    pthread_mutex_unlock(&l_lock[list_num]);
    return 0;
}

struct Cow_l_entry *insert_list(unsigned long fault_voff_4k_aligned) 
{
    int list_num = fault_voff_4k_aligned / COW_UNIT % MAX_THREAD_NUM;

    // here we first assume the cow granularity is 4KB (COW_UNIT)
    unsigned long to_copy_voff = fault_voff_4k_aligned;
    int copy_size = COW_UNIT;

    pthread_mutex_lock(&l_lock[list_num]);
    struct Cow_l_entry *p = cow_l[list_num].head;
#ifdef NO_FULL_COW
    int can_not_full_cow_flag = 0;
#endif

    // double check current list now...
    while (p->next != NULL) {
        if (to_copy_voff >= p->next->voff && (to_copy_voff + copy_size) <= (p->next->voff + p->next->copy_size)) { 
            pthread_mutex_unlock(&l_lock[list_num]);
            return NULL; // we return NULL if this range is already copied...
        }
        p = p->next;
    }
#ifdef NO_FULL_COW
    assert(can_not_full_cow_flag == 0);
#endif

    // insert the new cow_l_entry to current list (cow_l)
    p->next = malloc(sizeof(struct Cow_l_entry));
    p = p->next;
    p->voff = to_copy_voff;
    p->next = NULL;
    cow_l[list_num].size++;
    // the max size of the current list is the MAX_CONCURRENT supported
    if (cow_l[list_num].size >= MAX_CONCURRENT) {
        struct Cow_l_entry *to_free = cow_l[list_num].head;
        cow_l[list_num].head = cow_l[list_num].head->next;
        free(to_free);
        cow_l[list_num].size--;
    }

    int full_cow_flag = 0;
    struct Cow_entry *cow_entry;
    int bucket_num = to_copy_voff / cur_s->block_size % BUCKET_NUM;
    pthread_mutex_lock(&ht_lock[bucket_num]);
    cow_entry = search_ht(to_copy_voff / cur_s->block_size * cur_s->block_size, &full_cow_flag);
    if (NULL == cow_entry) {
        cow_entry = insert_ht(to_copy_voff / cur_s->block_size * cur_s->block_size);
    }    
    pthread_mutex_unlock(&ht_lock[bucket_num]);
    p->cow_entry = cow_entry;
#ifdef NO_FULL_COW
    full_cow_flag = 0;
#endif

    if (full_cow_flag == 1) {
        p->copy_size = cur_s->block_size;
        p->voff = to_copy_voff / cur_s->block_size * cur_s->block_size;;
    } else {
        p->copy_size = COW_UNIT; 
        p->voff = to_copy_voff;
    }
    //print_list();
    pthread_mutex_unlock(&l_lock[list_num]);
    return p;
}

int del_list(unsigned long to_del_voff)
{
    int list_num = to_del_voff / COW_UNIT % MAX_THREAD_NUM;
    
    pthread_mutex_lock(&l_lock[list_num]);
    struct Cow_l_entry *p = cow_l[list_num].head;
    while (NULL != p->next) {
        if (p->next->voff == to_del_voff) {
            struct Cow_l_entry *to_free = p->next;
            p->next = p->next->next;
            pthread_mutex_unlock(&l_lock[list_num]);
            free(to_free);
            return 0;
        }
        p = p->next;
    }
    pthread_mutex_unlock(&l_lock[list_num]);
    return 0;
}

struct Cow_entry *insert_ht(unsigned long fault_voff_aligned)
{
    // create a cow_entry struct
    struct Cow_entry *cow_entry = malloc(sizeof(struct Cow_entry));
    cow_entry->fault_voff_aligned = fault_voff_aligned;
    struct New_block *my_nb = new_block(cur_s, fault_voff_aligned);
    cow_entry->block_off = my_nb->block_off;
    cow_entry->map_addr = my_nb->map_addr;
    cow_entry->map_addr2 = my_nb->map_addr2;
    int meta_n = (cur_s->active_meta_off - cur_s->my_off - cur_s->block_size) / (cur_s->max_per_seg + 1) * cur_s->block_size ;
    int entry_n = cur_s->active_meta->counter - 1;
    cow_entry->m = read_meta(cur_s, meta_n);
    cow_entry->m->pm_top->fg_counter++;
    cow_entry->meta_entry = cow_entry->m->pm_btm + entry_n;
    cow_entry->copied_counter = 0;

    // insert to the hash table
    int bucket_number = fault_voff_aligned / cur_s->block_size % BUCKET_NUM;
    cow_entry->next = cow_ht.buckets[bucket_number].head->next;
    cow_ht.buckets[bucket_number].head->next = cow_entry;
    cow_ht.size++;
    return cow_entry;
}

struct Cow_entry *search_ht(unsigned long fault_voff_aligned, int *full_cow_flag)
{
    int bucket_number = fault_voff_aligned / cur_s->block_size % BUCKET_NUM;
    struct Cow_entry *p = cow_ht.buckets[bucket_number].head;
    int rank = 0;
    while (NULL != p->next) {
        rank++;
        if (p->next->fault_voff_aligned == fault_voff_aligned) {
            struct Cow_entry *to_ret = p->next;
            if (rank < RECENT_RANK_THRESHOLD) { 
                to_ret->copied_counter = -1;
                *full_cow_flag = 1;
                // we delete the to_ret entry from the hash table here,
                // and we will free it in the SIGSEGV handler after COW
                p->next = to_ret->next;
            }
            return to_ret;
        }
        p = p->next;
    }
    return NULL;
}

int delete_ht(unsigned long fault_voff_aligned)
{
    int bucket_number = fault_voff_aligned / cur_s->block_size % BUCKET_NUM;
    struct Cow_entry *p = cow_ht.buckets[bucket_number].head;
    struct Cow_entry *to_del;
    
    while (NULL != p->next) {
        if (p->next->fault_voff_aligned == fault_voff_aligned) {
            to_del = p->next;
            to_del->m->pm_top->fg_counter--;
            pm_flush(to_del->m->pm_top);
            pm_fence();
            p->next = p->next->next;
        }
        p = p->next;
    }
    cow_ht.size--;
    free(to_del);
    return 0;
}

int final_cow(void)
{
    int i;
    for (i = 0; i < BUCKET_NUM; i++) {
        struct Cow_entry *p = cow_ht.buckets[i].head->next;
        while (NULL != p) {
            void *tmp_addr = mmap(NULL, cur_s->block_size, PROT_WRITE|PROT_READ, MAP_SHARED, cur_s->fd, p->block_off);
            assert(MAP_FAILED != tmp_addr);
            memcpy(tmp_addr, (void *)(cur_s->addr + p->fault_voff_aligned), cur_s->block_size);
            *p->meta_entry |= ADDR_MASK;
            pm_flush(p->meta_entry);
            pm_fence();
            p = p->next;
        }
    }
    return 0;
}
#endif /*FINEGRAINED_COW*/
