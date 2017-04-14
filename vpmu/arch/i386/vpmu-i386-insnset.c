#include "vpmu.h"              // VPMU common headers
#include "vpmu-i386-insnset.h" // X86 Instruction SET
#include "vpmu-log.h"          // ERR_MSG

// Return array length if not found
X86_Instructions get_index_of_x86_insn(const char *s)
{
#define etype(x) macro_str(x)
    // static is for putting it in global space
    static const char *str_x86_instructions[] = {X86_INSTRUCTION};
    int                i;

    for (i = 0; i < sizeof(str_x86_instructions) / sizeof(const char *); i++) {
        if (strcmp(str_x86_instructions[i], s) == 0) return (X86_Instructions)i;
    }

    ERR_MSG("get_index_of_x86_insn: could not find field \"%s\"\n", s);
#undef etype
    return X86_INSTRUCTION_TOTAL_COUNTS;
}

#ifdef CONFIG_VPMU_VFP
// Return array length if not found
X86_VFP_Instructions get_index_of_arm_vfp_insn(const char *s)
{
#define etype(x) macro_str(x)
    // static is for putting it in global space
    static const char *str_x86_vfp_instructions[] = {X86_VFP_INSTRUCTION};
    int                i;

    for (i = 0; i < sizeof(str_x86_vfp_instructions) / sizeof(const char *); i++) {
        if (strcmp(str_x86_vfp_instructions[i], s) == 0) return (X86_VFP_Instructions)i;
    }

    ERR_MSG("get_index_of_x86_vfp_insn: could not find field \"%s\"\n", s);
#undef etype
    return X86_VFP_INSTRUCTION_TOTAL_COUNTS;
}
#endif
