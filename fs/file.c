#include "file.h"
#include <common/defines.h>
#include <common/spinlock.h>
#include <common/sem.h>
#include <fs/inode.h>
#include <common/list.h>
#include <kernel/mem.h>
#include <fs/pipe.h>

// the global file table.
static struct ftable ftable;

void init_ftable() {
    // TODO: initialize your ftable.
    init_spinlock(&ftable.lock);
}

void init_oftable(struct oftable *oftable) {
    // TODO: initialize your oftable for a new process.
    for (int i = 0; i < 16; ++i) {
        oftable->openfile[i] = NULL;
    }
}

/* Allocate a file structure. */
struct file* file_alloc() {
    /* (Final) TODO BEGIN */
    acquire_spinlock(&ftable.lock);
    for (int i = 0; i < NFILE; ++i) {
        if (ftable.filelist[i].ref == 0) {
            ftable.filelist[i].ref = 1;
            release_spinlock(&ftable.lock);
            return &(ftable.filelist[i]);
        }
    }
    release_spinlock(&ftable.lock);
    return NULL;
    /* (Final) TODO END */
}

/* Increment ref count for file f. */
struct file* file_dup(struct file* f) {
    /* (Final) TODO BEGIN */
    acquire_spinlock(&ftable.lock);
    ++f->ref;
    release_spinlock(&ftable.lock);
    /* (Final) TODO END */
    return f;
}

/* Close file f. (Decrement ref count, close when reaches 0.) */
void file_close(struct file* f) {
    /* (Final) TODO BEGIN */
    acquire_spinlock(&ftable.lock);
    if (--f->ref > 0) {
        release_spinlock(&ftable.lock);
        return;
    }
    auto tmp = *f;
    f->type = FD_NONE;
    release_spinlock(&ftable.lock);
    if (tmp.type == FD_INODE) {
        OpContext op;
        bcache.begin_op(&op);
        inodes.put(&op, tmp.ip);
        bcache.end_op(&op);
    } else if (tmp.type == FD_PIPE) {
        pipe_close(tmp.pipe, tmp.writable);
    }
    /* (Final) TODO END */
}

/* Get metadata about file f. */
int file_stat(struct file* f, struct stat* st) {
    /* (Final) TODO BEGIN */
    if (f->type == FD_INODE) {
        inodes.lock(f->ip);
        stati(f->ip, st);
        inodes.unlock(f->ip);
        return 0;
    }
    /* (Final) TODO END */
    return -1;
}

/* Read from file f. */
isize file_read(struct file* f, char* addr, isize n) {
    /* (Final) TODO BEGIN */
    if (f->readable == false) {
        return -1;
    } else if (f->type == FD_PIPE) {
        return pipe_read(f->pipe, (u64)addr, n);
    } else if (f->type == FD_INODE) {
        inodes.lock(f->ip);
        isize ans = inodes.read(f->ip, (u8*)addr, f->off, n);
        if (ans > 0) {
            f->off += ans;
        }
        inodes.unlock(f->ip);
        return ans;
    }
    /* (Final) TODO END */
    return 0;
}

/* Write to file f. */
isize file_write(struct file* f, char* addr, isize n) {
    /* (Final) TODO BEGIN */
    if (f->writable == false) {
        return -1;
    } else if (f->type == FD_PIPE) {
        return pipe_write(f->pipe, (u64)addr, n);
    } else if (f->type == FD_INODE) {
        isize max = (OP_MAX_NUM_BLOCKS - 4) / 2 * BLOCK_SIZE, cur = 0;
        while (cur < n) {
            isize cap = MIN(n - cur, max);
            OpContext op;
            bcache.begin_op(&op);
            inodes.lock(f->ip);
            isize len = inodes.write(&op, f->ip, (u8*)(addr + cur), f->off, cap);
            if (len > 0) {
                f->off += len;
            }
            inodes.unlock(f->ip);
            bcache.end_op(&op);
            if (len < 0) {
                break;
            }
            cur += len;
        }
        return cur == n? n: -1;
    }
    /* (Final) TODO END */
    return 0;
}