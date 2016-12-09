#ifndef _shm_ringbuffer_h
#define _shm_ringbuffer_h

#define shm_ringBuffer_typedef(T, NAME, num)                                             \
    typedef struct {                                                                     \
        uint64_t          size;                                                          \
        volatile uint64_t start[num];                                                    \
        volatile uint64_t end;                                                           \
        T *               elems;                                                         \
    } NAME

#define shm_bufferInit(BUF, S, T, B_T, shm_addr)                                         \
    BUF       = (B_T *)(shm_addr);                                                       \
    BUF->size = S;                                                                       \
    memset((void *)BUF->start, 0, sizeof(BUF->start));                                   \
    BUF->end   = 0;                                                                      \
    BUF->elems = (T *)((uint8_t *)shm_addr + offsetof(B_T, elems) + sizeof(T *))

#define shm_nextStartIndex(BUF, _id) ((BUF->start[_id] + 1) % BUF->size)
#define shm_nextEndIndex(BUF) ((BUF->end + 1) % BUF->size)
#define shm_isBufferEmpty(BUF, _id) (BUF->end == BUF->start[_id])
#define shm_isBufferFull(BUF, _id) (shm_nextEndIndex(BUF) == BUF->start[_id])
#define shm_isBufferNotEmpty(BUF, _id) (BUF->end != BUF->start[_id])
#define shm_remainedBufferSpace(BUF, _id)                                                \
    ((BUF->end >= BUF->start[_id]) ? (BUF->size - (BUF->end - BUF->start[_id]) - 1)      \
                                   : ((BUF->start[_id] - BUF->end) - 1))

#define shm_bufferWrite(BUF, ELEM)                                                       \
    BUF->elems[BUF->end] = ELEM;                                                         \
    VPMU_MEM_FENCE();                                                                    \
    BUF->end = shm_nextEndIndex(BUF)

#define shm_bufferRead(BUF, _id, ELEM)                                                   \
    ELEM = BUF->elems[BUF->start[_id]];                                                  \
    VPMU_MEM_FENCE();                                                                    \
    BUF->start[_id] = shm_nextStartIndex(BUF, _id)

// Bulk write uses memory copy instead of looping for better performance
#define shm_bulkWrite(BUF, _ELEMS, _NUM_ELEMS, _SIZE_OF_ELEM)                            \
    do {                                                                                 \
        if ((BUF->end + _NUM_ELEMS) < BUF->size) {                                       \
            memcpy(BUF->elems + BUF->end, _ELEMS, _NUM_ELEMS * _SIZE_OF_ELEM);           \
            VPMU_MEM_FENCE();                                                            \
            BUF->end = (BUF->end + _NUM_ELEMS) % BUF->size;                              \
        } else {                                                                         \
            memcpy(                                                                      \
              BUF->elems + BUF->end, _ELEMS, (BUF->size - BUF->end) * _SIZE_OF_ELEM);    \
            memcpy(BUF->elems,                                                           \
                   _ELEMS + (BUF->size - BUF->end),                                      \
                   (_NUM_ELEMS - (BUF->size - BUF->end)) * _SIZE_OF_ELEM);               \
            VPMU_MEM_FENCE();                                                            \
            BUF->end = (BUF->end + _NUM_ELEMS) % BUF->size;                              \
        }                                                                                \
    } while (0)

// Bulk read uses memory copy when possible.
// If indexes are close or near the boundary, use normal copy for better performance.
// Also, it enables read to read out all the packets even it's less than buffer size.
#define shm_bulkRead(BUF, _id, _ELEMS, _NUM_ELEMS, _SIZE_OF_ELEM, _IDX_ELEM)             \
    do {                                                                                 \
        _IDX_ELEM = 0;                                                                   \
        if (((int64_t)BUF->end - (int64_t)BUF->start[_id]) > _NUM_ELEMS) {               \
            memcpy(_ELEMS, BUF->elems + BUF->start[_id], _NUM_ELEMS * _SIZE_OF_ELEM);    \
            VPMU_MEM_FENCE();                                                            \
            BUF->start[_id] = (BUF->start[_id] + _NUM_ELEMS) % BUF->size;                \
            _IDX_ELEM       = _NUM_ELEMS;                                                \
        } else {                                                                         \
            for (_IDX_ELEM = 0;                                                          \
                 _IDX_ELEM < _NUM_ELEMS && likely(shm_isBufferNotEmpty(BUF, _id));       \
                 _IDX_ELEM++) {                                                          \
                shm_bufferRead(BUF, _id, _ELEMS[_IDX_ELEM]);                             \
            }                                                                            \
        }                                                                                \
    } while (0)

#define VPMU_LOOP(L_CONDITION, call_back)                                                \
    do {                                                                                 \
        int _index;                                                                      \
        for (_index = 0; L_CONDITION; _index++) {                                        \
            call_back;                                                                   \
        }                                                                                \
    } while (0)

#define shm_waitSpaceSize(BUF, _num, _size)                                              \
    VPMU_LOOP(_index < _num,                                                             \
              while (shm_remainedBufferSpace(BUF, _index) <= _size) usleep(1);)

#define shm_waitBufferSpace(BUF, _num)                                                   \
    VPMU_LOOP(_index < _num, while (unlikely(shm_isBufferFull(BUF, _index))) usleep(1);)

#define shm_waitBufferEmpty(BUF, _num)                                                   \
    VPMU_LOOP(_index < _num,                                                             \
              while (unlikely(shm_isBufferNotEmpty(BUF, _index))) usleep(1);)

#define shm_pass_token(tok) (*tok)++
#define shm_reset_token(tok) *tok = 0
#define shm_wait_token(tok, id)                                                          \
    while (*tok != id) sched_yield()
#endif
