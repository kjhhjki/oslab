#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <aarch64/mmu.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <kernel/paging.h>
#include <kernel/pt.h>

Proc root_proc;

void kernel_entry();
void proc_entry();

static int pid;
static SpinLock plock;
static SpinLock listlock;

typedef struct ValList {
    int val;
    struct ValList *pre, *nxt;
} List;

List freepid;
static SpinLock pidlock;

void acquire_pidlock() {
    acquire_spinlock(&pidlock);
}
void release_pidlock() {
    release_spinlock(&pidlock);
}
bool list_empty(List* l) {
    bool r;
    // acquire_pidlock();
    r = l->nxt == l;
    // release_pidlock();
    return r;
}
void list_insert(List *p1, List *p2) {
    acquire_pidlock();
    
    if(!p1 || !p2) return;
    List *p3 = p1->nxt, *p4 = p2->pre;

    p1->nxt = p2; p2->pre = p1;
    p4->nxt = p3; p3->pre = p4;

    release_pidlock();
}
void list_erase(List *l) {
    // acquire_pidlock();
    List *pre = l->pre, *nxt = l->nxt;
    pre->nxt = nxt;
    nxt->pre = pre;
    l->nxt = l->pre = l;
    kfree(l);
    // release_pidlock();
}

int get_pid() {
    acquire_pidlock();
    if(list_empty(&freepid)) {
        int p = ++pid;
        release_pidlock();
        return p;
    } else {
        int p = freepid.nxt->val;
        list_erase(freepid.nxt);
        release_pidlock();
        return p;
    }
}

// init_kproc initializes the kernel process
// NOTE: should call after kinit
void init_kproc()
{
    // TODO:
    // 1. init global resources (e.g. locks, semaphores)
    // 2. init the root_proc (finished)
    ASSERT(cpuid() == 0);
    init_spinlock(&plock);
    init_spinlock(&listlock);
    freepid.pre = freepid.nxt = &freepid;
    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
}

void init_proc(Proc *p)
{
    // TODO:
    // setup the Proc with kstack and pid allocated
    // NOTE: be careful of concurrency
    acquire_spinlock(&plock);
    memset(p, 0, sizeof(*p));
    p->pid = get_pid();
    p->idle = 0;
    init_sem(&p->childexit, 0);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    p->parent = NULL;
    p->kstack = kalloc_page();
    init_oftable(&p->oftable);
    init_schinfo(&p->schinfo);
    p->kcontext = (KernelContext*)((u64)p->kstack + PAGE_SIZE - 16 - sizeof(KernelContext) - sizeof(UserContext));
    p->ucontext = (UserContext*)((u64)p->kstack + PAGE_SIZE - 16 - sizeof(UserContext));

    p->killed = false;
    init_pgdir(&p->pgdir);

    release_spinlock(&plock);
}

Proc *create_proc()
{
    Proc *p = kalloc(sizeof(Proc));
    init_proc(p);
    return p;
}

void set_parent_to_this(Proc *proc)
{
    // TODO: set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL
    acquire_spinlock(&plock);
    ASSERT(proc->parent == NULL);
    proc->parent = thisproc();
    insert_into_list(&listlock, &thisproc()->children, &proc->ptnode);
    release_spinlock(&plock);
}

int start_proc(Proc *p, void (*entry)(u64), u64 arg)
{
    // TODO:
    // 1. set the parent to root_proc if NULL
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    // 3. activate the proc and return its pid
    // NOTE: be careful of concurrency
    acquire_spinlock(&plock);
    if(p->parent == NULL) {
        p->parent = &root_proc;
        insert_into_list(&listlock, &root_proc.children, &p->ptnode);
    }
    p->kcontext->lr = (u64)&proc_entry;
    p->kcontext->x0 = (u64)entry;
    p->kcontext->x1 = (u64)arg;
    int id = p->pid;
    activate_proc(p);
    release_spinlock(&plock);
    return id;
}

int wait(int *exitcode)
{
    // TODO:
    // 1. return -1 if no children
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its pid and exitcode
    // NOTE: be careful of concurrency
    acquire_spinlock(&plock);
    auto this = thisproc();
    if(this->children.next == &this->children) {
        release_spinlock(&plock);
        return -1;
    }
    release_spinlock(&plock);
    if(!wait_sem(&this->childexit)) {
        return -1;
    }
    acquire_spinlock(&plock);
    acquire_sched_lock();
    Proc *zombie = NULL;
    _for_in_list(p, &this->children) {
        if(p == &this->children) continue;
        auto childproc = container_of(p, Proc, ptnode);
        ASSERT(childproc->parent == this);
        ASSERT(&childproc->ptnode == p);
        if (childproc->state == ZOMBIE) {
            zombie = childproc;
            break;
        }
    }
    if(zombie != NULL) {
        ASSERT(zombie->state == ZOMBIE);
        detach_from_list(&listlock, &zombie->ptnode);
        detach_from_list(&listlock, &zombie->schinfo.rq);
        *exitcode = zombie->exitcode;
        kfree_page(zombie->kstack);
        int npid = zombie->pid;
        List *l = kalloc(sizeof(List));
        l->val = npid;
        l->nxt = l->pre = l;
        list_insert(&freepid, l);
        kfree(zombie);
        release_sched_lock();
        release_spinlock(&plock);
        return npid;
    }
    release_sched_lock();
    release_spinlock(&plock);
    return -1;
}

NO_RETURN void exit(int code)
{
    // TODO:
    // 1. set the exitcode
    // 2. clean up the resources
    // 3. transfer children to the root_proc, and notify the root_proc if there is zombie
    // 4. sched(ZOMBIE)
    // NOTE: be careful of concurrency
    acquire_spinlock(&plock);
    acquire_sched_lock();

    auto this = thisproc();
    ASSERT(this != &root_proc);
    this->exitcode = code;
    int times = 0;
    _for_in_list(p, &this->children){
        if(p == &this->children) continue;
        auto childproc = container_of(p, Proc, ptnode);
        ASSERT(childproc->parent == this);
        childproc->parent = &root_proc;
        if(childproc->state == ZOMBIE) {
            ++times;
        } 
    }
    if(!_empty_list(&this->children)) {
        merge_list(&listlock, &root_proc.children, this->children.next);
        detach_from_list(&listlock, &this->children);
        release_sched_lock();
        for(int i = 0; i < times; ++i) {
            post_sem(&root_proc.childexit);
        }
        acquire_sched_lock();
    }
    for (int i = 0; i < 16; ++i) {
        if (this->oftable.openfile[i]){
            release_sched_lock();
            release_spinlock(&plock);
            file_close(this->oftable.openfile[i]);
            acquire_spinlock(&plock);
            acquire_sched_lock();
            this->oftable.openfile[i] = NULL;
        }
    }
    release_sched_lock();
    release_spinlock(&plock);
    if (this->cwd) {
        inodes.put(NULL, this->cwd);
    }
    acquire_spinlock(&plock);
    acquire_sched_lock();
    free_pgdir(&this->pgdir);
    release_sched_lock();
    post_sem(&thisproc()->parent->childexit); 
    acquire_sched_lock();
    release_spinlock(&plock);
    sched(ZOMBIE);
    PANIC(); // prevent the warning of 'no_return function returns'
}

Proc* search(int pid, Proc *cur) 
{
    if(cur->pid == pid && !is_unused(cur)) {
        cur->killed = true;
        return cur;
    }
    _for_in_list(p, &cur->children){
        if(p == &cur->children) {
            continue;
        }
        auto childproc = container_of(p, Proc, ptnode);
        Proc *ch = search(pid, childproc);
        if(ch != NULL) {
            return ch;
        }
    }
    return NULL;
}

int kill(int pid)
{
    // TODO:
    // Set the killed flag of the proc to true and return 0.
    // Return -1 if the pid is invalid (proc not found).
    acquire_spinlock(&plock);
    Proc *target = search(pid, &root_proc);
    release_spinlock(&plock);
    if(target != NULL) {
        if(target->ucontext->elr >> 48) {
            return -1;
        } else {
            alert_proc(target);
            return 0;
        }
    }
    return -1;
}
/*
 * Create a new process copying p as the parent.
 * Sets up stack to return as if from system call.
 */
void trap_return();
int fork()
{
    /**
     * (Final) TODO BEGIN
     * 
     * 1. Create a new child process.
     * 2. Copy the parent's memory space.
     * 3. Copy the parent's trapframe.
     * 4. Set the parent of the new proc to the parent of the parent.
     * 5. Set the state of the new proc to RUNNABLE.
     * 6. Activate the new proc and return its pid.
     */
    printk("fork\n");
    auto cur = thisproc();
    auto proc = create_proc();
    if (proc == NULL) {
        return -1;
    }
    auto tmp = vm_copy(&cur->pgdir);
    if (tmp == NULL) {
        kfree(proc->kstack);
        acquire_spinlock(&plock);
        proc->state = UNUSED;
        release_spinlock(&plock);
        return -1;
    }
    proc->pgdir = *tmp;
    proc->parent = cur;
    memmove(proc->ucontext, cur->ucontext, sizeof(*proc->ucontext));
    proc->ucontext->x[0] = 0;
    for (int i = 0; i < 16; ++i) {
        if (cur->oftable.openfile[i]) {
            proc->oftable.openfile[i] = file_dup(cur->oftable.openfile[i]);
        }
    }
    proc->cwd = inodes.share(cur->cwd);
    int pid = proc->pid;
    acquire_spinlock(&plock);
    insert_into_list(&listlock, &cur->children, &proc->ptnode);
    release_spinlock(&plock);
    start_proc(proc, trap_return, 0);
    return pid;
    /* (Final) TODO END */
}