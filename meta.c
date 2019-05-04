/***
    Metadata block and Super block struct.
    Jaycee Zhang, 20181107     <jczhang@nbjl.nankai.edu.cn>
***/

#define _GNU_SOURCE
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <immintrin.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#ifdef QEMU
#include "qemu/pcow-config.h"
#include "qemu/pcow-meta.h"
#ifdef PROFILE
#include "qemu/pcow-profiler.h"
#endif /*PROFILE*/
#else
#include "config.h"
#include "meta.h"
#ifdef PROFILE
#include "profiler.h"
#endif /*PROFILE*/
#endif /*QEMU*/

#define MAGIC "NBJL"
struct New_block *nbs[MAX_PRE_NUM];
unsigned long nb_count;
unsigned long nb_available;
pthread_mutex_t nb_lock;
pthread_mutex_t nb_lock2;


int pm_flush(void *addr)
{
    _mm_clflush(addr);
    return 0;
}

int pm_fence(void)
{
    _mm_sfence();
    return 0;
}

struct Super *read_first_super(int fd, unsigned long read_off, int create_flag)
{
    struct stat st;
    struct Super *s = malloc(sizeof(struct Super));

    fstat(fd, &st);

    // we first check if the first block is super block
    s->pm_super = mmap(NULL, CL_SIZE, PROT_WRITE|PROT_READ, MAP_SHARED, fd, read_off);
    if (st.st_size < CL_SIZE || strncmp(s->pm_super->magic, MAGIC, 4)) {
        free(s);
        munmap(s->pm_super, CL_SIZE);
        if (create_flag) {
            printf("Formatting... \n");
            int block_size = DEFAULT_BLOCK_SIZE * 1024;
            int max_block_num = DEFAULT_MAX_FILE_SIZE * 1024 * 1024 / DEFAULT_BLOCK_SIZE;
            ftruncate(fd, 0);
            return new_super(fd, block_size, max_block_num);
        } else {
            // Invalid image format
            return NULL;
        }
    }

    s->fd = fd;
    s->block_size = s->pm_super->block_size;
    s->max_per_seg = (s->block_size - CL_SIZE) / ENTRY_SIZE;
    s->max_block_num = s->pm_super->max_block_num;
    s->my_off = read_off;
    s->active_meta_num = s->pm_super->active_meta_num;
    s->active_meta_off = s->my_off + (unsigned long)s->block_size * (1L + (unsigned long)s->active_meta_num * (1L + (unsigned long)s->max_per_seg));
    s->active_meta = read_meta(s, -1);

    return s;
}

struct Super *read_next_super(struct Super *s)
{
    struct stat st;
    struct Super *next_s = malloc(sizeof(struct Super));

    fstat(s->fd, &st);
    unsigned long read_off = s->active_meta_off + (unsigned long)s->block_size * \
                                                                    (1L + s->active_meta->pm_top->counter);

    if (read_off >= st.st_size) { // s is the last super, we return NULL
        return NULL;
    }
    
    next_s->pm_super = mmap(NULL, CL_SIZE, PROT_WRITE|PROT_READ, MAP_SHARED, s->fd, read_off);
    assert(next_s->pm_super != MAP_FAILED);

    if (strncmp(next_s->pm_super->magic, MAGIC, 4)) {
        return NULL;
    }

    next_s->fd = s->fd;
    next_s->block_size = s->pm_super->block_size;
    next_s->max_per_seg = (s->block_size - CL_SIZE) / ENTRY_SIZE;
    next_s->max_block_num = s->pm_super->max_block_num;
    next_s->my_off = read_off;
    next_s->active_meta_num = next_s->pm_super->active_meta_num;
    next_s->active_meta_off = next_s->my_off + (unsigned long)next_s->block_size * (1L + (unsigned long)next_s->active_meta_num \
                                                            * (1L + (unsigned long)next_s->max_per_seg));

    next_s->active_meta = read_meta(next_s, -1);

    return next_s;
}

struct Super *read_last_super(int fd)
{
    struct Super *s = read_first_super(fd, 0, 0);
    if (NULL == s) {
        return NULL;
    }

    struct Super *tmp_s;
    while ((tmp_s = read_next_super(s))) {
        s = tmp_s;
    }
    assert(NULL == tmp_s);

    // s->addr will be inited by the caller ...
    return s;
}

struct Super *new_super(int fd, int block_size, int max_block_num)
{
    struct Super *s = malloc(sizeof(struct Super));

    /* use the file size as offset and allocate two block for the new super/snapshot 
       and its first meta block */
    struct stat st;
    fstat(fd, &st);
    struct Super *s0 = read_last_super(fd);
    unsigned long alloc_off;
    if (NULL != s0) {
        alloc_off = s0->active_meta_off + (unsigned long)s0->block_size * \
                                                            (1L + s0->active_meta->pm_top->counter);
    } else {
        alloc_off = 0;
    }
    printf("new super: alloc_off: %lu\t st.st_size: %lu\n", alloc_off, st.st_size);
    free(s0);
    int ret = fallocate(fd, 0, alloc_off, block_size * 2);
    assert(0 == ret);

    s->pm_super = mmap(NULL, CL_SIZE, PROT_WRITE|PROT_READ, MAP_SHARED, fd, alloc_off);
    s->pm_super->block_size = block_size;
    s->pm_super->max_block_num = max_block_num;
    s->pm_super->active_meta_num = 0;
    s->pm_super->magic[0] = 'N';
    s->pm_super->magic[1] = 'B';
    s->pm_super->magic[2] = 'J';
    s->pm_super->magic[3] = 'L';
    pm_flush(s->pm_super);
    pm_fence();
    munmap(s->pm_super, CL_SIZE);

    struct Meta *m0 = malloc(sizeof(struct Meta));
    m0->pm_top = mmap(NULL, CL_SIZE, PROT_WRITE|PROT_READ, MAP_SHARED, fd, alloc_off + block_size);
    m0->pm_btm = (void *)((unsigned long)m0->pm_top + CL_SIZE);
    m0->counter = 0;
    m0->pm_top->counter = 0;
#ifdef FINEGRAINED_COW
    m0->pm_top->fg_counter = 0;
#endif
    pm_flush(m0->pm_top);
    pm_fence();
    munmap(m0->pm_top, CL_SIZE);
    munmap(m0->pm_btm, block_size - CL_SIZE);
    free(m0);
    free(s);

    return read_first_super(fd, alloc_off, 0);
}

struct Meta *read_meta(struct Super *s, int n)
{
    struct Meta *meta = malloc(sizeof(struct Meta));

    unsigned long meta_off;

    if (n == -1) { // read the active meta
        meta_off = s->active_meta_off;

    } else {
        meta_off = s->my_off + (unsigned long)s->block_size + (unsigned long)n * (s->max_per_seg + 1) * s->block_size;
    }
    meta->pm_top = mmap(NULL, s->block_size, PROT_WRITE|PROT_READ, 
                                            MAP_SHARED, s->fd, meta_off);
    meta->pm_btm = (void *)((unsigned long)meta->pm_top + CL_SIZE);
    if(meta->pm_top->counter > s->max_per_seg) {
        printf("error: counter larger than max_per_seg...  s->block_size = %d\t"
                "meta_off = %lu\tn=%d\t top->counter= %d\tmax_per_seg=%d\n",
                            s->block_size, meta_off, n, meta->pm_top->counter, s->max_per_seg);
        exit(-1);
    } 
    assert(meta->pm_btm);
    meta->counter = meta->pm_top->counter;

    return meta;
}




#ifdef PRE_ALLOC
void *new_block_allocator(void *arg)
{
    struct newblk_th_arg *th_arg = arg; 
    struct Super *s = th_arg->s;

    int alloced_num;
    int prealloc_n;
    unsigned long cur_off;

    while (1) {
        cur_off = (unsigned long)s->active_meta_off + (unsigned long)s->block_size + \
                                                (unsigned long)s->active_meta->counter * s->block_size;
        prealloc_n = (s->alloc_off - cur_off) / s->block_size;
        if (prealloc_n + 1 > MAX_PRE_NUM) {
            usleep(10);
            continue; 
        }
        alloced_num = s->active_meta_num * s->max_per_seg + s->active_meta->counter;
        if (alloced_num + prealloc_n + 1 > s->max_block_num) {
            printf("error: out of range, allocator thread exiting......\n");
            pthread_exit(NULL);
        }

        // allocate data block
        struct New_block *nb = malloc(sizeof(struct New_block));
        int ret = fallocate(s->fd, 0, s->alloc_off, s->block_size);
        nb->block_off = s->alloc_off;
        nb->map_addr = mmap(NULL, s->block_size, PROT_WRITE|PROT_READ, MAP_SHARED, s->fd, s->alloc_off);
        nb->map_addr2 = mmap(NULL, s->block_size, PROT_WRITE|PROT_READ, MAP_SHARED, s->fd, s->alloc_off);
        nbs[nb_count % MAX_PRE_NUM] = nb;
        pthread_mutex_lock(&nb_lock);
        nb_count++;
        nb_available++;
        pthread_mutex_unlock(&nb_lock);
         
        s->alloc_off += s->block_size;
        if (ret != 0) {
            printf("data fallocate failed! errno: %s s->alloc_off: %lu, s->block_size: %d \n", 
                                    strerror(errno), s->alloc_off, s->block_size);
            exit(-1);
        }
    }
}

struct Meta *new_meta(struct Super *s)
{
    PCOW_ST(newblk_alloc_tmr);
    struct New_block *my_nb;
    while (1) {
        pthread_mutex_lock(&nb_lock);
        if (nb_available > 0) {
            my_nb = nbs[(nb_count - nb_available) % MAX_PRE_NUM];
            nb_available--;
            pthread_mutex_unlock(&nb_lock);
            break;
        }
        pthread_mutex_unlock(&nb_lock);
    }
    unsigned long cur_off = my_nb->block_off;
    PCOW_ET(newblk_alloc_tmr);
    PCOW_ST(newblk_meta_tmr);
    struct Meta *meta = malloc(sizeof(struct Meta));

    meta->pm_top = mmap(NULL, s->block_size, PROT_WRITE|PROT_READ, 
                                            MAP_SHARED, s->fd, cur_off);
    meta->pm_btm = (void *)((unsigned long)meta->pm_top + CL_SIZE);
    meta->pm_top->counter = 0;
#ifdef FINEGRAINED_COW
    meta->pm_top->fg_counter = 0;
#endif
	// may be we need not flush and fence here, the counter will be increased to 1 soon
    meta->counter = 0;
    PCOW_ET(newblk_meta_tmr);

    return meta;
}

struct New_block *new_block(struct Super *s, unsigned long fault_off)
{
    
    pthread_mutex_lock(&nb_lock2);

    int to_alloc_num = s->active_meta_num * s->max_per_seg + s->active_meta->counter + 1;
    if (to_alloc_num > s->max_block_num) {
        printf("error: out of range\n");
        exit(-1);
    }

    struct New_block *my_nb;
    if (s->active_meta->counter < s->max_per_seg) {
        // 1. allocate new block
        PCOW_ST(newblk_alloc_tmr);
        while (1) {
            pthread_mutex_lock(&nb_lock);
            if (nb_available > 0) {
                my_nb = nbs[(nb_count - nb_available) % MAX_PRE_NUM];
                nb_available--;
                pthread_mutex_unlock(&nb_lock);
                break;
            }
            pthread_mutex_unlock(&nb_lock);
        }
        PCOW_ET(newblk_alloc_tmr);
        // 2. append meta block bottom entry
        PCOW_ST(newblk_meta_tmr);
        s->active_meta->pm_btm[s->active_meta->counter] = (unsigned int)(fault_off / s->block_size);
#ifdef FINEGRAINED_COW
        s->active_meta->pm_btm[s->active_meta->counter] &= ADDR_MASK;
        pm_flush(&s->active_meta->pm_btm[s->active_meta->counter]);
        pm_fence();
#endif /*FINEGRAINED_COW*/

        // 3. modify meta block top counter
        s->active_meta->counter += 1;
        s->active_meta->pm_top->counter += 1;
        pm_flush(s->active_meta->pm_top);
        pm_fence();
    } else {
        // else, we should allocate a new meta block first
        // 1. allocate new meta block
        struct Meta *new_m = new_meta(s);
        // 2. allocate new block
        PCOW_ST(newblk_alloc_tmr);
        while (1) {
            pthread_mutex_lock(&nb_lock);
            if (nb_available > 0) {
                my_nb = nbs[(nb_count - nb_available) % MAX_PRE_NUM];
                nb_available--;
                pthread_mutex_unlock(&nb_lock);
                break;
            }
            pthread_mutex_unlock(&nb_lock);
        }
        PCOW_ET(newblk_alloc_tmr);
        // 3. append meta block bottom entry
        PCOW_ST(newblk_meta_tmr);
        new_m->pm_btm[new_m->counter] = (unsigned int)(fault_off / s->block_size);
#ifdef FINEGRAINED_COW
        new_m->pm_btm[new_m->counter] &= ADDR_MASK;
#endif /*FINEGRAINED_COW*/
        pm_flush(&new_m->pm_btm[new_m->counter]);
        pm_fence();

        // 4. modify meta block top counter
        new_m->counter += 1;
        new_m->pm_top->counter += 1;
        pm_flush(new_m->pm_top);
        pm_fence();

        // 5. update super block
        free(s->active_meta);
        s->active_meta = new_m;
        s->active_meta_num += 1;
        s->active_meta_off = s->my_off + (unsigned long)s->block_size + (unsigned long)s->active_meta_num * \
                                                        ((unsigned long)s->max_per_seg + 1L) * s->block_size;
        s->pm_super->active_meta_num = s->active_meta_num;
        pm_flush(s->pm_super);
        pm_fence();
        PCOW_ET(newblk_meta_tmr);
    }

    pthread_mutex_unlock(&nb_lock2);
    return my_nb;
}

#else /*PRE_ALLOC is not defined*/

struct Meta *new_meta(struct Super *s)
{
    unsigned long alloc_off = (unsigned long)s->active_meta_off + (unsigned long)s->block_size + \
                                                    (unsigned long)s->active_meta->counter * s->block_size;
    PCOW_ST(newblk_alloc_tmr);
    int ret = fallocate(s->fd, 0, alloc_off, s->block_size);
    assert(ret == 0);
    PCOW_ET(newblk_alloc_tmr);
    PCOW_ST(newblk_meta_tmr);
    struct Meta *meta = malloc(sizeof(struct Meta));

    meta->pm_top = mmap(NULL, s->block_size, PROT_WRITE|PROT_READ, 
                                            MAP_SHARED, s->fd, alloc_off);
    meta->pm_btm = (void *)((unsigned long)meta->pm_top + CL_SIZE);
    meta->pm_top->counter = 0;
#ifdef FINEGRAINED_COW
    meta->pm_top->fg_counter = 0;
#endif
	// may be we need not flush and fence here, the counter will be increased to 1 soon
    meta->counter = 0;
    PCOW_ET(newblk_meta_tmr);
    return meta;
}

struct New_block *new_block(struct Super *s, unsigned long fault_off)
{
    struct New_block *my_nb = malloc(sizeof(struct New_block));
    int fd = s->fd;

    unsigned long block_size = (unsigned long)s->block_size;
    unsigned long alloc_off;
    int to_alloc_num = s->active_meta_num * s->max_per_seg + s->active_meta->counter + 1;
    if (to_alloc_num > s->max_block_num) {
        printf("error: out of range\n");
        exit(-1);
    }

    if (s->active_meta->counter < s->max_per_seg) {
        // if the allocating block entry is within the range of current active meta block, 
        // we directly allocate the data block and update the corresponding entry in the active meta block

        // 1. allocate data block, and map it to the fault addr
        PCOW_ST(newblk_alloc_tmr);
        alloc_off = (unsigned long)s->active_meta_off + (unsigned long)block_size + \
                                                        (unsigned long)s->active_meta->counter * block_size;
        int ret = fallocate(fd, 0, alloc_off, block_size);
        PCOW_ET(newblk_alloc_tmr);
        if (ret != 0) {
            printf("data fallocate failed! errno: %s alloc_off: %lu, block_size: %d \n", 
                                    strerror(errno), alloc_off, block_size);
            exit(-1);
        }


        PCOW_ST(newblk_meta_tmr);
        // 2. append meta block bottom entry
        s->active_meta->pm_btm[s->active_meta->counter] = (unsigned int)(fault_off / s->block_size);
#ifdef FINEGRAINED_COW
        s->active_meta->pm_btm[s->active_meta->counter] &= ADDR_MASK;
#endif /*FINEGRAINED_COW*/
        pm_flush(&s->active_meta->pm_btm[s->active_meta->counter]);
        pm_fence();

        // 3. modify meta block top counter
        s->active_meta->counter += 1;
        s->active_meta->pm_top->counter += 1;
        pm_flush(s->active_meta->pm_top);
        pm_fence();
        PCOW_ET(newblk_meta_tmr);

    } else { // else, we should allocate a new meta block first
        assert(s->active_meta->counter == s->max_per_seg);
        // 1. allocate new meta block
        struct Meta *new_m = new_meta(s);

        // 2. allocate data block, and map it to the fault addr
        // alloc_off should cross over the newly allocated meta block
        PCOW_ST(newblk_alloc_tmr);
        alloc_off = (unsigned long)s->active_meta_off + (unsigned long)block_size + \
                                        (unsigned long)s->active_meta->counter * block_size + block_size;
        int ret = fallocate(fd, 0, alloc_off, block_size);
        PCOW_ET(newblk_alloc_tmr);
        if (ret != 0) {
            printf("meta fallocate failed! errno: %s alloc_off: %lu, block_size: %d \n", 
                                    strerror(errno), alloc_off, block_size);
            exit(-1);
        }

        // 3. append meta block bottom entry
        PCOW_ST(newblk_meta_tmr);
        new_m->pm_btm[new_m->counter] = (unsigned int)(fault_off / s->block_size);
#ifdef FINEGRAINED_COW
        new_m->pm_btm[new_m->counter] &= ADDR_MASK;
#endif /*FINEGRAINED_COW*/
        pm_flush(&new_m->pm_btm[new_m->counter]);
        pm_fence();

        // 4. modify meta block top counter
        new_m->counter += 1;
        new_m->pm_top->counter += 1;
        pm_flush(new_m->pm_top);
        pm_fence();

        // 5. update super block
        free(s->active_meta);
        s->active_meta = new_m;
        s->active_meta_num += 1;
        s->active_meta_off = s->my_off + (unsigned long)s->block_size + (unsigned long)s->active_meta_num * \
                                                        ((unsigned long)s->max_per_seg + 1L) * s->block_size;
        s->pm_super->active_meta_num = s->active_meta_num;
        pm_flush(s->pm_super);
        pm_fence();
        PCOW_ET(newblk_meta_tmr);
    }
    my_nb->block_off = alloc_off;
    my_nb->map_addr = mmap(NULL, s->block_size, PROT_WRITE|PROT_READ, MAP_SHARED, fd, alloc_off);
    my_nb->map_addr2 = mmap(NULL, s->block_size, PROT_WRITE|PROT_READ, MAP_SHARED, fd, alloc_off);
    return my_nb;
}
#endif /*PRE_ALLOC*/

int print_struct(int fd)
{
    int counter = 0;
    struct Super *s = read_first_super(fd, 0, 0);
    unsigned long i, j;

    do {
        printf("===== Super Block No. %d =====\n", counter);
        counter++;
        for (i = 0; i <= s->active_meta_num; i++) {
            printf("\tsegment count: [%lu]\n", i);
            struct Meta *m = read_meta(s, i);
            unsigned long data_off = (unsigned long)s->block_size * (1L + (1L + s->max_per_seg) * i);
            for (j = 0; j < m->counter; j++) {
                if (j % 1000 == 0) {
                    printf("\t[%lu] <---- [%lu]\n", 
                                (unsigned long)(m->pm_btm[j] & ADDR_MASK) * s->block_size, data_off + s->block_size *j);
                    printf("\t ... \n");
                }
            }
            free(m);
            printf("\n");
        }
        printf("image file magic string: %c%c%c%c\n", 
                        s->pm_super->magic[0], s->pm_super->magic[1], s->pm_super->magic[2], s->pm_super->magic[3]);
        printf("Super: \n\tblock_size = %d\n\tmax_block_num = %d\n\tactive_meta_num = %d\n",
                                                    s->block_size, s->max_block_num, s->active_meta_num);
        printf("\tmax_per_seg = %d\n", s->max_per_seg);
        // active meta:
        printf("Meta : \n\tcounter = %d\n", s->active_meta->counter);
    } while ((s = read_next_super(s)));

    return 0;
}
