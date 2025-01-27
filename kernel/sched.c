#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <common/rbtree.h>

extern bool panic_flag;

extern void swtch(KernelContext *new_ctx, KernelContext **old_ctx);

static SpinLock rqlock, listlock;
static ListNode rq;

static struct timer sched_timer[NCPU];
static void sched_timer_handler(struct timer*);

void init_sched()
{
    // TODO: initialize the scheduler
    // 1. initialize the resources (e.g. locks, semaphores)
    // 2. initialize the scheduler info of each CPU
    init_spinlock(&rqlock);
    init_spinlock(&listlock);
    init_list_node(&rq);
    for(int i = 0; i < NCPU; ++i) {
        Proc *p = kalloc(sizeof(Proc));
        p->idle = 1;
        p->state = RUNNING;
        cpus[i].sched.this = cpus[i].sched.idle = p;

        sched_timer[i].triggered = true;
        sched_timer[i].data = i;
        sched_timer[i].elapse = 5;
        sched_timer[i].handler = &sched_timer_handler;
    }
}

Proc* thisproc()
{
    // TODO: return the current process
    return cpus[cpuid()].sched.this;
}

void init_schinfo(struct schinfo *p)
{
    // TODO: initialize your customized schinfo for every newly-created process
    init_list_node(&p->rq);
}

void acquire_sched_lock()
{
    // TODO: acquire the sched_lock if need
    acquire_spinlock(&rqlock);
}

void release_sched_lock()
{
    // TODO: release the sched_lock if need
    release_spinlock(&rqlock);
}

bool is_zombie(Proc *p)
{
    bool r;
    acquire_sched_lock();
    r = p->state == ZOMBIE;
    release_sched_lock();
    return r;
}

bool is_unused(Proc *p)
{
    bool r;
    acquire_sched_lock();
    r = p->state == UNUSED;
    release_sched_lock();
    return r;
}

bool _activate_proc(Proc *p, bool onalert)
{
    // TODO:(Lab5 new)
    // if the proc->state is RUNNING/RUNNABLE, do nothing and return false
    // if the proc->state is SLEEPING/UNUSED, set the process state to RUNNABLE, add it to the sched queue, and return true
    // if the proc->state is DEEPSLEEPING, do nothing if onalert or activate it if else, and return the corresponding value.
    acquire_sched_lock();
    if(p->state == RUNNING || p->state == RUNNABLE || p->state == ZOMBIE || (p->state == DEEPSLEEPING && onalert)) {
        release_sched_lock();
        return false;
    }
    if(p->state == SLEEPING || p->state == UNUSED || (p->state == DEEPSLEEPING && !onalert)) {
        p->state = RUNNABLE;
        insert_into_list(&listlock, &rq, &p->schinfo.rq);
    }
    release_sched_lock();
    return true;
}

static void update_this_state(enum procstate new_state)
{
    // TODO: if you use template sched function, you should implement this routinue
    // update the state of current process to new_state, and modify the sched queue if necessary
    auto this = thisproc();
    // TODO*: The code above isn't enough. Consider a Zombie or sleeping proc.
    if(this != cpus[cpuid()].sched.idle && (this->state == RUNNING || this->state == RUNNABLE)) {
        detach_from_list(&listlock, &this->schinfo.rq);
    }
    this->state = new_state;
    if(this != cpus[cpuid()].sched.idle && (new_state == RUNNING || new_state == RUNNABLE)) {
        insert_into_list(&listlock, rq.prev, &this->schinfo.rq);
    }
}

Proc *pick_next()
{
    // TODO: if using template sched function, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process
    if(panic_flag) {
        return cpus[cpuid()].sched.idle;
    }
    // TODO*: Dead, need improving.
    // put it to the back.
    _for_in_list(p, &rq) {
        if(p == &rq) {
            continue;
        }
        auto proc = container_of(p, Proc, schinfo.rq);
        if(proc->state == RUNNABLE) {
            detach_from_list(&listlock, p);
            insert_into_list(&listlock, rq.prev, p);
            return proc;
        }
    }
    return cpus[cpuid()].sched.idle;
    
}

static void update_this_proc(Proc *p)
{
    // TODO: you should implement this routinue
    // update thisproc to the choosen process
    cpus[cpuid()].sched.this = p;

    if(!sched_timer[cpuid()].triggered) {
        cancel_cpu_timer(&sched_timer[cpuid()]);
    }
    set_cpu_timer(&sched_timer[cpuid()]);
}

// A simple scheduler.
// You are allowed to replace it with whatever you like.
// call with sched_lock
void sched(enum procstate new_state)
{
    auto this = thisproc();
    ASSERT(this->state == RUNNING);
    if(this->killed && new_state != ZOMBIE) {
        release_sched_lock();
        return;
    }
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    if (next != this) {
        attach_pgdir(&next->pgdir);
        swtch(next->kcontext, &this->kcontext);
    }
    release_sched_lock();
}

u64 proc_entry(void (*entry)(u64), u64 arg)
{
    release_sched_lock();
    set_return_addr(entry);
    return arg;
}

void sched_timer_handler(struct timer *timerr)
{
    timerr->data = 0;
    timerr->triggered = false;
    acquire_sched_lock();
    sched(RUNNABLE);
}   
