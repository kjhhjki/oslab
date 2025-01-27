#include <common/string.h>
#include <fs/inode.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <kernel/console.h>
#include <sys/stat.h>

/**
    @brief the private reference to the super block.

    @note we need these two variables because we allow the caller to
            specify the block cache and super block to use.
            Correspondingly, you should NEVER use global instance of
            them.

    @see init_inodes
 */
static const SuperBlock* sblock;

/**
    @brief the reference to the underlying block cache.
 */
static const BlockCache* cache;

/**
    @brief global lock for inode layer.

    Use it to protect anything you need.

    e.g. the list of allocated blocks, ref counts, etc.
 */
static SpinLock lock, listlock;

/**
    @brief the list of all allocated in-memory inodes.

    We use a linked list to manage all allocated inodes.

    You can implement your own data structure if you want better performance.

    @see Inode
 */
static ListNode head;


// return which block `inode_no` lives on.
static INLINE usize to_block_no(usize inode_no) {
    return sblock->inode_start + (inode_no / (INODE_PER_BLOCK));
}

// return the pointer to on-disk inode.
static INLINE InodeEntry* get_entry(Block* block, usize inode_no) {
    return ((InodeEntry*)block->data) + (inode_no % INODE_PER_BLOCK);
}

// return address array in indirect block.
static INLINE u32* get_addrs(Block* block) {
    return ((IndirectBlock*)block->data)->addrs;
}

// initialize inode tree.
void init_inodes(const SuperBlock* _sblock, const BlockCache* _cache) {
    init_spinlock(&lock);
    init_spinlock(&listlock);
    init_list_node(&head);
    sblock = _sblock;
    cache = _cache;

    if (ROOT_INODE_NO < sblock->num_inodes)
        inodes.root = inodes.get(ROOT_INODE_NO);
    else
        printk("(warn) init_inodes: no root inode.\n");
}

// initialize in-memory inode.
static void init_inode(Inode* inode) {
    init_sleeplock(&inode->lock);
    init_rc(&inode->rc);
    init_list_node(&inode->node);
    inode->inode_no = 0;
    inode->valid = false;
}

// see `inode.h`.
static usize inode_alloc(OpContext* ctx, InodeType type) {
    ASSERT(type != INODE_INVALID);

    // TODO
    for(usize i = 1; i < sblock->num_inodes; ++i) {
        usize bno = to_block_no(i);
        Block *cur = cache->acquire(bno);
        InodeEntry *entry = get_entry(cur, i);
        if(entry->type == INODE_INVALID) {
            memset(entry, 0, sizeof(InodeEntry));
            entry->type = type;
            cache->sync(ctx, cur);
            cache->release(cur);
            return i;
        }
        cache->release(cur);
    }
    return 0;
}

// see `inode.h`.
static void inode_lock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
    unalertable_wait_sem(&inode->lock);
}

// see `inode.h`.
static void inode_unlock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
    post_sem(&inode->lock);
}

// see `inode.h`.
static void inode_sync(OpContext* ctx, Inode* inode, bool do_write) {
    // TODO
    if(inode->valid && do_write) {
        usize bno = to_block_no(inode->inode_no);
        Block *cur = cache->acquire(bno);
        memcpy(get_entry(cur, inode->inode_no), &inode->entry, sizeof(InodeEntry));
        cache->sync(ctx, cur);
        cache->release(cur);
    }
    if(!inode->valid) {
        usize bno = to_block_no(inode->inode_no);
        Block *cur = cache->acquire(bno);
        memcpy(&inode->entry, get_entry(cur, inode->inode_no), sizeof(InodeEntry));
        inode->valid = true;
        cache->release(cur);
    }
}

// see `inode.h`.
static Inode* inode_get(usize inode_no) {
    ASSERT(inode_no > 0);
    if(inode_no >= sblock->num_inodes) {
        printk("%llu %u\n", inode_no, sblock->num_inodes);
    }
    ASSERT(inode_no < sblock->num_inodes);
    acquire_spinlock(&lock);
    // TODO
    _for_in_list(p, &head) {
        if(p == &head) {
            continue;
        }
        auto cur = container_of(p, Inode, node);
        if(cur->inode_no == inode_no) {
            increment_rc(&cur->rc);
            release_spinlock(&lock);
            return cur;
        }
    }
    Inode *cur = kalloc(sizeof(Inode));
    init_inode(cur);
    cur->inode_no = inode_no;
    increment_rc(&cur->rc);
    inode_lock(cur);
    inode_sync(NULL, cur, false);
    inode_unlock(cur);
    insert_into_list(&listlock, &head, &cur->node);
    release_spinlock(&lock);
    return cur;
}
// see `inode.h`.
static void inode_clear(OpContext* ctx, Inode* inode) {
    // TODO
    if(inode->entry.indirect != 0) {
        Block *inb = cache->acquire(inode->entry.indirect);
        u32 *addrs = get_addrs(inb);
        for(int i = 0; i < (int)INODE_NUM_INDIRECT; ++i) {
            if(addrs[i]) {
                cache->free(ctx, addrs[i]);
            }
        }
        cache->release(inb);
        cache->free(ctx, inode->entry.indirect);
        inode->entry.indirect = 0;
    }
    for(int i = 0; i < (int)INODE_NUM_DIRECT; ++i) {
        if(inode->entry.addrs[i]) {
            cache->free(ctx, inode->entry.addrs[i]);
            inode->entry.addrs[i] = 0;
        }
    }
    inode->entry.num_bytes = 0;
    inode_sync(ctx, inode, true);
}

// see `inode.h`.
static Inode* inode_share(Inode* inode) {
    // TODO
    increment_rc(&inode->rc);
    return inode;
}

// see `inode.h`.
static void inode_put(OpContext* ctx, Inode* inode) {
    // TODO
    unalertable_wait_sem(&inode->lock);
    decrement_rc(&inode->rc);
    if(inode->rc.count == 0 && inode->entry.num_links == 0) {
        inode->entry.type = INODE_INVALID;
        inode_clear(ctx, inode);
        inode_sync(ctx, inode, true);
        acquire_spinlock(&lock);
        detach_from_list(&listlock, &inode->node);
        release_spinlock(&lock);
        post_sem(&inode->lock);
        kfree(inode);
    } else {
        post_sem(&inode->lock);
    }
}

/**
    @brief get which block is the offset of the inode in.

    e.g. `inode_map(ctx, my_inode, 1234, &modified)` will return the block_no
    of the block that contains the 1234th byte of the file
    represented by `my_inode`.

    If a block has not been allocated for that byte, `inode_map` will
    allocate a new block and update `my_inode`, at which time, `modified`
    will be set to true.

    HOWEVER, if `ctx == NULL`, `inode_map` will NOT try to allocate any new block,
    and when it finds that the block has not been allocated, it will return 0.
    
    @param[out] modified true if some new block is allocated and `inode`
    has been changed.

    @return usize the block number of that block, or 0 if `ctx == NULL` and
    the required block has not been allocated.

    @note the caller must hold the lock of `inode`.
 */
static usize inode_map(OpContext* ctx,
                       Inode* inode,
                       usize offset,
                       bool* modified) {
    // TODO
    if(offset < INODE_NUM_DIRECT) {
        if(inode->entry.addrs[offset] == 0) {
            *modified = true;
            inode->entry.addrs[offset] = cache->alloc(ctx);
            inode_sync(ctx, inode, true);
        }
        return inode->entry.addrs[offset];
    }
    offset -= INODE_NUM_DIRECT;
    if(inode->entry.indirect == false) {
        inode->entry.indirect = cache->alloc(ctx);
        inode_sync(ctx, inode, true);
    }
    Block *inb = cache->acquire(inode->entry.indirect);
    u32 *addrs = get_addrs(inb);
    if(addrs[offset] == 0) {
        addrs[offset] = cache->alloc(ctx);
        cache->sync(ctx, inb);
        *modified = true;
    }
    usize ans = addrs[offset];
    cache->release(inb);
    return ans;
}

static int memcmp2(const char *s1,const char *s2){
    return memcmp(s1,s2,MAX(strlen(s1),strlen(s2)));
}

// see `inode.h`.
static usize inode_read(Inode* inode, u8* dest, usize offset, usize count) {
    InodeEntry* entry = &inode->entry;
    if (inode->entry.type == INODE_DEVICE) {
        return console_read(inode, (char*)dest, count);
    }
    if (count + offset > entry->num_bytes)
        count = entry->num_bytes - offset;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= entry->num_bytes);
    ASSERT(offset <= end);

    // TODO
    for(usize i = offset; i < end; i = (i / BLOCK_SIZE + 1) * BLOCK_SIZE){
        bool useless = false;
        usize bno = inode_map(NULL, inode, i / BLOCK_SIZE, &useless);
        Block *cur = cache->acquire(bno);
        usize len = MIN(BLOCK_SIZE - i % BLOCK_SIZE, end - i);
        memcpy(dest, cur->data + i % BLOCK_SIZE, len);
        dest += len;
        cache->release(cur);
    }
    return count;
}

// see `inode.h`.
static usize inode_write(OpContext* ctx,
                         Inode* inode,
                         u8* src,
                         usize offset,
                         usize count) {
    InodeEntry* entry = &inode->entry;
    usize end = offset + count;
    if(inode->entry.type == INODE_DEVICE) {
        return console_write(inode, (char*)src, count);
    }
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= INODE_MAX_BYTES);
    ASSERT(offset <= end);

    // TODO
    if(entry->num_bytes < end){
        entry->num_bytes = end;
        inode_sync(ctx, inode, true);
    }
    for(usize i = offset; i < end; i = (i / BLOCK_SIZE + 1) * BLOCK_SIZE) {
        bool useless = false;
        usize bno = inode_map(ctx, inode, i / BLOCK_SIZE, &useless);
        Block *cur = cache->acquire(bno);
        usize len = MIN(BLOCK_SIZE - i % BLOCK_SIZE, end - i);
        memcpy(cur->data + i % BLOCK_SIZE, src, len);
        cache->sync(ctx, cur);
        cache->release(cur);
        src += len;
    }
    return count;
}

// see `inode.h`.
static usize inode_lookup(Inode* inode, const char* name, usize* index) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    DirEntry cur;
    for(u32 i = 0; i < entry->num_bytes; i += sizeof(cur)) {
        inode_read(inode, (u8*)&cur, i, sizeof(cur));
        if(cur.inode_no && memcmp2(name, cur.name) == 0) {
            if(index) {
                *index = i;
            }
            return cur.inode_no;
        }
    }
    return 0;
}

static usize inode_lookup2(Inode* inode, const char* name, usize* index) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    DirEntry cur;
    if(index) {
        *index = INODE_MAX_BYTES;
    }
    for(u32 i = 0; i < entry->num_bytes; i += sizeof(cur)) {
        inode_read(inode, (u8*)&cur, i, sizeof(cur));
        if(index && cur.inode_no == 0 && *index == INODE_MAX_BYTES) {
            *index = i;
        }
        if(cur.inode_no && memcmp2(name, cur.name) == 0) {
            if(index) {
                *index = i;
            }
            return cur.inode_no;
        }
    }
    if(index && *index == INODE_MAX_BYTES) {
        *index = entry->num_bytes;
    }
    return 0;
}

// see `inode.h`.
static usize inode_insert(OpContext* ctx,
                          Inode* inode,
                          const char* name,
                          usize inode_no) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    usize offset = 0;
    if(inode_lookup2(inode, name, &offset)) {
        return -1;
    }
    DirEntry now;
    strncpy(now.name, name, FILE_NAME_MAX_LENGTH);
    now.inode_no = inode_no;
    inode_write(ctx, inode, (u8*)&now, offset, sizeof(now));
    return offset;
}

// see `inode.h`.
static void inode_remove(OpContext* ctx, Inode* inode, usize index) {
    // TODO
    DirEntry cur;
    inode_read(inode, (u8*)&cur, index, sizeof(cur));
    cur.inode_no = 0;
    inode_write(ctx, inode, (u8*)&cur, index, sizeof(cur));
}

InodeTree inodes = {
    .alloc = inode_alloc,
    .lock = inode_lock,
    .unlock = inode_unlock,
    .sync = inode_sync,
    .get = inode_get,
    .clear = inode_clear,
    .share = inode_share,
    .put = inode_put,
    .read = inode_read,
    .write = inode_write,
    .lookup = inode_lookup,
    .insert = inode_insert,
    .remove = inode_remove,
};

/**
    @brief read the next path element from `path` into `name`.
    
    @param[out] name next path element.

    @return const char* a pointer offseted in `path`, without leading `/`. If no
    name to remove, return NULL.

    @example 
    skipelem("a/bb/c", name) = "bb/c", setting name = "a",
    skipelem("///a//bb", name) = "bb", setting name = "a",
    skipelem("a", name) = "", setting name = "a",
    skipelem("", name) = skipelem("////", name) = NULL, not setting name.
 */
static const char* skipelem(const char* path, char* name) {
    const char* s;
    int len;

    while (*path == '/')
        path++;
    if (*path == 0)
        return 0;
    s = path;
    while (*path != '/' && *path != 0)
        path++;
    len = path - s;
    if (len >= FILE_NAME_MAX_LENGTH)
        memmove(name, s, FILE_NAME_MAX_LENGTH);
    else {
        memmove(name, s, len);
        name[len] = 0;
    }
    while (*path == '/')
        path++;
    return path;
}

/**
    @brief look up and return the inode for `path`.

    If `nameiparent`, return the inode for the parent and copy the final
    path element into `name`.
    
    @param path a relative or absolute path. If `path` is relative, it is
    relative to the current working directory of the process.

    @param[out] name the final path element if `nameiparent` is true.

    @return Inode* the inode for `path` (or its parent if `nameiparent` is true), 
    or NULL if such inode does not exist.

    @example
    namex("/a/b", false, name) = inode of b,
    namex("/a/b", true, name) = inode of a, setting name = "b",
    namex("/", true, name) = NULL (because "/" has no parent!)
 */
static Inode* namex(const char* path,
                    bool nameiparent,
                    char* name,
                    OpContext* ctx) {
    /* (Final) TODO BEGIN */
    Inode *ans;
    if (*path == '/') {
        ans = inode_get(ROOT_INODE_NO);
    } else {
        ans = inode_share(thisproc()->cwd);
    }
    while ((path = skipelem(path, name)) != NULL) {
        inode_lock(ans);
        if (ans->entry.type != INODE_DIRECTORY) {
            inode_unlock(ans);
            inode_put(ctx, ans);
            return NULL;
        } else if (nameiparent && *path == '\0') {
            inode_unlock(ans);
            return ans;
        }
        auto nxt = inode_get(inode_lookup(ans, name, 0));
        if(nxt == NULL) {
            inode_unlock(ans);
            inode_put(ctx, ans);
            return NULL;
        }
        inode_unlock(ans);
        inode_put(ctx, ans);
        ans = nxt;
    }
    if (nameiparent) {
        inode_put(ctx, ans);
        return NULL;
    }
    return ans;
    /* (Final) TODO END */
}

Inode* namei(const char* path, OpContext* ctx) {
    char name[FILE_NAME_MAX_LENGTH];
    return namex(path, false, name, ctx);
}

Inode* nameiparent(const char* path, char* name, OpContext* ctx) {
    return namex(path, true, name, ctx);
}

/**
    @brief get the stat information of `ip` into `st`.
    
    @note the caller must hold the lock of `ip`.
 */
void stati(Inode* ip, struct stat* st) {
    st->st_dev = 1;
    st->st_ino = ip->inode_no;
    st->st_nlink = ip->entry.num_links;
    st->st_size = ip->entry.num_bytes;
    switch (ip->entry.type) {
        case INODE_REGULAR:
            st->st_mode = S_IFREG;
            break;
        case INODE_DIRECTORY:
            st->st_mode = S_IFDIR;
            break;
        case INODE_DEVICE:
            st->st_mode = 0;
            break;
        default:
            PANIC();
    }
}