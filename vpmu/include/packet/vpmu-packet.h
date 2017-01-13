#ifndef __VPMU_PACKET_H_
#define __VPMU_PACKET_H_
#include <signal.h>    // Signaling header
#include <semaphore.h> // Semaphore related header
#include <unistd.h>    // usleep

enum PACKET_PROCESSOR_TYPE { PROCESSOR_CPU, PROCESSOR_GPU, ALL_PROC };

// These are branch related
#define PREDICT_CORRECT 0
#define PREDICT_WRONG 1

// Each model defines its own packet type here
#define VPMU_PACKET_DATA      0x0000
#define VPMU_PACKET_CONTROL   0x8000
#define VPMU_PACKET_HOT       0x0800
// These are cache related
#define CACHE_PACKET_READ     0x0000
#define CACHE_PACKET_WRITE    0x0001
#define CACHE_PACKET_INSN     0x0002

// Values above 0xFF00 belongs to control packets
#define VPMU_PACKET_BARRIER   (0x00FF | VPMU_PACKET_CONTROL)
#define VPMU_PACKET_SYNC_DATA (0x00EF | VPMU_PACKET_CONTROL)
#define VPMU_PACKET_DUMP_INFO (0x00DF | VPMU_PACKET_CONTROL)
#define VPMU_PACKET_RESET     (0x00CF | VPMU_PACKET_CONTROL)

#define VPMU_PACKET_STATES_MASK 0x0F00

// Define likely
#ifndef likely
#if __GNUC__ < 3
#ifndef __builtin_expect
#define __builtin_expect(x, n) (x)
#endif
#endif

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x)   __builtin_expect(!!(x), 0)
#endif

#ifdef CONFIG_VPMU_USE_CPU_FENCE
// Prevent CPU reordering
#define VPMU_MEM_FENCE() asm volatile("mfence" ::: "memory")
#else
// Prevent compiler reordering
    #define VPMU_MEM_FENCE() asm volatile("" ::: "memory")
#endif

#define IS_VPMU_CONTROL_PACKET(_type) (_type & VPMU_PACKET_CONTROL)

#define VPMU_POST_JOB(sem_mutex_t, call_before_function) do {\
    call_before_function;\
    sem_post(sem_mutex_t);       /* up semaphore */\
}while(0)

#define VPMU_WAIT_JOB(sem_mutex_t, call_back_function) do {\
    sem_wait(sem_mutex_t);       /* down semaphore */\
    call_back_function;\
}while(0)

//This is a macro with callback for making a batch.
#define VPMU_PUSH_JOB(sem_mutex_t, call_before_function) do {\
    static unsigned char _local_counter = 1;\
    call_before_function;\
    if (_local_counter == 0) {    /* Signal per 256 requests */\
        sem_post(sem_mutex_t); /* up semaphore */\
    }\
    _local_counter++;\
}while(0)

//This is a macro with callback for making a batch.
#define VPMU_SHM_PUSH_JOB(_sems, num, call_before_function) do {\
    static unsigned char _local_counter = 1;\
    call_before_function;\
    if (_local_counter == 0) { /* Signal per 256 requests */\
        int _ii;\
        for (_ii = 0; _ii < num; _ii++) /* Big overhead for fast signaling */\
            sem_post(_sems[_ii]); /* up semaphore */\
    }\
    _local_counter++;\
}while(0)

#define VPMU_SHM_POST_JOB(_sems, num, call_before_function) do {\
    call_before_function;\
    int _ii;\
    for (_ii = 0; _ii < num; _ii++)\
        sem_post(_sems[_ii]);       /* up semaphore */\
}while(0)


#endif
