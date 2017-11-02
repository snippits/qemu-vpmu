#ifndef __VPMU_PACKET_HPP_
#define __VPMU_PACKET_HPP_
#pragma once

extern "C" {
#include "vpmu-qemu.h"   // VPMUPlatformInfo
#include "vpmu-conf.h"   // VPMU_MAX_CPU_CORES
#include "vpmu-packet.h" // VPMU Packet Types
}

#include "ringbuffer.hpp" // RingBuffer class

// FIXME this is a temporary data type
typedef struct {
    uint16_t type;         // Packet Type
    uint8_t  num_ex_slots; // Number of reserved ring buffer slots.
    uint8_t  core;         // Number of CPU core
    uint64_t id;           // Special ID associated with this control packet
} CommandPacket;

// This class defines the layout of VPMU ring buffer with its common data, etc.
// We only use this as layout mapping, not object instance because the underlying memory
// might be a file or pure memory.
// The overall layout of memory in heap is as follows

#pragma pack(push) // push current alignment to stack
#pragma pack(8)    // set alignment to 8 bytes boundary
template <typename T, int SIZE = 0>
class StreamLayout
{
public:
    using Reference = typename T::Reference;
    using Data      = typename T::Data;

    VPMUPlatformInfo platform_info;                ///< The cpu information
    T                common[VPMU_MAX_NUM_WORKERS]; ///< Configs/states
    uint32_t         token;                        ///< Token variable
    uint64_t         heart_beat;                   ///< Heartbeat signals
    uint64_t         padding[8];                   ///< 8 words of padding
    /// The buffer for sending traces. This must be the last member for correct layout.
    RingBuffer<Reference, SIZE, VPMU_MAX_NUM_WORKERS> trace;
    /// The buffer for receiving the performance counters asychronously
    Data sync_data[VPMU_MAX_NUM_WORKERS][32];
};
#pragma pack(pop) // restore original alignment from stack

#endif
