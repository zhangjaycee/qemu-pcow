/*** 
    config.h
    20181219   by Jaycee zhang
***/

#ifndef __PCOW_CONFIG
#define __PCOW_CONFIG

//#define QEMU
//#define assert

#define PROFILE
#ifdef PROFILE
#define REPORT_INTERVAL 10000
#define PCOW_ST(x) { fmap_st (x); }
#define PCOW_ET(x) { fmap_et (x); }
#else
#define PCOW_ST(x)
#define PCOW_ET(x)
#endif /*PROFILE*/
#define PRE_ALLOC
#define MAX_PRE_NUM 5

#define FINEGRAINED_COW
#define MAX_COW_QUEUE_NUM 100
#define ACTIVE_MAX_COW_QUEUE_NUM 64
#define COW_UNIT 4096
#define RECENT_RANK_THRESHOLD 2
//#define NO_FULL_COW

#define BUCKET_NUM 128

// max thread number equals to the number of current list (cow_l) number
// and there will also be MAX_THREAD_NUM locks for COW
#define MAX_THREAD_NUM 128
// max concurrent is the max size of each current list (cow_l)
#define MAX_CONCURRENT 128

#endif /* __PCOW_CONFIG */
