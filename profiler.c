/***
    profiler.c
    by Jaycee Zhang, 20181204
***/

#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#ifdef QEMU
#include "qemu/pcow-config.h"
#include "qemu/pcow-profiler.h"
#else
#include "config.h"
#include "profiler.h"
#endif /*QEMU*/


struct my_timer *uffd_tmr, *sig_tmr, *sig_newblk_tmr, *sig_meta_tmr, *uffd_newblk_tmr, *sig_map_tmr, *uffd_map_tmr, *sig_cow_tmr, *newblk_alloc_tmr, *newblk_meta_tmr, *lock_tmr;

unsigned long long fmap_td( struct timeval *t1, struct timeval *t2);

unsigned long long fmap_td( struct timeval *t1, struct timeval *t2)
{
    unsigned long long dt = t2->tv_sec * 1000000 + t2->tv_usec;
    return dt - t1->tv_sec * 1000000 - t1->tv_usec;
}

int fmap_st(struct my_timer *tmr)
{
    gettimeofday(&tmr->s, NULL);
    return 0;
}

int fmap_et(struct my_timer *tmr)
{
     gettimeofday(&tmr->e, NULL);
     tmr->t += fmap_td(&tmr->s, &tmr->e);
     tmr->count += 1;
     if (tmr == sig_tmr && tmr->count % REPORT_INTERVAL == 0) {
        print_timers(); 
        reset_timers();
     }
     if (tmr == uffd_tmr && tmr->count % REPORT_INTERVAL == 0) {
        print_timers(); 
        reset_timers();
     }
    return 0;
}

struct my_timer *init_timer(const char *name)
{
    struct my_timer *tmr = malloc(sizeof(struct my_timer));
    tmr->t = 0;
    tmr->count = 0;
    int name_len = strlen(name);
    tmr->name = malloc(sizeof(char) * (1 + name_len));
    strcpy(tmr->name, name);
    return tmr;
}
int init_timers(void)
{
    sig_tmr = init_timer("SIGSEGV time");
    sig_cow_tmr = init_timer("SIGSEGV: copy-on-write (CoW)");
    sig_map_tmr = init_timer("SIGSEGV: map time");
    sig_newblk_tmr = init_timer("SIGSEGV: new_block time (may take no time in fg)");
    sig_meta_tmr = init_timer("SIGSEGV: update meta time");

    uffd_tmr = init_timer("UFFD time");
    uffd_map_tmr = init_timer("UFFD: map time");
    uffd_newblk_tmr = init_timer("UFFD: new_block time");

    newblk_alloc_tmr = init_timer("fallocate time (in new_block)");
    newblk_meta_tmr = init_timer("update meta time (in new_block)");
    lock_tmr = init_timer("pthread mutex lock / unlock time (in new_block)");
    return 0;
}

int reset_timer(struct my_timer *tmr)
{
    tmr->t = 0;
    tmr->count = 0;
    return 0;
}
int reset_timers(void)
{
    reset_timer(uffd_tmr);
    reset_timer(sig_tmr);
    reset_timer(sig_map_tmr);
    reset_timer(uffd_map_tmr);
    reset_timer(sig_newblk_tmr);
    reset_timer(sig_meta_tmr);
    reset_timer(uffd_newblk_tmr);
    reset_timer(sig_cow_tmr);
    reset_timer(newblk_meta_tmr);
    reset_timer(newblk_alloc_tmr);
    reset_timer(lock_tmr);
    return 0;
}

int print_timer(struct my_timer *tmr)
{
    printf("%s:\n\t%llu us\tcount: %d\tavg:%llu us\n", tmr->name, tmr->t, tmr->count, tmr->count != 0 ? tmr->t / tmr->count : 0);
    return 0;
}
int print_timers(void)
{
    printf("=============  timers report  ===============\n");
    print_timer(uffd_tmr);
    print_timer(sig_tmr);
    print_timer(sig_map_tmr);
    print_timer(uffd_map_tmr);
    print_timer(sig_newblk_tmr);
    print_timer(sig_meta_tmr);
    print_timer(uffd_newblk_tmr);
    print_timer(sig_cow_tmr);
    print_timer(newblk_alloc_tmr);
    print_timer(newblk_meta_tmr);
    print_timer(lock_tmr);
    printf("=============================================\n");
    return 0;
}
