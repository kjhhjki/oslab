#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <kernel/proc.h>
#include <test/test.h>
#include <driver/virtio.h>

volatile bool panic_flag;

NO_RETURN void idle_entry()
{
    set_cpu_on();
    while (1) {
        yield();
        if (panic_flag)
            break;
        arch_with_trap
        {
            arch_wfi();
        }
    }
    set_cpu_off();
    arch_stop_cpu();
}

extern char icode[], eicode[];
void trap_return();
NO_RETURN void kernel_entry()
{
    init_filesystem();

    printk("Hello world! (Core %lld)\n", cpuid());

    // printk("Starting 1st pgfault test.");
    // pgfault_first_test();
    // printk("Starting 2nd pgfault test.");
    // pgfault_second_test();
    // printk("Pgfault test over.");

    // proc_test();
    // vm_test();
    // user_proc_test();
    // io_test();

    /* LAB 4 TODO 3 BEGIN */
    /* LAB 4 TODO 3 END */

    /**
     * (Final) TODO BEGIN 
     * 
     * Map init.S to user space and trap_return to run icode.
     */
    auto p = create_proc();
    for (u64 q = (u64)icode; q < (u64)eicode; q += PAGE_SIZE) {
        *get_pte(&p->pgdir, 0x400000 + q - (u64)icode, true) = K2P(q) | PTE_USER_DATA;
    }
    p->ucontext->x[0] = 0;
    p->ucontext->elr = 0x400000;
    p->ucontext->spsr = 0;
    OpContext op;
    bcache.begin_op(&op);
    p->cwd = namei("/", &op);
    bcache.end_op(&op);
    set_parent_to_this(p);
    start_proc(p, trap_return, 0);
    printk("start\n");
    while (1)
    {
        yield();
        arch_with_trap
        {
            arch_wfi();
        }
    }

    /* (Final) TODO END */
}

NO_INLINE NO_RETURN void _panic(const char *file, int line)
{
    printk("=====%s:%d PANIC%lld!=====\n", file, line, cpuid());
    panic_flag = true;
    set_cpu_off();
    for (int i = 0; i < NCPU; i++) {
        if (cpus[i].online)
            i--;
    }
    printk("Kernel PANIC invoked at %s:%d. Stopped.\n", file, line);
    arch_stop_cpu();
}