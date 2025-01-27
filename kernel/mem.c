#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
#include <common/list.h>
#include <kernel/printk.h>
#include <common/string.h>

RefCount kalloc_page_cnt;

typedef struct MemoryBlock
{
    u32 nxt;
    u32 size;
} MemBlock;

MemBlock* get(u32 x) { return (MemBlock*)P2K(x); }
MemBlock* getv(void* x) { return (MemBlock*)x; }

void insert(MemBlock *h, MemBlock *n) 
{
    n->nxt = h->nxt;
    h->nxt = K2P(n);
}

const u64 offset = sizeof(MemBlock);
u64 maxpage;
void* const nullptr = (void*)P2K(NULL);
extern char end[], edata[];
void *top, *zero_page;

MemBlock mems;
SpinLock page_lock, alloc_lock;
MemBlock pages, *pt = &pages;

void kinit() {
    init_rc(&kalloc_page_cnt);
    init_spinlock(&alloc_lock);
    init_spinlock(&page_lock);
    pages.nxt = NULL;
    zero_page = end + (4096 - (((u64)end) & 4095));
    memset(zero_page, 0, PAGE_SIZE);
    top = zero_page + PAGE_SIZE;
    mems.nxt = NULL;
    maxpage = (P2K(PHYSTOP) - PAGE_BASE(top)) / PAGE_SIZE;
}

void* kalloc_page() {
    acquire_spinlock(&page_lock);
    increment_rc(&kalloc_page_cnt);
    MemBlock *p = get(pages.nxt);
    if(p == nullptr) {
        p = top;
        p->size = PAGE_SIZE;
        p->nxt = NULL;
        top += PAGE_SIZE;
    } else {
        pages.nxt = p->nxt;
    }
    release_spinlock(&page_lock);
    ASSERT((u64)p % PAGE_SIZE == 0);
    return (void*)(p);
}

void kfree_page(void* p) {
    acquire_spinlock(&page_lock);
    decrement_rc(&kalloc_page_cnt);
    // pt->nxt = (u64)p;
    // pt = (MemBlock*)P2K(p);
    // pt->nxt = NULL;
    // pt->size = PAGE_SIZE;
    // printk("free: %p\n", p);
    release_spinlock(&page_lock);
    return;
}

void* kalloc(unsigned long long sz) {
    acquire_spinlock(&alloc_lock);
    sz += offset + ((8 - (sz & 7)) & 7);
    MemBlock *p = get(mems.nxt), *lst = &mems;
    while(p != nullptr) {
        if(p->size >= sz) {
            break;
        }
        lst = p;
        p = get(p->nxt); 
    }
    if(p == nullptr) {
        p = (MemBlock*)(kalloc_page());
        p->nxt = NULL;
        p->size = PAGE_SIZE;
        insert(lst, p);
    }
    if(p->size > sz + offset) {
        MemBlock *p2 = p + sz / 8;
        p2->size = p->size - sz;
        p->size = sz;
        p2->nxt = p->nxt;
        lst->nxt = K2P(p2);
    } else {
        lst->nxt = p->nxt;
    }
    p->nxt = NULL;
    release_spinlock(&alloc_lock);
    return (void*)(p + 1);
}

void kfree(void* ptr) {
    acquire_spinlock(&alloc_lock);
    MemBlock *pt = (MemBlock*)(ptr);
    --pt;
    pt->nxt = NULL;
    if(mems.nxt == NULL) {
        insert(&mems, pt);
    } else {
        MemBlock *p = get(mems.nxt);
        while(p->nxt != NULL && get(p->nxt) < pt) {
            p = get(p->nxt);
        }
        if(p + p->size / 8 == pt) {
            p->size += pt->size;
            while(p != nullptr && p->nxt) {
                if(get(p->nxt) == p + p->size / 8) {
                    p->size += get(p->nxt)->size;
                    p->nxt = get(p->nxt)->nxt;
                } else {
                    p = get(p->nxt); 
                }
            }
        } else {
            insert(p, pt);
            p = pt;
            while(p != nullptr && p->nxt) {
                if(get(p->nxt) == p + p->size / 8) {
                    p->size += get(p->nxt)->size;
                    p->nxt = get(p->nxt)->nxt;
                } else {
                    p = get(p->nxt); 
                }
            }
        }        
    }
    release_spinlock(&alloc_lock);
}

void* get_zero_page() {
    return zero_page;
}

u64 left_page_cnt(){
    return maxpage - kalloc_page_cnt.count;
}
