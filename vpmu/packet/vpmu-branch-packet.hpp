#ifndef __VPMU_BRANCH_PACKET_HPP_
#define __VPMU_BRANCH_PACKET_HPP_
#pragma once

extern "C" {
#include "vpmu-conf.h" // VPMU_MAX_CPU_CORES
}

class VPMU_Branch
{
public:
    // Defining the types (struct) for communication

#pragma pack(push) // push current alignment to stack
#pragma pack(8)    // set alignment to 8 bytes boundary
    // Packet type of a single trace
    typedef struct {
        uint16_t type;         // Packet Type
        uint8_t  num_ex_slots; // Number of reserved ring buffer slots.
        uint8_t  core;         // Number of CPU core
        uint64_t pc;           // PC Address of branch instruction
        uint8_t  taken;        // Is this a taken branch
    } Reference;

    // The data/states of each simulators for VPMU
    class Data
    {
    public:
        uint64_t correct[VPMU_MAX_CPU_CORES]; // branch_predict_correct counter
        uint64_t wrong[VPMU_MAX_CPU_CORES];   // branch_predict_wrong counter
        // uint64_t cycles[VPMU_MAX_CPU_CORES];

        void reduce(void)
        {
            for (int i = 1; i < VPMU.platform.cpu.cores; i++) {
                this->correct[0] += this->correct[i];
                this->wrong[0] += this->wrong[i];
                // this->cycles[0] += this->cycles[i];
                this->correct[i] = 0;
                this->wrong[i]   = 0;
                // this->cycles[i] = 0;
            }
        }

        void mask_out_except(int core_id)
        {
            for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
                if (i != core_id) {
                    this->correct[i] = 0;
                    this->wrong[i]   = 0;
                    // this->cycles[i] = 0;
                }
            }
        }

        Data operator+(const Data &rhs)
        {
            Data out = {}; // Copy elision

            for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
                out.correct[i] = this->correct[i] + rhs.correct[i];
                out.wrong[i]   = this->wrong[i] + rhs.wrong[i];
            }
            return out;
        }

        Data operator-(const Data &rhs)
        {
            Data out = {}; // Copy elision

            for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
                out.correct[i] = this->correct[i] - rhs.correct[i];
                out.wrong[i]   = this->wrong[i] - rhs.wrong[i];
            }
            return out;
        }
    };

    // The architectural configuration information
    // which VPMU needs to know for some functionalities.
    typedef struct {
        char     name[128];
        uint32_t latency;
    } Model;
#pragma pack(pop) // restore original alignment from stack

public:
    // Defining the instances for communication between VPMU and workers.

    // A number representing the ID of current worker (timing simulator)
    uint32_t id;
    // semaphore for signaling worker thread
    sem_t job_semaphore;
    // Timing simulator model information that VPMU required for some functions
    Model model;
    // cache counter information
    Data data;
    // Synchronization Counter to identify the serial number of synchronized data
    volatile uint32_t sync_counter;
    // Synchronization flag to indicate whether it's done (true/false)
    volatile uint32_t synced_flag;

    // Remain the last 128 bits empty to avoid false sharing due to cache line size
    uint32_t _paddings[4];
};

#endif
