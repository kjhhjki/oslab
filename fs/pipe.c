#include <kernel/mem.h>
#include <kernel/sched.h>
#include <fs/pipe.h>
#include <common/string.h>
#include <kernel/printk.h>

// typedef struct pipe {
//     SpinLock lock;
//     Semaphore wlock, rlock;
//     char data[PIPE_SIZE];
//     u32 nread; // Number of bytes read
//     u32 nwrite; // Number of bytes written
//     int readopen; // Read fd is still open
//     int writeopen; // Write fd is still open
// } Pipe;

void init_pipe(Pipe *pi)
{
    /* (Final) TODO BEGIN */
    init_spinlock(&pi->lock);
    init_sem(&pi->wlock, 0);
    init_sem(&pi->rlock, 0);        
    memset(pi->data, 0, sizeof(pi->data));
    pi->nread = 0;
    pi->nwrite = 0;
    pi->readopen = 1; 
    pi->writeopen = 1;
    /* (Final) TODO END */
}

void init_read_pipe(File *readp, Pipe *pipe)
{
    /* (Final) TODO BEGIN */
    init_pipe(pipe);
    readp->type = FD_PIPE;
    readp->pipe = pipe;
    readp->readable = true;
    readp->writable = false;
    /* (Final) TODO END */
}

void init_write_pipe(File *writep, Pipe *pipe)
{
    /* (Final) TODO BEGIN */
    init_pipe(pipe);
    writep->type = FD_PIPE;
    writep->pipe = pipe;
    writep->readable = false;
    writep->writable = true;
    /* (Final) TODO END */
}

int pipe_alloc(File **f0, File **f1)
{
    /* (Final) TODO BEGIN */
    Pipe *p = NULL;
    *f0 = *f1 = NULL;
    if ((*f0 = file_alloc()) == 0 || (*f1 = file_alloc()) == 0 || 
        (p = (Pipe*)kalloc(sizeof(Pipe))) == 0) {
        if (p) {
            kfree((char *)p);
        }
        if (*f0) {
            file_close(*f0);
        }
        if (*f1) {
            file_close(*f1);
        }
        return -1;
    }
    init_read_pipe(*f0, p);
    init_read_pipe(*f1, p);
    return 0;
    /* (Final) TODO END */
}

void pipe_close(Pipe *pi, int writable)
{
    /* (Final) TODO BEGIN */
    acquire_spinlock(&pi->lock);
    if (writable) {
        pi->writeopen = 0;
        post_sem(&pi->rlock);
    } else {
        pi->readopen = 0;
        post_sem(&pi->wlock);
    }
    if (pi->readopen == 0 && pi->writeopen == 0) {
        release_spinlock(&pi->lock);
        kfree((void*)pi);
    } else {
        release_spinlock(&pi->lock);
    }
    /* (Final) TODO END */
}

int pipe_write(Pipe *pi, u64 addr, int n)
{
    /* (Final) TODO BEGIN */
    acquire_spinlock(&pi->lock);
    for (int i = 0; i < n; ++i) {
        while (pi->nwrite == pi->nread + PIPE_SIZE){
            if (pi->readopen == 0 || thisproc()->killed){
                release_spinlock(&pi->lock);
                return -1;
            }
            post_sem(&pi->rlock);
            release_spinlock(&pi->lock);
            unalertable_wait_sem(&pi->wlock);
        }
        pi->data[pi->nwrite++ % PIPE_SIZE] = *((char *)addr + i);
    }
    post_sem(&pi->rlock);
    release_spinlock(&pi->lock);
    return n;
    /* (Final) TODO END */
}

int pipe_read(Pipe *pi, u64 addr, int n)
{
    /* (Final) TODO BEGIN */
    acquire_spinlock(&pi->lock);
    while (pi->nread == pi->nwrite && pi->writeopen){
        if (thisproc()->killed) {
            release_spinlock(&pi->lock);
            return -1;
        }
        release_spinlock(&pi->lock);
        unalertable_wait_sem(&pi->rlock);
    }
    int i;
    for (i = 0; i < n; ++i) {
        if (pi->nread == pi->nwrite) {
            break;
        }
        *((char *)addr + i) = pi->data[++pi->nread % PIPE_SIZE];
    }
    post_sem(&pi->wlock);
    release_spinlock(&pi->lock);
    return i;
    /* (Final) TODO END */
}