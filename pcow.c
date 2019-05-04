#define _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <linux/falloc.h>
#include <linux/userfaultfd.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <time.h>
#include <sys/wait.h>
#include <setjmp.h>
#include <stdbool.h>
#include <linux/falloc.h>
#include <signal.h>

#ifdef QEMU
#include "qemu/pcow-config.h"
#include "qemu/pcow-meta.h"
#else
#include "config.h"
#include "meta.h"
#endif

#ifdef PROFILE
#ifdef QEMU
#include "qemu/pcow-profiler.h"
#else
#include "profiler.h"
#endif /*QEMU*/
#endif /*PROFILE*/

#ifdef FINEGRAINED_COW
#ifdef QEMU
#include "qemu/pcow-cow.h"
#else
#include "cow.h"
#endif /*QEMU*/
pthread_mutex_t cow_lock[MAX_THREAD_NUM];
#else /*FINEGRAINED_COW*/
pthread_mutex_t cow_lock0;
#endif /*FINEGRAINED_COW*/

#ifdef PRE_ALLOC
pthread_t alloc_thr;  
#endif /*PRE_ALLOC*/

struct Super *cur_s;
long cur_uffd;
pthread_t thr;  
size_t global_length;

#ifdef UFFD_FS // TODO: now we have not add the UFFD_FS feature in kernel
#define UFFD_FEATURE UFFD_FEATURE_FS
const long REG_LEN=512UL*1024*1024*1024;
#else
#define UFFD_FEATURE 0
const long REG_LEN=512UL*1024*1024*1024;
#endif

#define UFFD_FEATURE_FS (1<<9)

#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
                           } while (0)
#define handle_error(msg) \
           do { perror(msg); exit(EXIT_FAILURE); } while (0)


int print_size(int fd);
int do_readonly_fmmap(struct Super *s, void *addr);
int do_writable_fmmap(struct Super *s, int prot, int flags);
int do_base_fmmap(struct Super *s, void *addr);

int print_size(int fd) 
{
    struct stat st;
    fstat(fd, &st);
    printf("fstat:\t file size: %d KB (%d KB allocate).\n\n", (int)(st.st_size / 1024), (int)(st.st_blocks * 512 / 1024));
    return 0;
}


/* CoW handler */
static void cow_handler(int sig, siginfo_t *si, void *unused)
{
    PCOW_ST(sig_tmr);

    if ((unsigned long)si->si_addr < (unsigned long)cur_s->addr || 
    (unsigned long)si->si_addr - (unsigned long)cur_s->addr > global_length - (unsigned long)cur_s->block_size) {
        PCOW_ET(sig_tmr);
        return;
    }
    // NOTE: here voffs are the virtualized offset
    unsigned long fault_voff = (unsigned long)si->si_addr - (unsigned long)cur_s->addr;
    unsigned long fault_voff_aligned = fault_voff / cur_s->block_size * cur_s->block_size;
    unsigned long fault_addr_aligned = fault_voff_aligned + (unsigned long)cur_s->addr;

    int lock_num = fault_voff / cur_s->block_size % MAX_THREAD_NUM;
    
#ifdef FINEGRAINED_COW
    unsigned long fault_voff_4k_aligned = fault_voff / COW_UNIT * COW_UNIT;
    unsigned long fault_addr_4k_aligned = fault_voff_4k_aligned + (unsigned long)cur_s->addr;
    
    struct Cow_l_entry *cow_l_entry;

    // if already in current list, that indicates the 4k unit is copied or under copying, we return derictly
    PCOW_ST(sig_newblk_tmr);
    if (in_list(fault_voff_4k_aligned)) {
        usleep(2);
        PCOW_ET(sig_newblk_tmr);
        PCOW_ET(sig_tmr);
        return; 
    }

    pthread_mutex_lock(&cow_lock[lock_num]);

    cow_l_entry = insert_list(fault_voff_4k_aligned);
    PCOW_ET(sig_newblk_tmr);
    if (NULL == cow_l_entry) { // we check again
        pthread_mutex_unlock(&cow_lock[lock_num]);
        PCOW_ET(sig_tmr);
        return; 
    }

    if (cow_l_entry->copy_size != cur_s->block_size) {
        void *tmp_addr = (void *)((unsigned long)cow_l_entry->cow_entry->map_addr + cow_l_entry->voff - fault_voff_aligned);
        PCOW_ST(sig_cow_tmr);
        memcpy(tmp_addr, (void *)(cur_s->addr + cow_l_entry->voff), cow_l_entry->copy_size); 
        int i;
        for (i = 0; i < cow_l_entry->copy_size / 8; i += 8) {
            pm_flush(tmp_addr + i);
        }
        pm_fence();
        PCOW_ET(sig_cow_tmr);
        PCOW_ST(sig_map_tmr);
        assert((unsigned long)cur_s->addr + cow_l_entry->voff == fault_addr_4k_aligned);
        void *ret_addr = mremap(tmp_addr, cow_l_entry->copy_size, cow_l_entry->copy_size, MREMAP_FIXED|MREMAP_MAYMOVE, (unsigned long)cur_s->addr + cow_l_entry->voff);
        PCOW_ET(sig_map_tmr);

        PCOW_ST(sig_meta_tmr);
        int cow_num = (cow_l_entry->voff - fault_voff_aligned) / COW_UNIT;
        int cow_cnt = cow_l_entry->copy_size / COW_UNIT;
        cow_l_entry->cow_entry->copied_counter += cow_num;
        for (i = 0; i < cow_cnt; i++) {
            *(cow_l_entry->cow_entry->meta_entry) |= (1ULL << (cow_num + i)) << 32;
        }
        pm_flush(cow_l_entry->cow_entry->meta_entry);
        pm_fence();
        PCOW_ET(sig_meta_tmr);

    } else { // cow_l_entry->copy_size == block_size, we capy the whole block!
        PCOW_ST(sig_map_tmr);
        void *tmp_addr = (void *)((unsigned long)cow_l_entry->cow_entry->map_addr2);
        PCOW_ET(sig_map_tmr);
        PCOW_ST(sig_cow_tmr);
        memcpy(tmp_addr, (void *)fault_addr_aligned, cur_s->block_size);
        int i;
        for (i = 0; i < cur_s->block_size / 8; i += 8) {
            pm_flush(tmp_addr + i);
        }
        pm_fence();

        PCOW_ET(sig_cow_tmr);
        PCOW_ST(sig_map_tmr);
        void *ret_addr = mremap(tmp_addr, cur_s->block_size, cur_s->block_size, MREMAP_FIXED|MREMAP_MAYMOVE, fault_addr_aligned);
        PCOW_ET(sig_map_tmr);
        assert((unsigned long)ret_addr == fault_addr_aligned);

        PCOW_ST(sig_meta_tmr);
        *(cow_l_entry->cow_entry->meta_entry) &= ADDR_MASK;
        pm_flush(cow_l_entry->cow_entry->meta_entry);
        pm_fence();
        PCOW_ET(sig_meta_tmr);
        // full copied entry is no use any more, it is alread deleted from the hash table, we free it here
        free(cow_l_entry->cow_entry);
    }

    pthread_mutex_unlock(&cow_lock[lock_num]);
#else
    // we copy the data from the write protected area to newly allocated file block
    pthread_mutex_lock(&cow_lock0);
    PCOW_ST(sig_newblk_tmr);
    struct New_block *my_nb = new_block(cur_s, fault_voff_aligned);
    PCOW_ET(sig_newblk_tmr);

    void *tmp_addr = my_nb->map_addr;
    PCOW_ST(sig_cow_tmr);
    memcpy(tmp_addr, (void *)fault_addr_aligned, cur_s->block_size);
    PCOW_ET(sig_cow_tmr);

    PCOW_ST(sig_map_tmr);
    void *ret_addr = mremap(tmp_addr, cur_s->block_size, cur_s->block_size, MREMAP_FIXED|MREMAP_MAYMOVE, fault_addr_aligned);
    PCOW_ET(sig_map_tmr);
    assert((unsigned long)ret_addr == fault_addr_aligned);
    pthread_mutex_unlock(&cow_lock0);
#endif /*FINEGRAINED_COW*/
    PCOW_ET(sig_tmr);
}


struct th_arg {
    long uffd;
    struct Super *s;                // writable layer
};
static void *uf_monitor(void *arg)
{
    printf("in uffd handler...\n");
    struct th_arg *th_arg = arg;
    struct Super *s = th_arg->s;
    long uffd = th_arg->uffd;
    int fd = th_arg->s->fd;
    void *addr = s->addr;
    void *ret_addr;
    //int page_size = sysconf(_SC_PAGE_SIZE);
    ssize_t nread;
    static struct uffd_msg msg;   /* Data read from userfaultfd */
    static int fault_cnt = 0;     /* Number of faults so far handled */

    for (;;) {
       struct pollfd pollfd;
       int nready;
       pollfd.fd = uffd;
       pollfd.events = POLLIN;
       nready = poll(&pollfd, 1, -1);
       PCOW_ST(uffd_tmr);
#ifdef MONITOR_DBG
       if (nready == -1)
           errExit("poll");
        printf("[Moniter] fault_handler_thread():\n");
        printf("[Moniter] \tpoll() returns: nready = %d; "
               "POLLIN = %d; POLLERR = %d\n", nready,
               (pollfd.revents & POLLIN) != 0,
               (pollfd.revents & POLLERR) != 0);
#endif /* MONITOR_DBG */

        /* Read an event from the userfaultfd */
        nread = read(uffd, &msg, sizeof(msg));
#ifdef MONITOR_DBG
        if (nread == 0) {
           printf("EOF on userfaultfd!\n");
           exit(EXIT_FAILURE);
        }
        if (nread == -1)
           errExit("read");
        /* We expect only one kind of event; verify that assumption */
        if (msg.event != UFFD_EVENT_PAGEFAULT) {
           fprintf(stderr, "Unexpected event on userfaultfd\n");
           exit(EXIT_FAILURE);
        }
#endif /* MONITOR_DBG */

        fault_cnt++;

#ifdef MONITOR_DBG
        printf("[Moniter] \tUFFD_EVENT_PAGEFAULT event: ");
        printf("counter = %d; ", fault_cnt);
        printf("flags = %llx; ", msg.arg.pagefault.flags);
        printf("address = %llx;", msg.arg.pagefault.address);
        printf("off = %lu (%lu KB)\n UFFD_PAGEFAULT_FLAG_WRITE: %d\n", msg.arg.pagefault.address - (unsigned long)addr,
                                        (msg.arg.pagefault.address - (unsigned long)addr)/1024, UFFD_PAGEFAULT_FLAG_WRITE );
#endif /* MONITOR_DBG */

        assert((unsigned long)addr % 4096 == 0);
        assert((unsigned long)msg.arg.pagefault.address % 4096 == 0);
        unsigned long fault_off = ((unsigned long)msg.arg.pagefault.address - (unsigned long)addr) / s->block_size * s->block_size;
        assert((unsigned long)fault_off % s->block_size == 0);
        unsigned long fault_addr = fault_off + (unsigned long)addr;
        assert((unsigned long)fault_addr % 4096 == 0);

        struct uffdio_range uffdio_range;
        uffdio_range.start = fault_addr;
        uffdio_range.len = s->block_size;
        if (fault_off <= s->fmmap_length - s->block_size) {
           // If we don't have backing files, allocate a new block and map it.
           PCOW_ST(uffd_newblk_tmr);
           struct New_block *my_nb = new_block(s, fault_off);
           PCOW_ET(uffd_newblk_tmr);
           PCOW_ST(uffd_map_tmr);
           // we can not use the pre mmaped addr here
           ret_addr = mmap((void *)fault_addr, s->block_size, PROT_WRITE|PROT_READ, MAP_SHARED|MAP_FIXED, fd, my_nb->block_off);
           PCOW_ET(uffd_map_tmr);
           if ((unsigned long)ret_addr != fault_addr) {
                printf("ret_addr: %lu, fault_addr: %lu\n", (unsigned long)ret_addr, (unsigned long)fault_addr);
           }
           assert((unsigned long)ret_addr == fault_addr);
        } else {
#ifdef MONITOR_DBG
            printf("[Moniter] out of s->fmmap_length file size!!!\n");
            printf("[Moniter] SIGBUS is expected\n");
#endif /* MONITOR_DBG */
        }
        if (ioctl(uffd, UFFDIO_WAKE, &uffdio_range) == -1)
           errExit("ioctl-UFFDIO_WAKE");
       PCOW_ET(uffd_tmr);
    }
}

// map a read-only snapshot onto addr.
int do_readonly_fmmap(struct Super *s, void *addr)
{
    printf("[readonly---]s->active_meta_num = %d active_meta->counter = %d\n", s->active_meta_num, s->active_meta->pm_top->counter);
    unsigned long i, j;
    for (i = 0; i <= s->active_meta_num; i++) {
        struct Meta *m = read_meta(s, i);
        unsigned long data_off = s->my_off + (unsigned long)s->block_size * (2L + (1L + (unsigned long)s->max_per_seg) * i); 
        for (j = 0; j < m->counter; j++) {
            mmap((void *)((unsigned long)addr + (unsigned long)(m->pm_btm[j] & ADDR_MASK) * s->block_size), 
                s->block_size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, s->fd, data_off + (unsigned long)s->block_size *j);
            mprotect((void *)((unsigned long)addr + (unsigned long)(m->pm_btm[j] & ADDR_MASK) * s->block_size), s->block_size, PROT_READ);
        }
        free(m);
    }
    return 0;
}

// recusively map read-only base image...
int do_base_fmmap(struct Super *s, void *addr)
{
    // TODO: Do we need to ensure all base image have the same block_size?
    // map the base image's data block onto addr
    printf("base_image_name: %s, [%d]\n", s->pm_super->base_image_name, (int)strlen(s->pm_super->base_image_name));
    if (0 != strlen(s->pm_super->base_image_name)) {
        printf("base_image_name is not NULL: %s, [%d]\n", s->pm_super->base_image_name, (int)strlen(s->pm_super->base_image_name));
        struct Super *base_s = read_first_super(s->fd, 0, 0);
        if (base_s != NULL) {
            do_base_fmmap(base_s, addr);
        }
        free(base_s);
    }

    // map the snapshots onto the addr
    struct Super *tmp_s = s;
    do {
        do_readonly_fmmap(s, addr);
        free(s);
        s = tmp_s;
    } while ((tmp_s = read_next_super(s)));
    do_readonly_fmmap(s, addr);
    return 0;
}

// writable layer mapping...
// FIXME: the prot and flag is not clearly considered, we now just or the MAP_FIXED flag,
//        and now it seems support with "PROT_READ|PROT_WRITE" fmmap well...
int do_writable_fmmap(struct Super *s, int prot, int flags)
{
    printf("[wriable +++]s->active_meta_num = %d active_meta->counter = %d\n", s->active_meta_num, s->active_meta->pm_top->counter);
    unsigned long i, j;
    for (i = 0; i <= s->active_meta_num; i++) {
        struct Meta *m = read_meta(s, i);
        unsigned long data_off = s->my_off + (unsigned long)s->block_size * (2L + (1L + (unsigned long)s->max_per_seg) * i); 
        for (j = 0; j < m->counter; j++) {
            mmap((void *)((unsigned long)s->addr + (unsigned long)(m->pm_btm[j] & ADDR_MASK) * s->block_size), 
                            s->block_size, prot, flags|MAP_FIXED, s->fd, data_off + (unsigned long)s->block_size *j);
        }
        free(m);
    }
    return 0;
}

/*
Example:

on file:           
    +---------+---------+-----+-----+-----+---------+-----+-----+-----+---------+-----+-----+
    |  Super  |  Meta0  |  0  |  1  |  2  |  Meta1  |  2  |  3  |  4  |  Meta2  |  5  |  6  |   ......
    +---------+---------+-----+-----+-----+---------+-----+-----+-----+---------+-----+-----+

VMAs:
    corresponding block:  2                   0           6   4   3       1        7   5
    non-linear mapping: +---+               +---+       +---+---+---+   +---+    +---+---+   
    udderlying VMA:     +--------------------------------------------------------------------+

Snapshots:

    +----+---+---+---+---+---+---+---+---+---+----+---+---+---+---+---+---+---+----+---+---+---+
    | S  | M | 0 | 1 | 2 | 3 | M | 0 | 1 | 2 | S  | M | 0 | 1 | 2 | 3 | M | 0 | S  | M | 0 | 1 | ....
    +----+---+---+---+---+---+---+---+---+---+----+---+---+---+---+---+---+---+----+---+---+---+
                                                ^                               ^    ^ 
                                            snapshot1                    snapshot2  active_meta_num
*/
void *fmmap(void *expected_addr, size_t length, int prot, int flags, int fd, unsigned long offset)
{
    global_length = length;
    long uffd;
    struct stat st;
    fstat(fd, &st);
    printf("REG_LEN: %ld\n", REG_LEN);
    printf("fstat:\t file size: %d KB (%d KB allocate).\n\n", (int)st.st_size / 1024, (int)st.st_blocks * 512 / 1024);


    struct Super *s =  read_first_super(fd, 0, 0);
    if (NULL == s) {
        printf("File is not fomatted!\n");
        return MAP_FAILED;
    }

    if (length == 0) {
        length = (size_t)s->block_size * s->max_block_num;
    }


    if (length > REG_LEN) {
        printf("[pcow error] the input mmap length is too big! max length support: %ld\n", REG_LEN);
        return MAP_FAILED;
    }

    if (offset > length) {
        printf("[pcow error] offset should not larger than max length!\n");
        return MAP_FAILED;
    }

    // 1. apply the underlying "fake" VMA and register it as a userfault area:
    void *addr;
#ifdef UFFD_FS // have bug
    addr = mmap(expected_addr, length, prot, flags, fd, offset);
#else
    addr = mmap(expected_addr, length, prot, MAP_SHARED|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
#endif /* UFFD_FS */
    assert(addr);

    //unsigned long len;
    struct uffdio_api uf_api;
    struct uffdio_register uf_reg;
    int ret;
    uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
    cur_uffd = uffd;
    assert(-1 != uffd);
    uf_api.api = UFFD_API;
    uf_api.features = UFFD_FEATURE;
    ret = ioctl(uffd, UFFDIO_API, &uf_api);
    assert(-1 != ret);
    uf_reg.range.start = (unsigned long)addr;
    //uf_reg.range.len = REG_LEN;
    uf_reg.range.len = length;
    uf_reg.mode = UFFDIO_REGISTER_MODE_MISSING;
    ret = ioctl(uffd, UFFDIO_REGISTER, &uf_reg);
    assert(-1 != ret);


    // 2.1. Map all the data blocks of the base images.  (read-only layer mapping)
    printf("in fmmap: base_image_name: %s, [%d]\n", s->pm_super->base_image_name, (int)strlen(s->pm_super->base_image_name));
    if (0 != strlen(s->pm_super->base_image_name)) { // have base image
        int base_fd = open(s->pm_super->base_image_name, O_RDWR | O_CREAT | O_DIRECT, 0755);
        struct Super *base_s = read_first_super(base_fd, 0, 0);
        if (base_s != NULL) {
            printf("------- we have base image ---------\n");
            do_base_fmmap(base_s, addr);
        }
        free(base_s);
    }

    // 2.2. Map the writable image's read-only snapshots.  (read-only layer mmaping)
    struct Super *tmp_s;
    while ((tmp_s = read_next_super(s))) {
        printf("--------snapshot ...\n");
        do_readonly_fmmap(s, addr);
        free(s);
        s = tmp_s;
    }

    // now the 's' is the writable super block
    s->fmmap_length = length;
    s->addr = addr;
    // we init the preallocation lock and the counter...
    cur_s = s;
    printf("============ cur_s : my_off: %lu\n", cur_s->my_off);

    // 2.3. Map the active snapshot onto writable (also uffd) layer. (writable layer mmaping)
    ret = do_writable_fmmap(s, prot, flags);
    assert(0 == ret);

    // 3. register the SIGSEGV signal handler
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = cow_handler;
    if (sigaction(SIGSEGV, &sa, NULL) == -1)
        handle_error("sigaction");

    // 4. create and start a uffd monitor thread
    struct th_arg *th_arg = (struct th_arg *)malloc(sizeof(struct th_arg));
    th_arg->uffd = uffd;
    th_arg->s = s;

    ret = pthread_create(&thr, NULL, uf_monitor, th_arg);
    assert(0 == ret);

#ifdef PROFILE
    init_timers();
#endif /*PROFILE*/

#ifdef PRE_ALLOC
    // 5. create the pre-allocator thread
    nb_count = 0;
    nb_available = 0;
    pthread_mutex_init(&nb_lock, NULL);
    pthread_mutex_init(&nb_lock2, NULL);
    s->alloc_off = (unsigned long)s->active_meta_off + (unsigned long)s->block_size + \
                                                    (unsigned long)s->active_meta->counter * s->block_size;

    struct newblk_th_arg *newblk_th_arg = (struct newblk_th_arg *)malloc(sizeof(struct newblk_th_arg));
    newblk_th_arg->s = s;

    ret = pthread_create(&alloc_thr, NULL, new_block_allocator, newblk_th_arg);
    assert(0 == ret);
#endif /*PRE_ALLOC*/

#ifdef FINEGRAINED_COW
    int i;
    for (i = 0; i < MAX_THREAD_NUM; i++) {
        pthread_mutex_init(&cow_lock[i], NULL); 
    }
    init_fgcow();
#else
    pthread_mutex_init(&cow_lock0, NULL);
#endif /*FINEGRAINED_COW*/

    return (void *)((unsigned long)s->addr + offset);
}

int fmunmap(void *addr)
{
    if (addr != cur_s->addr)
        return -1;
#ifdef FINEGRAINED_COW
    final_cow();
#endif /*FINEGRAINED_COW*/
    struct uffdio_register uf_reg;
    uf_reg.range.start = (unsigned long) cur_s->addr;
    size_t length = (size_t)cur_s->block_size * cur_s->max_block_num;
    uf_reg.range.len = length;
    ioctl(cur_uffd, UFFDIO_UNREGISTER, &uf_reg.range);
    munmap(addr, length);
    pthread_cancel(thr);
#ifdef PRE_ALLOC
    pthread_cancel(alloc_thr);
#endif /*PRE_ALLOC*/
    free(cur_s);
    return 0;
}
