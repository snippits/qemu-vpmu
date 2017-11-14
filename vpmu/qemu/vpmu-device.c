/*
 * Virtual Performance Monitor Unit
 * Copyright (c) 2017 PAS Lab, CSIE, National Taiwan University, Taiwan.
 */
#include "qemu/osdep.h"    // DeviceState, VMState, etc.
#include "qemu/timer.h"    // QEMU_CLOCK_VIRTUAL, timer_new_ns()
#include "cpu.h"           // QEMU CPU definitions and macros (CPUArchState)
#include "hw/sysbus.h"     // SysBusDevice
#include "exec/exec-all.h" // tlb_fill()

#include "vpmu/vpmu-common.h"      // Common headers and macros
#include "vpmu/qemu/vpmu-device.h" // VPMU Device mapping and macros
#include "vpmu/vpmu.h"             // Import the common configurations
#include "vpmu/event-tracing/event-tracing.h"

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

static void save_binary_file(const char *file, const char *buff, size_t buff_size)
{
    FILE *fp = NULL;

    fp = fopen(file, "wb");
    if (fp) {
        fwrite(buff, buff_size, 1, fp);
        fclose(fp);
    }
    return;
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
    static char *kallsym_name = NULL;
    static char *binary_name  = NULL;
    void *       paddr        = NULL;
    char *       buffer       = NULL;
    static int   buffer_size  = 0;
    void *       per_core_env = NULL;
#endif

#if TARGET_LONG_BITS == 64
    // All VPMU MMAP addresses are aligned to 64 bits
    // But we still need to handle the problem of QEMU generating two accesses
    // when emulating 64 bits platform which passes the data in 64 bits.
    // This is just a walk-around to make the rest of code act like
    // receiving 64 bits of data with a single access to addr.
    static uint64_t value_lower_bytes = 0;
    if ((addr & 0x04) == 0 && size == 8) {
        // Nothing to do when the access is aligned and the size is 8 bytes.
    } else {
        // If the size is not correct, try to assemble the 8 bytes values.
        if ((addr & 0x04) == 0) {
            value_lower_bytes = value;
            return;
        } else {
            value = (value << 32) | value_lower_bytes;
            addr  = addr & (~0x04);
        }
    }
#endif
    // DBG(STR_VPMU "write vpmu device at addr=0x%lx value=%ld\n", addr, value);

    switch (addr) {
    case VPMU_MMAP_SET_TIMING_MODEL:
        VPMU.timing_model = value;
        vpmu_print_status(&VPMU);

        break;
    case VPMU_MMAP_ENABLE:
        VPMU_reset();
        VPMU.enabled      = 1;
        VPMU.timing_model = value | VPMU_WHOLE_SYSTEM;
        vpmu_print_status(&VPMU);

        tic(&(VPMU.enabled_time_t));
        break;
    case VPMU_MMAP_DISABLE:
        VPMU.enabled = 0;
        VPMU_sync();
        toc(&(VPMU.enabled_time_t), &(VPMU.disabled_time_t));

#ifdef CONFIG_VPMU_VFP
        // TODO Finish the VFP tracking
        print_vfp_count();
#endif
        break;
    case VPMU_MMAP_REPORT:
        VPMU_sync();
        toc(&(VPMU.enabled_time_t), &(VPMU.disabled_time_t));
        VPMU_dump_result();
        break;
    case VPMU_MMAP_RESET:
        // TODO Temporarily disable reset for phase tracing. This should be done in controller.
        // VPMU_reset();
        // tic(&(VPMU.enabled_time_t));
        break;
#ifdef CONFIG_VPMU_SET
    case VPMU_MMAP_ADD_PROC_NAME:
        if (VPMU.platform.kvm_enabled) break;
        if (value == 0) break;
        per_core_env = VPMU.core[vpmu_get_core_id()].cpu_arch_state;
        paddr        = vpmu_tlb_get_host_addr(per_core_env, value);
        binary_name  = (char *)paddr;
        DBG(STR_VPMU "Trace process name: %s\n", binary_name);
        // Check neither program nor process exists before updating a new one.
        // Some processes are monitored but binary does not exist in the list.
        // This usually happens when using attach mode (attach to a running process).
        if (!et_find_program_in_list(binary_name)
            && !et_find_process_in_list(binary_name)) {
            // Only push to the list when it's not duplicated
            DBG(STR_VPMU "Push process: %s into list\n", binary_name);
            et_add_program_to_list(binary_name);
        }
        break;
    case VPMU_MMAP_REMOVE_PROC_NAME:
        if (VPMU.platform.kvm_enabled) break;
        if (value == 0) break;
        per_core_env = VPMU.core[vpmu_get_core_id()].cpu_arch_state;
        paddr        = vpmu_tlb_get_host_addr(per_core_env, value);
        DBG(STR_VPMU "Remove traced process: %s\n", (char *)paddr);
        et_remove_program_from_list((const char *)paddr);
        break;
    case VPMU_MMAP_SET_PROC_SIZE:
        if (VPMU.platform.kvm_enabled) break;
        buffer_size = value;
        break;
    case VPMU_MMAP_SET_PROC_BIN:
        if (VPMU.platform.kvm_enabled) break;
        if (value == 0 || buffer_size == 0) break;
        buffer = (char *)malloc(buffer_size);
        if (buffer == NULL) {
            ERR_MSG("Cannot allocate memory for receiving binary\n");
            break;
        }
        per_core_env = VPMU.core[vpmu_get_core_id()].cpu_arch_state;
        vpmu_copy_from_guest(buffer, value, buffer_size, per_core_env);
        save_binary_file("/tmp/vpmu-traced-bin", buffer, buffer_size);
        free(buffer);
        buffer      = NULL;
        buffer_size = 0;
        DBG(STR_VPMU "Save binary '%s' to /tmp/vpmu-traced-bin\n", binary_name);
        if (binary_name != NULL) {
            et_update_program_elf_dwarf(binary_name, "/tmp/vpmu-traced-bin");
        }
        break;
    case VPMU_MMAP_OFFSET_FILE_f_path_dentry:
    case VPMU_MMAP_OFFSET_DENTRY_d_iname:
    case VPMU_MMAP_OFFSET_DENTRY_d_parent:
    case VPMU_MMAP_OFFSET_THREAD_INFO_task:
    case VPMU_MMAP_OFFSET_TASK_STRUCT_pid:
        if (VPMU.platform.kvm_enabled) break;
        et_set_linux_struct_offset(addr, value);
        break;
    case VPMU_MMAP_OFFSET_LINUX_VERSION:
        VPMU.platform.linux_version = value;
        DBG(STR_VPMU "Running with Linux version %lu.%lu.%lu\n",
            (VPMU.platform.linux_version >> 16) & 0xff,
            (VPMU.platform.linux_version >> 8) & 0xff,
            (VPMU.platform.linux_version >> 0) & 0xff);
        break;
    case VPMU_MMAP_OFFSET_KERNEL_SYM_NAME:
        if (VPMU.platform.kvm_enabled) break;
        per_core_env = VPMU.core[vpmu_get_core_id()].cpu_arch_state;
        paddr        = vpmu_tlb_get_host_addr(per_core_env, value);
        kallsym_name = (char *)paddr;
        break;
    case VPMU_MMAP_OFFSET_KERNEL_SYM_ADDR:
        if (VPMU.platform.kvm_enabled) break;
        et_set_linux_sym_addr(kallsym_name, value);
        break;
    case VPMU_MMAP_THREAD_SIZE:
        et_set_linux_thread_struct_size(value);
        break;
#endif
    default:
        CONSOLE_LOG(
          STR_VPMU "write 0x%lx to unknown address 0x%lx of vpd\n", value, addr);
    }
}

static const MemoryRegionOps vpmu_dev_ops = {
  .read       = special_read,
  .write      = special_write,
  .endianness = DEVICE_NATIVE_ENDIAN,
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

static void vpmu_dev_reset(DeviceState *dev) {}

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
