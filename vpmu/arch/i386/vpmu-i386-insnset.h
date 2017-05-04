#ifndef __VPMU_I386_INSNSET_
#define __VPMU_I386_INSNSET_

#define macro_str(str) #str

#define etype(x) X86_INSTRUCTION_##x

#define X86_INSTRUCTION \
    etype(ADD), \
    etype(MUL), \
    etype(MOV), \
    etype(TOTAL_COUNTS)

typedef enum { X86_INSTRUCTION } X86_Instructions;
#undef etype

X86_Instructions get_index_of_x86_insn(const char *s);
#endif // End of __VPMU_INSNSET_
