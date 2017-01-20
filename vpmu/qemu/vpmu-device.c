/*
 * Virtual Performance Monitor Unit
 * Copyright (c) 2016 PAS Lab, CSIE, National Taiwan University, Taiwan.
 */
#include "qemu/osdep.h"    // DeviceState, VMState, etc.
#include "qemu/timer.h"    // QEMU_CLOCK_VIRTUAL, timer_new_ns()
#include "cpu.h"           // QEMU CPU definitions and macros (CPUArchState)
#include "hw/sysbus.h"     // SysBusDevice
#include "exec/exec-all.h" // tlb_fill()

#include "vpmu/include/vpmu-device.h" // VPMU Device mapping and macros
#include "vpmu/include/vpmu.h"        // Import the common configurations
#include "vpmu/include/event-tracing/event-tracing.h"

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
static CPUArchState *vpmu_cpu_context[VPMU_MAX_CPU_CORES];

void vpmu_simulator_status(VPMU_Struct *vpmu)
{
    vpmu->timing_model &VPMU_WHOLE_SYSTEM ? CONSOLE_LOG("o : ") : CONSOLE_LOG("x : ");
    CONSOLE_LOG("Whole System Profiling\n");

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
    static char *binary_name = NULL;
    void *       paddr       = NULL;
    char *       buffer;
    static int   buffer_size = 0;
    FILE *       fp;
#endif

    DBG(STR_VPMU "write vpmu device at addr=0x%lx value=%ld\n", addr, value);

#if 0
    // This is a test code to read user data in guest virtual address from host
    if (addr == 100 * 4) {
        ERR_MSG("VA:%lx\n", value);
        void *guest_addr = vpmu_tlb_get_host_addr(VPMU.cpu_arch_state, value);
        ERR_MSG("PA:%p\n", (void *)guest_addr);
        ERR_MSG("value:%lx\n", *(unsigned long int *)guest_addr);
        return;
    }
#endif

    switch (addr) {
    case VPMU_MMAP_SET_TIMING_MODEL:
        VPMU.timing_model = value;
        break;
    case VPMU_MMAP_ENABLE:
        VPMU_reset();
        VPMU.enabled      = 1;
        VPMU.timing_model = value | VPMU_WHOLE_SYSTEM;
        vpmu_simulator_status(&VPMU);

        tic(&(VPMU.start_time));
        break;
    case VPMU_MMAP_DISABLE:
        VPMU.enabled = 0;
        VPMU_sync();
        toc(&(VPMU.start_time), &(VPMU.end_time));

#ifdef CONFIG_VPMU_VFP
        // FILE_LOG("Before print_vfp_count \n");
        print_vfp_count();
#endif
        break;
    case VPMU_MMAP_REPORT:
        VPMU_sync();
        toc(&(VPMU.start_time), &(VPMU.end_time));
        VPMU_dump_result();
        break;
    case VPMU_MMAP_RESET:
        VPMU_reset();
        tic(&(VPMU.start_time));
        break;
#ifdef CONFIG_VPMU_SET
    case VPMU_MMAP_ADD_PROC_NAME:
        if (value != 0) {
            // Copy the whole CPU context including TLB Table and MMU registers for
            // VPMU's use.
            vpmu_qemu_free_cpu_arch_state(vpmu_cpu_context[0]);
            vpmu_cpu_context[0] = vpmu_qemu_clone_cpu_arch_state(VPMU.cpu_arch_state);
            paddr               = vpmu_tlb_get_host_addr(vpmu_cpu_context[0], value);
            binary_name         = (char *)paddr;
            DBG(STR_VPMU "trace process name: %s\n", (char *)paddr);
            if (!et_find_program_in_list((const char *)paddr)) {
                // Only push to the list when it's not duplicated
                et_add_program_to_list((const char *)paddr);
            }
        }
        break;
    case VPMU_MMAP_REMOVE_PROC_NAME:
        if (value != 0) {
            paddr = vpmu_tlb_get_host_addr(VPMU.cpu_arch_state, value);
            DBG(STR_VPMU "remove traced process: %s\n", (char *)paddr);
            et_remove_program_from_list((const char *)paddr);
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
            vpmu_copy_from_guest(buffer, value, buffer_size, VPMU.cpu_arch_state);
            fp = fopen("/tmp/vpmu-traced-bin", "wb");
            if (fp != NULL) {
                fwrite(buffer, buffer_size, 1, fp);
            }
            fclose(fp);
            free(buffer);
            buffer_size = 0;
            buffer      = NULL;
            if (binary_name != NULL)
            et_update_program_elf_dwarf(binary_name, "/tmp/vpmu-traced-bin");
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
    int type;
    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
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
    int           i;
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
    for (i = 0; i < QEMU_CLOCK_MAX; i++) status->timer[i] = NULL;
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
