/***
    profiler.h
    by Jaycee Zhang 20181204
***/


extern struct my_timer *uffd_tmr, *sig_tmr, *sig_cow_tmr, *sig_meta_tmr, *sig_newblk_tmr, *uffd_newblk_tmr, *sig_map_tmr, *uffd_map_tmr, *newblk_alloc_tmr, *newblk_meta_tmr, *lock_tmr;

struct my_timer {
    struct timeval s;
    struct timeval e;
    int count;
    unsigned long long t;
    char *name;
};

int fmap_st(struct my_timer *tmr);
int fmap_et(struct my_timer *tmr);

struct my_timer *init_timer(const char *name);
int init_timers(void);

int reset_timer(struct my_timer *tmr);
int reset_timers(void);

int print_timer(struct my_timer *tmr);
int print_timers(void);
