#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>

/**
    @brief the private reference to the super block.

    @note we need these two variables because we allow the caller to
            specify the block device and super block to use.
            Correspondingly, you should NEVER use global instance of
            them, e.g. `get_super_block`, `block_device`

    @see init_bcache
 */
static const SuperBlock *sblock;

/**
    @brief the reference to the underlying block device.
 */
static const BlockDevice *device; 

/**
    @brief global lock for block cache.

    Use it to protect anything you need.

    e.g. the list of allocated blocks, etc.
 */
static SpinLock lock;

static SpinLock loglock, listlock;
static SpinLock bitmaplock;
static ListNode head;
static LogHeader header;
static usize blocknum;

/**
    @brief the list of all allocated in-memory block.

    We use a linked list to manage all allocated cached blocks.

    You can implement your own data structure if you like better performance.

    @see Block
 */
static ListNode head;

static LogHeader header; // in-memory copy of log header block.

/**
    @brief a struct to maintain other logging states.
    
    You may wonder where we store some states, e.g.
    
    * how many atomic operations are running?
    * are we checkpointing?
    * how to notify `end_op` that a checkpoint is done?

    Put them here!

    @see cache_begin_op, cache_end_op, cache_sync
 */

static void movetohead(ListNode* p);
static void copyblockdata(usize fromblockno, usize toblockno);

struct {
    /* your fields here */
    bool iscommit;
    int outstanding;
    Semaphore logsem;
    Semaphore check;
} log;

// read the content from disk.
static INLINE void device_read(Block *block) {
    device->read(block->block_no, block->data);
}

// write the content back to disk.
static INLINE void device_write(Block *block) {
    device->write(block->block_no, block->data);
}

// read log header from disk.
static INLINE void read_header() {
    device->read(sblock->log_start, (u8 *)&header);
}

// write log header back to disk.
static INLINE void write_header() {
    device->write(sblock->log_start, (u8 *)&header);
}

// initialize a block struct.
static void init_block(Block *block) {
    block->block_no = 0;
    init_list_node(&block->node);
    block->acquired = false;
    block->pinned = false;

    init_sleeplock(&block->lock);
    block->valid = false;
    memset(block->data, 0, sizeof(block->data));
}

// see `cache.h`.
static usize get_num_cached_blocks() {
    // TODO
    return blocknum;
}

// see `cache.h`.
static Block *cache_acquire(usize block_no) {
    // TODO
    acquire_spinlock(&lock);
    Block* ans = NULL;
    _for_in_list(p, &head){
        if (p == &head) {
            break;
        }
        Block* now = container_of(p, Block, node);
        if(now->block_no == block_no) {
            ans = now;
            break;
        }
    }
    
    if(ans) {
        ans->acquired = 1;
        release_spinlock(&lock);
        bool f = wait_sem(&ans->lock);
        if(!f) {
            PANIC();
        }
        acquire_spinlock(&lock);
        movetohead(&ans->node);
        release_spinlock(&lock);
        return ans;
    }
    if(blocknum >= EVICTION_THRESHOLD) {
        ListNode *p = head.prev, *q;
        while(1) {
            if(p == &head || blocknum < EVICTION_THRESHOLD) {
                break;
            }
            q = p->prev;
            Block* now = container_of(p, Block, node);
            if(!now->acquired && !now->pinned) {
                detach_from_list(&listlock, p);
                --blocknum;
                kfree(now);
            }
            p = q;
        }
    }
    ans = kalloc(sizeof(Block));
    init_block(ans);
        
    bool f = wait_sem(&ans->lock);
    if(!f) {
        PANIC();
    }
    ++blocknum;
    ans->block_no = block_no;
    ans->acquired = 1;
    ans->valid = 1;
    release_spinlock(&lock);
    device_read(ans);
    acquire_spinlock(&lock);
    insert_into_list(&listlock, &head, &ans->node);
    release_spinlock(&lock);
    return ans;
}

// see `cache.h`.
static void cache_release(Block *block) {
    // TODO
    acquire_spinlock(&lock);
    block->acquired = 0;
    post_sem(&block->lock);
    release_spinlock(&lock);
}

// see `cache.h`.
void init_bcache(const SuperBlock *_sblock, const BlockDevice *_device) {
    sblock = _sblock;
    device = _device;

    // TODO
    init_spinlock(&lock);
    init_spinlock(&loglock);
    init_spinlock(&bitmaplock);
    init_spinlock(&listlock);
    init_list_node(&head);
    blocknum = 0;
    header.num_blocks = 0;
    log.outstanding = log.iscommit = 0;
    init_sem(&log.logsem, 0);
    init_sem(&log.check, 0);
    read_header();
    for(usize i = 0; i < header.num_blocks; ++i) {
        copyblockdata(sblock->log_start + i + 1, header.block_no[i]);
    }
    header.num_blocks = 0;
    memset(header.block_no, 0, LOG_MAX_SIZE);
    write_header();
}

// see `cache.h`.
static void cache_begin_op(OpContext *ctx) {
    // TODO
    acquire_spinlock(&loglock);
    ctx->rm = 0;
    while(log.iscommit || header.num_blocks + (log.outstanding + 1) * OP_MAX_NUM_BLOCKS > LOG_MAX_SIZE) {
        release_spinlock(&loglock);
        bool f = wait_sem(&log.logsem);
        if(!f) {
            PANIC();
        }
        acquire_spinlock(&loglock);
    }
    ++log.outstanding;
    release_spinlock(&loglock);
}

// see `cache.h`.
static void cache_sync(OpContext *ctx, Block *block) {
    // TODO
    if(ctx == NULL) {
        device_write(block);
        return;
    }
    acquire_spinlock(&loglock);
    block->pinned = 1;
    for (usize i = 0; i < header.num_blocks; ++i){
        if (block->block_no == header.block_no[i]){
            release_spinlock(&loglock);
            return;
        }
    }
    if(ctx->rm >= OP_MAX_NUM_BLOCKS || header.num_blocks >= LOG_MAX_SIZE) {
        PANIC();
    }
    header.block_no[header.num_blocks++] = block->block_no;
    ++ctx->rm;
    release_spinlock(&loglock);
}

// see `cache.h`.
static void cache_end_op(OpContext *ctx) {
    // TODO
    acquire_spinlock(&loglock);
    if(log.iscommit) {
        PANIC();
    }
    if(--log.outstanding > 0) {
        post_sem(&log.logsem);
        release_spinlock(&loglock);
        unalertable_wait_sem(&log.check);
        return;
    }
    log.iscommit = 1;
    for(int i = 0; i < (int)header.num_blocks; ++i) {
        release_spinlock(&loglock);
        copyblockdata(header.block_no[i], sblock->log_start + i + 1);
        acquire_spinlock(&loglock);
    }
    release_spinlock(&loglock);
    write_header();
    acquire_spinlock(&loglock);
    for(int i = 0; i < (int)header.num_blocks; ++i) {
        Block *now = cache_acquire(header.block_no[i]);
        cache_sync(NULL, now);
        now->pinned = 0;
        cache_release(now);
    }
    header.num_blocks = 0;
    release_spinlock(&loglock);
    write_header();
    acquire_spinlock(&loglock);
    log.iscommit = 0;
    post_all_sem(&log.logsem);
    post_all_sem(&log.check);
    release_spinlock(&loglock);
    return (void)ctx;
}

// see `cache.h`.
static usize cache_alloc(OpContext *ctx) {
    // TODO
    acquire_spinlock(&bitmaplock);
    for(int blockstart = 0; blockstart < (int)sblock->num_blocks; blockstart += BLOCK_SIZE * 8) {
        Block *mp = cache_acquire(sblock->bitmap_start + blockstart / (BLOCK_SIZE * 8));
        for(int add = 0; add < BLOCK_SIZE && blockstart + add * 8 < (int)sblock->num_blocks; ++add) {
            int temp=mp->data[add];
            for(int i = 0; i < 8 && blockstart + add * 8 + i < (int)sblock->num_blocks; ++i) {
                if((temp >> i & 1) == 0) {
                    mp->data[add] |= (1 << i);
                    cache_sync(ctx, mp);
                    cache_release(mp);
                    Block* ans = cache_acquire(blockstart + add * 8 + i);
                    memset(ans->data, 0, BLOCK_SIZE);
                    cache_sync(ctx, ans);
                    cache_release(ans);
                    release_spinlock(&bitmaplock);
                    return blockstart + add * 8 + i;
                }
            }
        }
        cache_release(mp);
    }
    release_spinlock(&bitmaplock);
    PANIC();
}

// see `cache.h`.
static void cache_free(OpContext *ctx, usize block_no) {
    // TODO
    acquire_spinlock(&bitmaplock);
    Block *mp = cache_acquire(sblock->bitmap_start + block_no / (8 * BLOCK_SIZE));
    int idx = block_no % (8 * BLOCK_SIZE);
    mp->data[idx / 8] -= (1 << (idx % 8));
    cache_sync(ctx, mp);
    cache_release(mp);
    release_spinlock(&bitmaplock);
}

BlockCache bcache = {
    .get_num_cached_blocks = get_num_cached_blocks,
    .acquire = cache_acquire,
    .release = cache_release,
    .begin_op = cache_begin_op,
    .sync = cache_sync,
    .end_op = cache_end_op,
    .alloc = cache_alloc,
    .free = cache_free,
};

static void movetohead(ListNode* p)
{
    detach_from_list(&listlock, p);
    insert_into_list(&listlock, &head, p);
}

static void copyblockdata(usize fromblockno, usize toblockno)
{
    Block *from = cache_acquire(fromblockno);
    Block *to = cache_acquire(toblockno);
    for(int j = 0; j < BLOCK_SIZE; ++j) {
        to->data[j] = from->data[j];
    }
    cache_sync(NULL, to);
    cache_release(from);
    cache_release(to);
}