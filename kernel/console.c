#include <kernel/console.h>
#include <aarch64/intrinsic.h>
#include <kernel/sched.h>
#include <driver/uart.h>
#include <driver/interrupt.h>

struct console cons;
static SpinLock conslock;
static Semaphore conssem;

void console_init()
{
    /* (Final) TODO BEGIN */
    init_spinlock(&conslock);
    init_sem(&conssem, 0);
    /* (Final) TODO END */
}

static void console_putchar(int c){
    if (c == 0x100) {
        uart_put_char('\b');
        uart_put_char(' ');
        uart_put_char('\b');
    } else {
        uart_put_char(c);
    }
}

/**
 * console_write - write to uart from the console buffer.
 * @ip: the pointer to the inode
 * @buf: the buffer
 * @n: number of bytes to write
 */
isize console_write(Inode *ip, char *buf, isize n)
{
    /* (Final) TODO BEGIN */
    inodes.unlock(ip);
    acquire_spinlock(&conslock);
    for (isize i = 0; i < n; ++i){
        console_putchar(buf[i]);
    }
    release_spinlock(&conslock);
    inodes.lock(ip);
    return n;
    /* (Final) TODO END */
}

/**
 * console_read - read to the destination from the buffer
 * @ip: the pointer to the inode
 * @dst: the destination
 * @n: number of bytes to read
 */
static u64 rid, wid, eid;
static char buf[128];
isize console_read(Inode *ip, char *dst, isize n)
{
    /* (Final) TODO BEGIN */
    inodes.unlock(ip);
    acquire_spinlock(&conslock);
    isize m = n;
    while (n > 0) {
        while (rid == wid) {
            if (thisproc()->killed) {
                release_spinlock(&conslock);
                inodes.lock(ip);
                return -1;
            }
            release_spinlock(&conslock);
            unalertable_wait_sem(&conssem);
        }
        int c = buf[rid % 128];
        ++rid;
        if (c == C('D')) {
            if (n < m) {
                --rid;
            }
            break;
        }
        *dst++ = c;
        --n;
        if (c == '\n') {
            break;
        }
    }
    release_spinlock(&conslock);
    inodes.lock(ip);
    return m - n;
    /* (Final) TODO END */
}

void console_intr(char c)
{
    /* (Final) TODO BEGIN */
    if (c == C('C')) {
        ASSERT(kill(thisproc()->pid) == 0);
    } else if (c == C('U')) {
        while (eid != wid && buf[(eid - 1) % 128] != '\n'){
            eid--;
            console_putchar(0x100);
        }
    } else if (c=='\x7f') {
        if (eid!=wid){
            eid--;
            console_putchar(0x100);
        }
    } else if (c != 0 && eid - rid < 128) {
        c = (c=='\r') ? '\n' : c;
        buf[eid%128] = c;
        ++eid;
        console_putchar(c);
        if (c == '\n' || c == C('D') || eid == rid + 128){
            wid = eid;
            post_sem(&conssem);
        }
    }
    /* (Final) TODO END */
}