/*
 * Virtual Performance Monitor Unit
 * Copyright (c) 2016 PAS Lab, CSIE, National Taiwan University, Taiwan.
 */
#include "vpmu/include/vpmu-device.h" // VPMU Device mapping and macros
#include "vpmu/include/vpmu.h"        // Import the common configurations

#include "qemu/osdep.h"    // DeviceState, VMState, etc.
#include "qemu/timer.h"    // QEMU_CLOCK_VIRTUAL, timer_new_ns()
#include "cpu.h"           // QEMU CPU definitions and macros (CPUArchState)
#include "hw/sysbus.h"     // SysBusDevice
#include "exec/exec-all.h" // tlb_fill()

// QEMU realted device information, such as BUS, IRQ, VPMU strucutre
typedef struct vpmu_state {
    SysBusDevice busdev;
    qemu_irq     irq;
    MemoryRegion iomem;

    // Custom data array
    uint8_t data[VPMU_DEVICE_IOMEM_SIZE];
    // Periodically Interrupt Timers of VPMU
    QEMUTimer *timer[QEMU_CLOCK_MAX];     // Use 'QEMUClockType' as the index
    uint64_t   last_tick[QEMU_CLOCK_MAX]; // Used to accumulate the expired time for timer
    // You can add custom field here for variable life cycle management
} vpmu_state_t;

static const VMStateDescription vpmu_vmstate = {
  .name               = VPMU_DEVICE_NAME,
  .version_id         = 1,
  .minimum_version_id = 1,
  .fields =
    (VMStateField[]){VMSTATE_UINT8_ARRAY(data, vpmu_state_t, VPMU_DEVICE_IOMEM_SIZE),
                     VMSTATE_TIMER_PTR_ARRAY(timer, vpmu_state_t, QEMU_CLOCK_MAX),
                     VMSTATE_UINT64_ARRAY(last_tick, vpmu_state_t, QEMU_CLOCK_MAX),
                     VMSTATE_END_OF_LIST()}};

// Saving QEMU's context for TLB and MMU use. (copy data from/to guest)
static CPUState *vpmu_cpu_context[VPMU_MAX_CPU_CORES];

void vpmu_simulator_status(VPMU_Struct *vpmu)
{
    vpmu->timing_model &VPMU_INSN_COUNT_SIM ? CONSOLE_LOG("o : ") : CONSOLE_LOG("x : ");
    CONSOLE_LOG("Instruction Simulation\n");

    vpmu->timing_model &VPMU_DCACHE_SIM ? CONSOLE_LOG("o : ") : CONSOLE_LOG("x : ");
    CONSOLE_LOG("Data Cache Simulation\n");

    vpmu->timing_model &VPMU_ICACHE_SIM ? CONSOLE_LOG("o : ") : CONSOLE_LOG("x : ");
    CONSOLE_LOG("Insn Cache Simulation\n");

    vpmu->timing_model &VPMU_BRANCH_SIM ? CONSOLE_LOG("o : ") : CONSOLE_LOG("x : ");
    CONSOLE_LOG("Branch Predictor Simulation\n");

    vpmu->timing_model &VPMU_PIPELINE_SIM ? CONSOLE_LOG("o : ") : CONSOLE_LOG("x : ");
    CONSOLE_LOG("Pipeline Simulation\n");

    vpmu->timing_model &VPMU_JIT_MODEL_SELECT ? CONSOLE_LOG("o : ") : CONSOLE_LOG("x : ");
    CONSOLE_LOG("JIT Model Selection\n");

    vpmu->timing_model &VPMU_EVENT_TRACE ? CONSOLE_LOG("o : ") : CONSOLE_LOG("x : ");
    CONSOLE_LOG("VPMU Event Trace mechanism\n");
}

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
static CPUState *vpmu_clone_qemu_cpu_state(CPUState *cpu)
{
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

static uint64_t special_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t      ret    = 0;
    vpmu_state_t *status = (vpmu_state_t *)opaque;

    DBG(STR_VPMU "read vpmu device at addr=0x%lx\n", addr);
    (void)status;
    return ret;
}

static void special_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
#ifdef CONFIG_VPMU_SET
    uintptr_t  paddr = 0;
    char *     buffer;
    static int buffer_size = 0;
    FILE *     fp;
#endif

    DBG(STR_VPMU "write vpmu device at addr=0x%lx value=%ld\n", addr, value);

#if 0
    //This is a test code to read user data in guest virtual address from host
    if (addr == 100 * 4) {
        ERR_MSG("VA:%lx\n", value);
        uintptr_t guest_vaddr = vpmu_get_phy_addr_global(VPMU.cs, value);
        ERR_MSG("PA:%lx\n", guest_vaddr);
        ERR_MSG("value:%lx\n", *(unsigned long int *)guest_vaddr);
        return ;
    }
#endif

    switch (addr) {
    case VPMU_MMAP_ENABLE:
        VPMU_reset();
        VPMU.enabled      = 1;
        VPMU.timing_model = value;
        vpmu_simulator_status(&VPMU);

        tic(&(VPMU.start_time));
        break;
    case VPMU_MMAP_DISABLE:
        VPMU_sync();
        toc(&(VPMU.start_time), &(VPMU.end_time));

        VPMU_dump_result();

        VPMU.enabled = 0;
#ifdef CONFIG_VPMU_VFP
        // FILE_LOG("Before print_vfp_count \n");
        print_vfp_count();
#endif
        break;
    case VPMU_MMAP_REPORT:
        VPMU_sync();
        VPMU_dump_result();

        break;
#ifdef CONFIG_VPMU_SET
    case VPMU_MMAP_SET_PROC_NAME:
        if (value != 0) {
            // Copy the whole CPU context including TLB Table and MMU registers for
            // VPMU's use.
            if (vpmu_cpu_context[0] != NULL) {
                free(vpmu_cpu_context[0]->env_ptr);
                free(vpmu_cpu_context[0]);
            }
            vpmu_cpu_context[0] = vpmu_clone_qemu_cpu_state(VPMU.cs);
            paddr               = vpmu_get_phy_addr_global(vpmu_cpu_context[0], value);
            DBG(STR_SET "trace process name:%s\n", (char *)paddr);
            strcpy(VPMU.traced_process_name, (char *)paddr);
        }
        break;
    case VPMU_MMAP_SET_PROC_SIZE:
        buffer_size = value;
        break;

    case VPMU_MMAP_SET_PROC_BIN:
        if (buffer_size != 0) {
            buffer = (char *)malloc(buffer_size);
            if (buffer == NULL) {
                ERR_MSG("Can not allocate memory\n");
                exit(EXIT_FAILURE);
            }
            vpmu_copy_from_guest(buffer, value, buffer_size, VPMU.cs);
            fp = fopen("/tmp/vpmu-traced-bin", "wb");
            if (fp != NULL) {
                fwrite(buffer, buffer_size, 1, fp);
            }
            fclose(fp);
            free(buffer);
            buffer_size = 0;
            buffer      = NULL;
            EFD *efd    = efd_open_elf((char *)"/tmp/vpmu-traced-bin");
            dump_symbol_table(efd);
            efd_close(efd);
        }
        break;
#endif
    default:
        CONSOLE_LOG(
          STR_VPMU "write 0x%ld to unknown address 0x%lx of vpd\n", value, addr);
    }

    // Preventing unused warnings
    (void)vpmu_cpu_context;
}

static const MemoryRegionOps vpmu_dev_ops = {
  .read = special_read, .write = special_write, .endianness = DEVICE_NATIVE_ENDIAN,
};

static void start_vpmu_tick(vpmu_state_t *status)
{
    for (int type = 0; type < QEMU_CLOCK_MAX; type++) {
        if (status->timer[type] != NULL) {
            status->last_tick[type] = qemu_clock_get_us(type);
            timer_mod(status->timer[type], status->last_tick[type]);
        }
    }
}

static void vpmu_tick_virtual(void *opaque)
{
    vpmu_state_t *status = (vpmu_state_t *)opaque;
    uint64_t      tick   = status->last_tick[QEMU_CLOCK_VIRTUAL];

    tick += 10000; // 10ms
    // DBG("tick virtual %lu\n", tick);

    // Periodically synchronize simulator data back to VPMU
    // VPMU_sync_non_blocking();

    timer_mod(status->timer[QEMU_CLOCK_VIRTUAL], tick);
    status->last_tick[QEMU_CLOCK_VIRTUAL] = tick;
    (void)status;
}

static void vpmu_tick_real(void *opaque)
{
    vpmu_state_t *status = (vpmu_state_t *)opaque;
    uint64_t      tick   = status->last_tick[QEMU_CLOCK_REALTIME];

    tick += 10000; // 10ms
    // DBG("tick real %lu\n", tick);

    timer_mod(status->timer[QEMU_CLOCK_REALTIME], tick);
    status->last_tick[QEMU_CLOCK_REALTIME] = tick;
    (void)status;
}

void vpmu_dev_init(uint32_t base)
{
    CONSOLE_LOG(STR_VPMU "init vpmu device on addr 0x%x. \n", base);

    sysbus_create_simple(VPMU_DEVICE_NAME, base, NULL);
}

static void vpmu_dev_reset(DeviceState *dev)
{
}

static void vpmu_dev_instance_init(Object *obj)
{
    DeviceState * dev    = DEVICE(obj);
    vpmu_state_t *status = OBJECT_CHECK(vpmu_state_t, dev, VPMU_DEVICE_NAME);
    SysBusDevice *sbd    = SYS_BUS_DEVICE(obj);

    // Clear the custom data segment
    memset(status->data, 0, VPMU_DEVICE_IOMEM_SIZE);

    // sysbus_init_irq(sbd, &s->irq);
    // qdev_init_gpio_out(dev, &s->trigger, 1);
    memory_region_init_io(&status->iomem,
                          obj,
                          &vpmu_dev_ops,
                          status,
                          VPMU_DEVICE_NAME,
                          VPMU_DEVICE_IOMEM_SIZE);
    sysbus_init_mmio(sbd, &status->iomem);

    // Clear timer array first!
    for (int i = 0; i < QEMU_CLOCK_MAX; i++) status->timer[i] = NULL;
    // Assign timers
    status->timer[QEMU_CLOCK_REALTIME] =
      timer_new_us(QEMU_CLOCK_REALTIME, vpmu_tick_real, status);
    status->timer[QEMU_CLOCK_VIRTUAL] =
      timer_new_us(QEMU_CLOCK_VIRTUAL, vpmu_tick_virtual, status);
    // Start timers
    start_vpmu_tick(status);
}

static void vpmu_dev_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);

    dc->vmsd  = &vpmu_vmstate;
    dc->reset = vpmu_dev_reset;
}

static const TypeInfo vpmu_dev_info = {
  .name          = VPMU_DEVICE_NAME,
  .parent        = TYPE_SYS_BUS_DEVICE,
  .instance_size = sizeof(vpmu_state_t),
  .instance_init = vpmu_dev_instance_init,
  .class_init    = vpmu_dev_class_init,
};

static void vpmu_dev_register_types(void)
{
    type_register_static(&vpmu_dev_info);
}

type_init(vpmu_dev_register_types);
