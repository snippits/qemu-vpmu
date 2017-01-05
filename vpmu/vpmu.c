#include "vpmu/libs/efd.h"
#include "vpmu/include/vpmu.h"

#include "qemu/osdep.h"    // DeviceState, VMState, etc.
#include "qemu/timer.h"    // QEMU_CLOCK_VIRTUAL, timer_new_ns()
#include "cpu.h"           // QEMU CPU definitions and macros (CPUArchState)
#include "hw/sysbus.h"     // SysBusDevice
#include "exec/exec-all.h" // tlb_fill()

// TODO try
// static bool get_phys_addr(CPUARMState *env, target_ulong address,
// 8084                           int access_type, ARMMMUIdx mmu_idx,
// 8085                           hwaddr *phys_ptr, MemTxAttrs *attrs, int *prot,
// 8086                           target_ulong *page_size, uint32_t *fsr,
// 8087                           ARMMMUFaultInfo *fi)
uintptr_t vpmu_get_phy_addr_global(void *ptr, uintptr_t vaddr)
{
    int           retry            = 0;
    const int     READ_ACCESS_TYPE = 0;
    uintptr_t     paddr            = 0;
    CPUState *    cpu_state        = (CPUState *)ptr;
    CPUArchState *cpu_env          = (CPUArchState *)cpu_state->env_ptr;
    int           mmu_idx          = cpu_mmu_index(cpu_env, false);
    int           index;
    target_ulong  tlb_addr;

    index = (vaddr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
redo:
    retry++;
    if (retry > 10) {
        ERR_MSG(STR_VPMU "fail to find vaddr 0x%lx\n", vaddr);
        return 0;
    }
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
// TODO Need to find better way to allocate
// TODO Need to free
void vpmu_update_qemu_cpu_state(void *source_cpu_v, void *target_cpu_v)
{
    CPUState *s_cpu = (CPUState *)source_cpu_v;
    CPUState *t_cpu = (CPUState *)target_cpu_v;
#if defined(TARGET_ARM)
    memcpy(t_cpu, ARM_CPU(s_cpu), sizeof(ARMCPU));
#elif defined(TARGET_X86_64) || defined(TARGET_I386)
    memcpy(t_cpu, X86_CPU(s_cpu), sizeof(X86CPU));
#else
#error "VPMU does not support this architecture!"
#endif
    memcpy(t_cpu->env_ptr, s_cpu->env_ptr, sizeof(CPUArchState));
}

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

#if 0
static void dump_symbol_table(EFD *efd)
{
    int i;
    /* Push special functions into hash table */
    for (i = 0; i < efd_get_sym_num(efd); i++) {
        if ((efd_get_sym_type(efd, i) == STT_FUNC)
            && (efd_get_sym_vis(efd, i) == STV_DEFAULT)
            && (efd_get_sym_shndx(efd, i) != SHN_UNDEF)) {
            uint32_t vaddr    = efd_get_sym_value(efd, i) & 0xfffffffe;
            char *   funcName = efd_get_sym_name(efd, i);
            CONSOLE_LOG("%s: 0x%X\n", funcName, vaddr);
        }
    }
}
#endif

void vpmu_dump_elf_symbols(const char *file_path)
{
//    EFD *efd = efd_open_elf((char *)file_path);
//    dump_symbol_table(efd);
//    efd_close(efd);
}

uint64_t h_time_difference(struct timespec *t1, struct timespec *t2)
{
    uint64_t period = 0;

    period = t2->tv_nsec - t1->tv_nsec;
    period += (t2->tv_sec - t1->tv_sec) * 1000000000;

    return period;
}

void tic(struct timespec *t1)
{
    clock_gettime(CLOCK_REALTIME, t1);
}

uint64_t toc(struct timespec *t1, struct timespec *t2)
{
    clock_gettime(CLOCK_REALTIME, t2);
    return h_time_difference(t1, t2);
}

uint8_t *vpmu_read_ptr_from_guest(void *cs, uint64_t addr, uint64_t offset)
{
    return (uint8_t *)vpmu_get_phy_addr_global((void *)cs, (uint64_t)addr) + offset;
}

uint8_t vpmu_read_uint8_from_guest(void *cs, uint64_t addr, uint64_t offset)
{
    return *((uint8_t *)vpmu_read_ptr_from_guest(cs, addr, offset));
}

uint16_t vpmu_read_uint16_from_guest(void *cs, uint64_t addr, uint64_t offset)
{
    return *((uint16_t *)vpmu_read_ptr_from_guest(cs, addr, offset));
}

uint32_t vpmu_read_uint32_from_guest(void *cs, uint64_t addr, uint64_t offset)
{
    return *((uint32_t *)vpmu_read_ptr_from_guest(cs, addr, offset));
}

uint64_t vpmu_read_uint64_from_guest(void *cs, uint64_t addr, uint64_t offset)
{
    return *((uint64_t *)vpmu_read_ptr_from_guest(cs, addr, offset));
}

uintptr_t vpmu_read_uintptr_from_guest(void *cs, uint64_t addr, uint64_t offset)
{
#if defined(TARGET_LONG_BITS)
#if (TARGET_LONG_BITS == 32)
    return (uintptr_t)vpmu_read_uint32_from_guest(cs, addr, offset);
#elif (TARGET_LONG_BITS == 64)
    return (uintptr_t)vpmu_read_uint64_from_guest(cs, addr, offset);
#endif
#else
#error TARGET_LONG_BITS undefined
#endif
}
