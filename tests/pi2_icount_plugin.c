/* QEMU 6.2 plugin API v1. Host instrumentation; no guest instructions added.
 * Build against the upstream qemu-plugin.h (see test_pi2_icount.py).
 */
#include "qemu-plugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
/* Internal QEMU 6.2 ABI: QEMU_CLOCK_VIRTUAL == 1. Not a portable plugin API.
 * Used only for auxiliary timer observations, not instruction totals.
 */
extern int64_t qemu_clock_get_ns(int clock);
static uint64_t instructions, other_instructions, mmio, memory, origin, others;
static uint64_t disk_updates, work_disk_updates;
static uint64_t wait_pc, exit_pc, work_count, work_mmio, work_memory;
static int64_t previous_ns, work_ns;
static int64_t latest_timer_ns;
static int first_wait;
static unsigned samples, limit, warmup = 120000;
static int active, waiting;
static FILE *output;

static void count_other(unsigned cpu, void *count)
{
    if (cpu != 1 && active) other_instructions += (uintptr_t)count;
}

static void mem(unsigned cpu, qemu_plugin_meminfo_t info, uint64_t address, void *unused)
{
    (void)unused;
    if (cpu == 1 && address == 0x3f003004) {
        latest_timer_ns = qemu_clock_get_ns(1);
        if (first_wait && active) work_ns = latest_timer_ns - previous_ns;
        first_wait = 0;
    }
    if (cpu != 1 || !active || waiting) return;
    ++memory;
    struct qemu_plugin_hwaddr *hw = qemu_plugin_get_hwaddr(info, address);
    if (hw && qemu_plugin_hwaddr_is_io(hw)) ++mmio;
}

static void marker(unsigned cpu, void *which)
{
    if (cpu != 1) return;
    if ((uintptr_t)which == 3) {
        if (active && !waiting) ++disk_updates;
        return;
    }
    int64_t now = latest_timer_ns;
    if ((uintptr_t)which == 1) {
        if (active && !waiting) {
            work_count = instructions - origin;
            work_mmio = mmio; work_memory = memory;
            work_disk_updates = disk_updates;
            first_wait = 1;
        }
        waiting = 1;
    } else {
        if (active) {
            if (warmup) {
                --warmup;
                goto next_period;
            }
            fprintf(output, "%u,%llu,%llu,%llu,%llu,%lld,%lld,%lld,%llu,%llu\n",
                samples, (unsigned long long)work_count,
                (unsigned long long)(instructions - origin - work_count),
                (unsigned long long)work_memory, (unsigned long long)work_mmio,
                (long long)work_ns, (long long)(now - previous_ns),
                (long long)(now / 1000 - previous_ns / 1000),
                (unsigned long long)(other_instructions - others),
                (unsigned long long)work_disk_updates);
            if (++samples == limit) {
                fclose(output);
                _exit(0); /* Disposable VM, no card changes; signal completion to runner. */
            }
        }
next_period:
        active = 1; waiting = 0;
        origin = instructions; previous_ns = now;
        others = other_instructions;
        mmio = memory = disk_updates = 0;
    }
}

static void translate(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    (void)id;
    size_t n = qemu_plugin_tb_n_insns(tb);
    qemu_plugin_register_vcpu_tb_exec_cb(tb, count_other, QEMU_PLUGIN_CB_NO_REGS, (void *)(uintptr_t)n);
    for (size_t i = 0; i < n; ++i) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t pc = qemu_plugin_insn_vaddr(insn);
        qemu_plugin_register_vcpu_insn_exec_inline(insn, QEMU_PLUGIN_INLINE_ADD_U64, &instructions, 1);
        qemu_plugin_register_vcpu_mem_cb(insn, mem, QEMU_PLUGIN_CB_NO_REGS,
                                         QEMU_PLUGIN_MEM_RW, NULL);
        if (pc == wait_pc || pc == exit_pc)
            qemu_plugin_register_vcpu_insn_exec_cb(insn, marker, QEMU_PLUGIN_CB_NO_REGS,
                                                  (void *)(uintptr_t)(pc == wait_pc ? 1 : 2));
#ifdef DISK_ACTIVE_PC
        if (pc == DISK_ACTIVE_PC)
            qemu_plugin_register_vcpu_insn_exec_cb(insn, marker, QEMU_PLUGIN_CB_NO_REGS, (void *)3);
#endif
    }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                                          int argc, char **argv)
{
    if (argc != 4 || info->version.cur != 1) return -1;
    const char *values[4];
    for (int i = 0; i < 4; ++i) {
        char *equals = strchr(argv[i], '=');
        if (!equals) return -1;
        values[i] = equals + 1;
    }
    output = fopen(values[0], "w");
    if (!output) return -1;
    wait_pc = strtoull(values[1], NULL, 0); exit_pc = strtoull(values[2], NULL, 0);
    limit = strtoul(values[3], NULL, 0);
    fprintf(output, "sample,work_insns,wait_insns,memory_accesses,mmio_accesses,work_ns,period_ns,timer_delta_us,other_core_insns,disk_updates\n");
    qemu_plugin_register_vcpu_tb_trans_cb(id, translate);
    return 0;
}
