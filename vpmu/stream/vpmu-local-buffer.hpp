#ifndef __VPMU_LOCAL_BUFFER_HPP_
#define __VPMU_LOCAL_BUFFER_HPP_

// Using vector will suffer some performance issue on this critical path.
// Statically assigining the size for the best performance
// This is also designed and optimized for multi-threading
template <typename Reference, const int SIZE = 256>
class VPMULocalBuffer
{
public:
    // return false if the buffer is full
    inline bool push_back(Reference &ref)
    {
        if (isFull()) return false;

        buffer[index] = ref;
        index++;
        return true;
    }

    inline bool isFull(void) { return (index == SIZE); }
    inline bool isEmpty(void) { return (index == 0); }

    inline Reference *get_buffer() { return buffer; }
    inline uint32_t   get_size() { return SIZE; }
    inline uint32_t   get_index() { return index; }

    void reset()
    {
        index = 0;
        memset(buffer, 0, sizeof(buffer));
    }

private:
    // Size + 8 (padding for efficiency of threading)
    Reference buffer[SIZE];
    uint32_t  index = 0;
    uint64_t  padding[8]; // 8 words of padding
};

#endif
