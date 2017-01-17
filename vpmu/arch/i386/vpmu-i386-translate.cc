extern "C" {
#include "vpmu-i386-translate.h"
}
#include "vpmu.hpp"      // VPMU common header
#include "vpmu-insn.hpp" // InsnStream, vpmu_insn_stream

// NOTE: These interface function should NOT include CPU core index!!!
// QEMU might translate TBs on core 1 and execute the same TB on core 1 and 2.
// If you care about pipelining on different cores, inject some information
// in extra TB info for runtime pipeline checking with core index.
// The extra TB info could be modified for your needs!!!
//
// If you really want CPU information and some other functions,
// include the following headers in another C file. (NOT CPP)
// And implement your own interface and functions for doing magics.
// #include "qemu/osdep.h"
// #include "cpu.h"
// #include "exec/exec-all.h"
// #include "translate.h"

void vpmu_accumulate_x86_64_ticks(ExtraTBInfo* ex_tb, uint64_t insn)
{
    ex_tb->ticks += vpmu_insn_stream.get_translator(0).get_x86_64_ticks(insn);
}

