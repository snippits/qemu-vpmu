#ifndef __VPMU_INSN_PACKET_HPP_
#define __VPMU_INSN_PACKET_HPP_
#pragma once

extern "C" {
#include "vpmu-conf.h"    // VPMU_MAX_CPU_CORES
#include "vpmu-extratb.h" // Extra TB Information
}

class VPMU_Insn
{
public:
#pragma pack(push) // push current alignment to stack
#pragma pack(8)    // set alignment to 8 bytes boundary
    // Packet type of a single trace
    typedef struct {
        uint16_t     type;            // Packet Type
        uint8_t      num_ex_slots;    // Number of reserved ring buffer slots.
        uint8_t      core;            // Number of CPU core
        uint8_t      mode;            // CPU mode
        ExtraTBInfo *tb_counters_ptr; // A pointer pointing to TB info
    } Reference;

    /// This is the counters in each mode (user/system).
    class DataCell
    {
    public:
        uint64_t cycles[VPMU_MAX_CPU_CORES];
        uint64_t total_insn[VPMU_MAX_CPU_CORES];
        uint64_t load[VPMU_MAX_CPU_CORES];
        uint64_t store[VPMU_MAX_CPU_CORES];
        uint64_t branch[VPMU_MAX_CPU_CORES];

        void reduce(void)
        {
            for (int i = 1; i < VPMU.platform.cpu.cores; i++) {
                this->cycles[0] += this->cycles[i];
                this->total_insn[0] += this->total_insn[i];
                this->load[0] += this->load[i];
                this->store[0] += this->store[i];
                this->branch[0] += this->branch[i];

                this->cycles[i]     = 0;
                this->total_insn[i] = 0;
                this->load[i]       = 0;
                this->store[i]      = 0;
                this->branch[i]     = 0;
            }
        }

        void mask_out_except(int core_id)
        {
            for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
                if (i != core_id) {
                    this->cycles[i]     = 0;
                    this->total_insn[i] = 0;
                    this->load[i]       = 0;
                    this->store[i]      = 0;
                    this->branch[i]     = 0;
                }
            }
        }

        uint64_t *operator[](std::size_t idx)
        {
            // The number of elements in struct DataCell
            const int cell_length = sizeof(DataCell)     // Total bytes
                                    / VPMU_MAX_CPU_CORES // Length of each member
                                    / sizeof(uint64_t);  // Size of each member
            struct ArrayView {
                uint64_t element[cell_length][VPMU_MAX_CPU_CORES];
            };
            struct ArrayView *l = (struct ArrayView *)this;

            return l->element[idx];
        }

        const uint64_t *operator[](std::size_t idx) const
        {
            // The number of elements in struct DataCell
            const int cell_length = sizeof(DataCell)     // Total bytes
                                    / VPMU_MAX_CPU_CORES // Length of each member
                                    / sizeof(uint64_t);  // Size of each member
            struct ArrayView {
                uint64_t element[cell_length][VPMU_MAX_CPU_CORES];
            };
            struct ArrayView *l = (struct ArrayView *)this;

            return l->element[idx];
        }

        int size(void)
        {
            return sizeof(DataCell)     // Total bytes
                   / VPMU_MAX_CPU_CORES // Length of each member
                   / sizeof(uint64_t);  // Size of each member
        }

        DataCell operator+(const DataCell &rhs)
        {
            DataCell  out = {}; // Copy elision
            DataCell &lhs = *this;

            for (int i = 0; i < out.size(); i++) {
                for (int j = 0; j < VPMU.platform.cpu.cores; j++) {
                    out[i][j] = lhs[i][j] + rhs[i][j];
                }
            }

            return out;
        }

        DataCell operator-(const DataCell &rhs)
        {
            DataCell  out = {}; // Copy elision
            DataCell &lhs = *this;

            for (int i = 0; i < out.size(); i++) {
                for (int j = 0; j < VPMU.platform.cpu.cores; j++) {
                    out[i][j] = lhs[i][j] - rhs[i][j];
                }
            }

            return out;
        }
    };

    typedef struct {
        uint64_t cycles;
        uint64_t total_insn;
        uint64_t load;
        uint64_t store;
        uint64_t branch;
    } DataCell_Summed;

    // The data/states of each simulators for VPMU
    class Data
    {
    public:
        DataCell user, system;

        DataCell sum_all_mode(void)
        {
            DataCell out = this->user + this->system; // Copy elision

            return out;
        }

        DataCell_Summed sum_all(void)
        {
            DataCell        p_result = this->sum_all_mode(); // Copy elision
            DataCell_Summed result   = {};                   // Copy elision

            // The number of elements in struct DataCell
            const int cell_length = sizeof(DataCell_Summed) // Total bytes
                                    / sizeof(uint64_t);     // Size of each member
            struct ArrayView_Summed {
                uint64_t element[cell_length];
            };
            struct ArrayView_Summed *res = (struct ArrayView_Summed *)&result;

            for (int i = 0; i < p_result.size(); i++) {
                for (int j = 0; j < VPMU.platform.cpu.cores; j++) {
                    res->element[i] += p_result[i][j];
                }
            }

            return result;
        }

        void reduce(void)
        {
            this->user.reduce();
            this->system.reduce();
        }

        void mask_out_except(int core_id)
        {
            this->user.mask_out_except(core_id);
            this->system.mask_out_except(core_id);
        }

        Data operator+(const Data &rhs)
        {
            Data out = {}; // Copy elision

            out.user   = this->user + rhs.user;
            out.system = this->system + rhs.system;

            return out;
        }

        Data operator-(const Data &rhs)
        {
            Data out = {}; // Copy elision

            out.user   = this->user - rhs.user;
            out.system = this->system - rhs.system;

            return out;
        }
    };

    // The architectural configuration information
    // which VPMU needs to know for some functionalities.
    typedef struct {
        char     name[128];
        uint64_t frequency;
        uint8_t  dual_issue;
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
    // Synchronization Counter to identify the serial number of synchronized data
    volatile uint32_t sync_counter;
    // Synchronization flag to indicate whether it's done (true/false)
    volatile uint32_t synced_flag;

    // Remain the last 128 bits empty to avoid false sharing due to cache line size
    uint32_t _paddings[4];
};

#endif
