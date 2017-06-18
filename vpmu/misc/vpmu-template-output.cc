#include "vpmu-template-output.hpp"

// We use 17 digits plus 3 characters (20 in total) to ensure a
// pretty output on the minimum 80 characters (width) tty console.
#define U64_20C "%'17" PRIu64 " | "
#define F64_20C "%'17.2lf | "

namespace vpmu
{

namespace output
{
    void print_u64_array(uint64_t value[])
    {
        int i = 0;
        CONSOLE_LOG(" ");
        // Fold when tty is too narrow
        if (VPMU.platform.cpu.cores > 4
            || vpmu::utils::get_tty_columns() < 20 * VPMU.platform.cpu.cores + 36) {
            CONSOLE_LOG("\n");
        }
        for (i = 0; i < VPMU.platform.cpu.cores; i++) {
            CONSOLE_LOG(U64_20C, value[i]);
            if (i != 0 && i % 4 == 0) CONSOLE_LOG("\n");
        }
        CONSOLE_LOG("\n");
    }

    void print_percentage_array(uint64_t first_val[], uint64_t second_val[])
    {
        int i = 0;
        CONSOLE_LOG(" ");
        // Fold when tty is too narrow
        if (VPMU.platform.cpu.cores > 4
            || vpmu::utils::get_tty_columns() < 20 * VPMU.platform.cpu.cores + 36) {
            CONSOLE_LOG("\n");
        }
        for (i = 0; i < VPMU.platform.cpu.cores; i++) {
            double perc = (double)first_val[i] / (first_val[i] + second_val[i] + 1);
            CONSOLE_LOG(F64_20C, perc);
            if (i != 0 && i % 4 == 0) CONSOLE_LOG("\n");
        }
        CONSOLE_LOG("\n");
    }

    void CPU_counters(VPMU_Insn::Model model, VPMU_Insn::Data data)
    {
        using vpmu::math::sum_cores;

        CONSOLE_U64(" Total instruction count        :", data.sum_all().cycles);
        CONSOLE_U64(" Total instruction count        :", data.sum_all().total_insn);
        CONSOLE_LOG("   ->User mode insn count       :");
        print_u64_array(data.user.total_insn);
        CONSOLE_LOG("   ->Supervisor mode insn count :");
        print_u64_array(data.system.total_insn);
        CONSOLE_U64(" Total load instruction count   :", data.sum_all().load);
        CONSOLE_LOG("   ->User mode load count       :");
        print_u64_array(data.user.load);
        CONSOLE_LOG("   ->Supervisor mode load count :");
        print_u64_array(data.system.load);
        CONSOLE_U64(" Total store instruction count  :", data.sum_all().store);
        CONSOLE_LOG("   ->User mode store count      :");
        print_u64_array(data.user.store);
        CONSOLE_LOG("   ->Supervisor mode store count:");
        print_u64_array(data.system.store);
    }

    void Branch_counters(VPMU_Branch::Model model, VPMU_Branch::Data data)
    {
        using vpmu::math::sum_cores;

        // Accuracy
        CONSOLE_LOG("    -> predict accuracy         :");
        print_percentage_array(data.correct, data.wrong);
        // Correct
        CONSOLE_LOG("    -> correct prediction       :");
        print_u64_array(data.correct);
        // Wrong
        CONSOLE_LOG("    -> wrong prediction         :");
        print_u64_array(data.wrong);
    }

    void Cache_counters(VPMU_Cache::Model model, VPMU_Cache::Data data)
    {
        // Dump info
        CONSOLE_LOG("       (Miss Rate)     "
                    "|    Access Count   "
                    "|  Read Miss Count  "
                    "|  Write Miss Count "
                    "|\n");

        CONSOLE_LOG("    -> memory   (%0.2lf) | " U64_20C U64_20C U64_20C "\n",
                    (double)0.0,
                    (uint64_t)data.memory_accesses,
                    (uint64_t)0,
                    (uint64_t)0);

        for (int l = model.levels; l >= VPMU_Cache::L2_CACHE; l--) {
            auto&&   c       = data.data_cache[PROCESSOR_CPU][l][0];
            uint64_t rw      = c[VPMU_Cache::READ] + c[VPMU_Cache::WRITE];
            uint64_t rw_miss = c[VPMU_Cache::READ_MISS] + c[VPMU_Cache::WRITE_MISS];

            // Index start from the last level of cache to the L2 cache
            CONSOLE_LOG("    -> L%d-D     (%0.2lf) | " U64_20C U64_20C U64_20C "\n",
                        l,
                        (double)rw_miss / (rw + 1),
                        (uint64_t)(rw),
                        (uint64_t)(c[VPMU_Cache::READ_MISS]),
                        (uint64_t)(c[VPMU_Cache::WRITE_MISS]));
        }

        for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
            auto&&   c       = data.data_cache[PROCESSOR_CPU][VPMU_Cache::L1_CACHE][i];
            uint64_t rw      = c[VPMU_Cache::READ] + c[VPMU_Cache::WRITE];
            uint64_t rw_miss = c[VPMU_Cache::READ_MISS] + c[VPMU_Cache::WRITE_MISS];

            CONSOLE_LOG("    -> L1-D[%2d] (%0.2lf) | " U64_20C U64_20C U64_20C "\n",
                        i,
                        (double)rw_miss / (rw + 1),
                        (uint64_t)(rw),
                        (uint64_t)(c[VPMU_Cache::READ_MISS]),
                        (uint64_t)(c[VPMU_Cache::WRITE_MISS]));

            auto&&   ci       = data.insn_cache[PROCESSOR_CPU][VPMU_Cache::L1_CACHE][i];
            uint64_t irw      = ci[VPMU_Cache::READ] + ci[VPMU_Cache::WRITE];
            uint64_t irw_miss = ci[VPMU_Cache::READ_MISS] + ci[VPMU_Cache::WRITE_MISS];

            CONSOLE_LOG("    -> L1-I[%2d] (%0.2lf) | " U64_20C U64_20C U64_20C "\n",
                        i,
                        (double)irw_miss / (irw + 1),
                        (uint64_t)(irw),
                        (uint64_t)(ci[VPMU_Cache::READ_MISS]),
                        (uint64_t)(ci[VPMU_Cache::WRITE_MISS]));
        }
    }

} // End of namespace vpmu::output
} // End of namespace vpmu
