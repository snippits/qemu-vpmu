#include "vpmu/libs/efd.h"
#include "vpmu/include/vpmu.h"

#include "qemu/osdep.h"    // DeviceState, VMState, etc.
#include "qemu/timer.h"    // QEMU_CLOCK_VIRTUAL, timer_new_ns()
#include "cpu.h"           // QEMU CPU definitions and macros (CPUArchState)
#include "hw/sysbus.h"     // SysBusDevice
#include "exec/exec-all.h" // tlb_fill()

uintptr_t vpmu_get_phy_addr_global(void *ptr, uintptr_t vaddr)
{
    const int     READ_ACCESS_TYPE = 0;
    uintptr_t     paddr            = 0;
    CPUState *    cpu_state        = (CPUState *)ptr;
    CPUArchState *cpu_env          = (CPUArchState *)cpu_state->env_ptr;
    int           mmu_idx          = cpu_mmu_index(cpu_env, false);
    int           index;
    target_ulong  tlb_addr;

    index = (vaddr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
redo:
    tlb_addr = cpu_env->tlb_table[mmu_idx][index].addr_read;
    if ((vaddr & TARGET_PAGE_MASK)
        == (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
        if (tlb_addr & ~TARGET_PAGE_MASK) {
            ERR_MSG(STR_VPMU "should not access IO currently\n");
        } else {
            uintptr_t addend;
            addend = cpu_env->tlb_table[mmu_idx][index].addend;
            paddr  = (uintptr_t)(vaddr + addend);
        }
    } else {
        tlb_fill(cpu_state, vaddr, READ_ACCESS_TYPE, mmu_idx, GETPC());
        goto redo;
    }

    return paddr;
}

size_t vpmu_copy_from_guest(void *dst, uintptr_t src, const size_t size, void *cs)
{
    size_t left_size = size;

    while (left_size > 0) {
        uintptr_t phy_addr = vpmu_get_phy_addr_global(cs, src);

        if (phy_addr) {
            size_t valid_len = TARGET_PAGE_SIZE - (phy_addr & ~TARGET_PAGE_MASK);
            if (valid_len > left_size) valid_len = left_size;
            memcpy(dst, (void *)phy_addr, valid_len);
            dst = ((uint8_t *)dst) + valid_len;
            src += valid_len;
            left_size -= valid_len;
        }
    }

    if (left_size) {
        ERR_MSG(STR_VPMU "Page fault in copy from guest\n");
    }

    return (size - left_size);
}

#ifdef CONFIG_VPMU_SET
void *vpmu_clone_qemu_cpu_state(void *cpu_v)
{
    CPUState *cpu = (CPUState *)cpu_v;
#if defined(TARGET_ARM)
    CPUState *cpu_state_ptr = (CPUState *)malloc(sizeof(ARMCPU));
    memcpy(cpu_state_ptr, ARM_CPU(cpu), sizeof(ARMCPU));
#elif defined(TARGET_X86_64) || defined(TARGET_I386)
    CPUState *cpu_state_ptr = (CPUState *)malloc(sizeof(X86CPU));
    memcpy(cpu_state_ptr, X86_CPU(cpu), sizeof(X86CPU));
#else
#error "VPMU does not support this architecture!"
#endif
    CPUArchState *cpu_arch_state_ptr = (CPUArchState *)malloc(sizeof(CPUArchState));
    memcpy(cpu_arch_state_ptr, cpu->env_ptr, sizeof(CPUArchState));

    cpu_state_ptr->env_ptr = cpu_arch_state_ptr;
    return cpu_state_ptr;
}
#endif

static void dump_symbol_table(EFD *efd)
{
    /* Push special functions into hash table */
    for (int i = 0; i < efd_get_sym_num(efd); i++) {
        if ((efd_get_sym_type(efd, i) == STT_FUNC)
            && (efd_get_sym_vis(efd, i) == STV_DEFAULT)
            && (efd_get_sym_shndx(efd, i) != SHN_UNDEF)) {
            uint32_t vaddr    = efd_get_sym_value(efd, i) & 0xfffffffe;
            char *   funcName = efd_get_sym_name(efd, i);
            CONSOLE_LOG("%s: 0x%X\n", funcName, vaddr);
        }
    }
}

void vpmu_dump_elf_symbols(const char *file_path)
{
    EFD *efd = efd_open_elf((char *)file_path);
    dump_symbol_table(efd);
    efd_close(efd);
}

