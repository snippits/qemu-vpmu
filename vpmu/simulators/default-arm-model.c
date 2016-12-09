uint32_t arm_instr_time[ARM_INSTRUCTION_TOTAL_COUNTS];

// Return array length if not found
ARM_Instructions get_index_of_arm_inst(const char *s)
{
#define etype(x) macro_str(x)
    // static is for putting it in global space
    static const char *str_arm_instructions[] = {ARM_INSTRUCTION};
    int                i;

    for (i = 0; i < sizeof(str_arm_instructions) / sizeof(const char *); i++) {
        if (strcmp(str_arm_instructions[i], s) == 0) return (ARM_Instructions)i;
    }

    ERR_MSG("get_index_of_arm_inst: could not find field \"%s\"\n", s);
#undef etype
    return sizeof(str_arm_instructions);
}

#ifdef CONFIG_VPMU_VFP
uint32_t arm_vfp_instr_time[] = {ARM_VFP_INSTRUCTION_TOTAL_COUNTS};
uint32_t arm_vfp_latency[]    = {ARM_VFP_INSTRUCTION_TOTAL_COUNTS};

// Return array length if not found
ARM_VFP_Instructions get_index_of_arm_vfp_inst(const char *s)
{
#define etype(x) macro_str(x)
    // static is for putting it in global space
    static const char *str_arm_vfp_instructions[] = {ARM_VFP_INSTRUCTION};
    int                i;

    for (i = 0; i < sizeof(str_arm_vfp_instructions) / sizeof(const char *); i++) {
        if (strcmp(str_arm_vfp_instructions[i], s) == 0) return (ARM_VFP_Instructions)i;
    }

    ERR_MSG("get_index_of_arm_vfp_inst: could not find field \"%s\"\n", s);
#undef etype
    return sizeof(str_arm_vfp_instructions);
}
#endif

/* The staring address of system RAM. */
#define SYSTEM_RAM_START 0x00000000
#define SYSTEM_RAM_END 0x05ffffff
