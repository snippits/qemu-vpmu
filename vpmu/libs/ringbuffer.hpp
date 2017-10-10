#ifndef _RINGBUFFER_HPP_
#define _RINGBUFFER_HPP_
#pragma once

#include <cassert>   // assert()
#include <cstring>   // std::memcpy
#include <algorithm> // std::min
#include <vector>    // std::vector

/// @brief The ligntening RingBuffer class designed for shared memory
/// @details This class implements a single-writer-multiple-reader ring buffer.
/// The implementation tries to push the throughput to maximum under this broadcasting
/// scenario with bulk data transfer function which can utilize the L1 cache resources
/// while using Linux shared memory as the communication media.
///
/// The implementation is multi-process safe. The writer can use sizeof(xxx) to get the
/// total size to be allocated. On the other hand, the reader side should set the
/// template argument BUFF_SIZE to 1 and never use sizeof to get the total size since
/// allocating resources from the reader side is prohibited any way.
/// The reader side can use the pointer type of this class. With the correct
/// base address, everything works fine.
///
/// @tparam ElementType The type of elements stored in this ring buffer
/// @tparam BUFF_SIZE The number of the elements in this ring buffer.
/// Note that the real size available is BUFF_SIZE - 1 due to the implementation.
/// @tparam MAX_READERS The maximum number of readers can be registered.
template <typename ElementType, int BUFF_SIZE = 1024, int MAX_READERS = 64>
class RingBuffer
{
    static_assert(sizeof(ElementType) % 4 == 0,
                  "Size of ElementType is not aligned to 32-bit");

/// @brief Define the memory fence for safe buffer operations across threads.
/// @details Compiler memory fence is used by default. If memory fence of CPU instruction
/// is required, please use 'asm volatile("mfence" ::: "memory")' instead.
#define MEM_FENCE() asm volatile("" ::: "memory")

public:
    /// @brief default initializer
    RingBuffer() : buffer_size(BUFF_SIZE) { this->elems = this->_elems; }

    /// @brief use custom buffer address for this ring buffer
    /// @param[in] addr The address of custom buffer. Must be aligned to 64-bit.
    /// @param[in] buff_size The size of custom buffer.
    RingBuffer(void* addr, uint64_t buff_size)
    {
        // Address must be aligned to 64
        assert((uintptr_t)addr % 64 == 0);
        this->buffer_size = buff_size;
        this->elems       = addr;
    }

    /// @brief Copy constructor. Only the buffer instance, _elems, is not copied.
    RingBuffer(const RingBuffer& rhs) { copy_ringbuffer(*this, rhs); }

    /// @brief Move constructor. Only the buffer instance, _elems, is not moved.
    RingBuffer(const RingBuffer&& rhs) { copy_ringbuffer(*this, rhs); }

    /// @brief Caution: assignment does not copy the data itself
    RingBuffer& operator=(const RingBuffer& rhs)
    {
        copy_ringbuffer(*this, rhs);
        return *this;
    }

    /// @brief use custom buffer address for this ring buffer
    /// @param[in] addr The address of custom buffer. Must be aligned to 64-bit.
    /// @param[in] buff_size The size of custom buffer.
    void reassign_buffer(void* addr, uint64_t buff_size)
    {
        // Address must be aligned to 64
        assert((uintptr_t)addr % 64 == 0);
        this->buffer_size = buff_size;
        this->elems       = (ElementType*)addr;
    }

    /// @brief register as a reader and retrieve the reader id
    /// @return The id of reader
    uint64_t register_reader(void)
    {
        this->num_readers++;
        assert(this->num_readers < MAX_READERS);
        return this->num_readers;
    }

    /// @brief return the total size of ring buffer
    /// @return The total size of ring buffer
    inline uint64_t total_size(void) { return buffer_size - 1; }

    /// @brief checks whether the underlying container is empty wrt the reader id
    inline bool empty(uint64_t id) { return (this->write_idx == this->read_idx[id]); }

    /// @brief checks whether the underlying container is empty from the perspective
    /// of the writer
    inline bool empty(void)
    {
        bool ret = empty(0);

        for (uint64_t id = 1; id < num_readers; id++) {
            ret &= empty(id);
        }
        return ret;
    }

    /// @brief checks whether the underlying container is full wrt the reader id
    inline bool full(uint64_t id)
    {
        return (this->next_write_idx() == this->read_idx[id]);
    }

    /// @brief checks whether the underlying container is full from the perspective
    /// of the writer
    inline bool full(void)
    {
        bool ret = full(0);

        for (uint64_t id = 1; id < num_readers; id++) {
            ret |= full(id);
        }
        return ret;
    }

    /// @brief returns the number of elements in this buffer wrt the reader id
    /// @return The number of elements in this buffer
    inline uint64_t size(uint64_t id)
    {
        if (this->write_idx >= this->read_idx[id]) {
            return this->write_idx - this->read_idx[id];
        } else {
            return this->buffer_size - (this->read_idx[id] - this->write_idx);
        }
    }

    /// @brief returns the number of elements from the perspective of the writer
    /// @return The number of elements in this buffer
    inline uint64_t size(void)
    {
        uint64_t ret = size(0);

        for (uint64_t id = 1; id < num_readers; id++) {
            ret = std::max(ret, size(id));
        }
        return ret;
    }

    /// @brief return the remained space wrt the reader id
    inline uint64_t remained_space(uint64_t id)
    {
        // Substracting 1 because the pointer of writer cannot be the same as th readers
        // when it is in full condition.
        if (this->write_idx >= this->read_idx[id]) {
            return this->buffer_size - (this->write_idx - this->read_idx[id]) - 1;
        } else {
            return this->read_idx[id] - this->write_idx - 1;
        }
    }

    /// @brief return the remained space from the perspective of the writer
    inline uint64_t remained_space(void)
    {
        uint64_t ret = remained_space(0);

        for (uint64_t id = 1; id < num_readers; id++) {
            ret = std::min(ret, remained_space(id));
        }
        return ret;
    }

    /// @brief insert element at the end
    /// @param[in] item The element to be inserted
    inline void push(const ElementType item)
    {
        if (this->full()) return;
        this->elems[this->write_idx] = item;
        MEM_FENCE();
        this->write_idx = this->next_write_idx();
    }

    /// @brief remove the first element
    /// @param[in] id The id of the reader
    /// @return The last element
    inline ElementType pop(uint64_t id)
    {
        if (this->empty()) return {};
        ElementType item = this->elems[this->read_idx[id]];
        MEM_FENCE();
        this->read_idx[id] = this->next_read_idx(id);
        return item;
    }

    /// @brief a simple helper for reading the default (the first one) reader.
    /// @return The last element
    inline ElementType pop(void) { this->pop(0); }

    /// @brief insert a number of items into this ring buffer
    /// @param[in] items The pointer to the elements to be inserted
    /// @param[in] num The number of elements to be inserted
    inline void push(ElementType* const items, uint64_t num)
    {
        // Reset the num to total number of elements can be written in this transaction.
        num = std::min(num, this->remained_space());
        if (num == 0) return;

        if (this->write_idx + num < this->buffer_size) {
            std::memcpy(&this->elems[this->write_idx], items, num * sizeof(ElementType));
        } else {
            uint64_t cnt = this->buffer_size - this->write_idx;
            // Copy from current pointer to buffer end
            std::memcpy(&this->elems[this->write_idx], items, cnt * sizeof(ElementType));
            // Copy from buffer begining for the rest items
            std::memcpy(this->elems, &items[cnt], (num - cnt) * sizeof(ElementType));
        }
        MEM_FENCE();
        this->write_idx = (this->write_idx + num) % this->buffer_size;
    }

    /// @brief insert a vector of items into this ring buffer
    /// @param[in] items The vector of elements to be inserted
    inline void push(const std::vector<ElementType>& items)
    {
        this->push(&items[0], items.size());
    }

    /// @brief remove a number of items from this ring buffer
    /// @details perforamcne test:
    /// 1024 iterations, 1638400 * 4 / bytes_per_read (times)
    /// bytes of reads : 819200,  8192,  2048
    /// i7-4790K       :  445ms, 280ms, 400ms
    /// @param[in] id The id of the reader
    /// @param[out] items The pointer to the elements to be placed
    /// @param[in] num The number of elements to be read
    /// @return The total number of elements successfully read
    inline uint64_t pop(uint64_t id, ElementType* items, uint64_t num)
    {
        // Reset the num to total number of elements can be read in this transaction.
        num = std::min(num, this->size(id));
        if (num == 0) return 0;

        if (this->read_idx[id] + num < this->buffer_size) {
            std::memcpy(
              items, &this->elems[this->read_idx[id]], num * sizeof(ElementType));
        } else {
            uint64_t cnt = this->buffer_size - this->read_idx[id];
            // Copy from current pointer to buffer end
            std::memcpy(
              items, &this->elems[this->read_idx[id]], cnt * sizeof(ElementType));
            // Copy from buffer begining for the rest items
            std::memcpy(&items[cnt], this->elems, (num - cnt) * sizeof(ElementType));
        }
        MEM_FENCE();
        this->read_idx[id] = (this->read_idx[id] + num) % this->buffer_size;
        return num;
    }

    /// @brief remove a number of items from this ring buffer
    /// @details perforamcne test:
    /// 1024 iterations, 1638400 * 4 / bytes_per_read (times)
    /// bytes of reads : 819200,  8192,  2048
    /// i7-4790K       :  615ms, 430ms, 590ms
    /// @param[in] id The id of the reader
    /// @param[in] num The number of elements to be read
    /// @return The vector of elements with the size of the number of successfully read
    inline std::vector<ElementType> pop(uint64_t id, uint64_t num)
    {
        // Reset the num to total number of elements can be read in this transaction.
        num = std::min(num, this->size(id));
        std::vector<ElementType> ret(num);
        this->pop(id, &ret[0], num);
        return ret;
    }

    void* get_elems(void) { return this->elems; }

private:
    /// @brief return the index of the next write
    inline uint64_t next_write_idx(void)
    {
        return (this->write_idx + 1) % this->buffer_size;
    }

    /// @brief return the index of the next read
    inline uint64_t next_read_idx(uint64_t id)
    {
        return (this->read_idx[id] + 1) % this->buffer_size;
    }

    void copy_ringbuffer(RingBuffer& lhs, const RingBuffer& rhs)
    {
        lhs.num_readers = rhs.num_readers;
        for (uint64_t i = 0; i < rhs.num_readers; i++) lhs.read_idx[i] = rhs.read_idx[i];
        lhs.write_idx   = rhs.write_idx;
        lhs.buffer_size = rhs.buffer_size;
        lhs.elems       = rhs.elems;
    }

private:
    uint64_t          buffer_size           = 0;       ///< Size of this buffer
    uint64_t          num_readers           = 0;       ///< Number of readers
    volatile uint64_t read_idx[MAX_READERS] = {};      ///< Index of readers
    volatile uint64_t write_idx             = 0;       ///< Index of writer
    const uint64_t    padding[8]            = {};      ///< 8 words of padding
    ElementType*      elems                 = nullptr; ///< Pointer to the real buffer
    /// @brief The real buffer. Never use this directly.
    /// @details This is used for static size of buffer. For compatibility of using
    /// Linux shared memory, where the size of buffer cannot be known at compile time,
    /// _elems must not be used at any reason.
    /// The reader can use a pointer of this class with BUFF_SIZE=1 and read buffer_size
    /// from shared memory. Then use elems to get the real pointer of the buffer.
    /// All of the above mentioned mechanisms are built in this class.
    ElementType _elems[BUFF_SIZE];
};

#endif
