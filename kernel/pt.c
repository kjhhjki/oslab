#include <aarch64/intrinsic.h>
#include <common/string.h>
#include <kernel/mem.h>
#include <kernel/pt.h>
#include <kernel/paging.h>

void* new_page() 
{
    void *p = kalloc_page();
    memset(p, 0, PAGE_SIZE);
    return p;
}

PTEntriesPtr get_pte(struct pgdir *pgdir, u64 va, bool alloc)
{
    // TODO:
    // Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
    // If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or return NULL if false.
    // THIS ROUTINUE GETS THE PTE, NOT THE PAGE DESCRIBED BY PTE.
    PTEntriesPtr p0 = pgdir->pt, p1, p2, p3;
    if(p0 == NULL) {
        if(alloc == false) return NULL;
        pgdir->pt = p0 = new_page();
    }
    if(!(p0[VA_PART0(va)] & PTE_VALID)) {
        if(alloc == false) return NULL;
        p0[VA_PART0(va)] = K2P(new_page()) | PTE_TABLE;
    }
    p1 = (PTEntriesPtr)P2K(PTE_ADDRESS(p0[VA_PART0(va)]));
    if(!(p1[VA_PART1(va)] & PTE_VALID)) {
        if(alloc == false) return NULL;
        p1[VA_PART1(va)] = K2P(new_page()) | PTE_TABLE;
    }
    p2 = (PTEntriesPtr)P2K(PTE_ADDRESS(p1[VA_PART1(va)]));
    if(!(p2[VA_PART2(va)] & PTE_VALID)) {
        if(alloc == false) return NULL;
        p2[VA_PART2(va)] = K2P(new_page()) | PTE_TABLE;
    }
    p3 = (PTEntriesPtr)P2K(PTE_ADDRESS(p2[VA_PART2(va)]));
    return &p3[VA_PART3(va)];
}

void init_pgdir(struct pgdir *pgdir)
{
    pgdir->pt = kalloc_page();
    memset(pgdir->pt,0,PAGE_SIZE);
    init_spinlock(&pgdir->lock);
    init_list_node(&pgdir->section_head);
    init_sections(&pgdir->section_head);
}

void free_PT_dfs(PTEntriesPtr p, int d){
    if(d == 3 || p == NULL) {
        kfree_page(p);
        return;
    }
    for(int i = 0; i < N_PTE_PER_TABLE; ++i){
        if(p[i] != NULL) {
            free_PT_dfs((PTEntriesPtr)P2K(PTE_ADDRESS(p[i])), d + 1);
            p[i] = NULL;
        }
    }
    kfree_page(p);
}
void free_pgdir(struct pgdir *pgdir)
{
    // TODO:
    // Free pages used by the page table. If pgdir->pt=NULL, do nothing.
    // DONT FREE PAGES DESCRIBED BY THE PAGE TABLE
    if(!pgdir->pt) return;
    free_PT_dfs(pgdir->pt, 0);
    pgdir->pt = NULL;
}

void attach_pgdir(struct pgdir *pgdir)
{
    extern PTEntries invalid_pt;
    if (pgdir->pt)
        arch_set_ttbr0(K2P(pgdir->pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}

/**
 * Map virtual address 'va' to the physical address represented by kernel
 * address 'ka' in page directory 'pd', 'flags' is the flags for the page
 * table entry.
 */
void vmmap(struct pgdir *pd, u64 va, void *ka, u64 flags)
{
    /* (Final) TODO BEGIN */
    auto pte = get_pte(pd, va, true);
    *pte = K2P(ka) | flags;
    attach_pgdir(pd);
    arch_tlbi_vmalle1is();
    /* (Final) TODO END */
}

/*
 * Copy len bytes from p to user address va in page table pgdir.
 * Allocate physical pages if required.
 * Useful when pgdir is not the current page table.
 */
int copyout(struct pgdir *pd, void *va, void *p, usize len)
{
    /* (Final) TODO BEGIN */
    void *page;
    usize n, pgoff;
    u64 *pte;
    if ((usize)va + len > USERTOP) {
        return -1;
    }
    for (; len; len -= n, va += n) {
        pgoff = (usize)va % PAGE_SIZE;
        if ((pte = get_pte(pd, (u64)va, 1)) == NULL) {
            return -1;
        }
        if (*pte & PTE_VALID) {
            page = (void*)P2K(PTE_ADDRESS(*pte));
        } else {
            if ((page = kalloc_page()) == NULL) {
                return -1;
            }
            *pte = K2P(page) | PTE_USER_DATA;
        }
        n = MIN(PAGE_SIZE - pgoff, len);
        if (p) {
            memmove(page + pgoff, p, n);
            p += n;
        } else {
            memset(page + pgoff, 0, n);
        }
    }
    return 0;
    /* (Final) TODO END */
}

struct pgdir* vm_copy(struct pgdir *pgdir) {
    struct pgdir* newpgdir = kalloc(sizeof(struct pgdir));
    init_pgdir(newpgdir);
    if (!newpgdir) {
        return NULL;
    }
    for (int i = 0; i < N_PTE_PER_TABLE; ++i) {
        if (pgdir->pt[i] & PTE_VALID) {
            PTEntriesPtr pgt1 = (PTEntriesPtr)P2K(PTE_ADDRESS(pgdir->pt[i]));
            for (int i1 = 0; i1 < N_PTE_PER_TABLE; ++i1) {
                if (pgt1[i1] & PTE_VALID) {
                    PTEntriesPtr pgt2 = (PTEntriesPtr)P2K(PTE_ADDRESS(pgt1[i1]));
                    for (int i2 = 0; i2 < N_PTE_PER_TABLE; ++i2) {
                        if (pgt2[i2] & PTE_VALID) {
                            PTEntriesPtr pgt3 = (PTEntriesPtr)P2K(PTE_ADDRESS(pgt2[i2]));
                            for (int i3 = 0; i3 < N_PTE_PER_TABLE; ++i3) {
                                if (pgt3[i3] & PTE_VALID) {
                                    u64 va = (u64)i << (12 + 9 * 3) | (u64) i1 << (12 + 9 * 2) | 
                                             (u64)i2 << (12 + 9) | i3 << 12;
                                    u64 pa = PTE_ADDRESS(pgt3[i3]);
                                    void *np = kalloc_page();
                                    memmove(np, (void*)P2K(pa), PAGE_SIZE);
                                    auto pte = get_pte(newpgdir, va, true);
                                    *pte = K2P(np) | PTE_USER_DATA;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return newpgdir;
}

int uvm_alloc(struct pgdir *pgdir, u64 base, u64 stksz, u64 oldsz, u64 newsz) {
    base = base;
    for (u64 a= (oldsz + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE; a < newsz; a += PAGE_SIZE) {
        void *p = kalloc_page();
        *get_pte(pgdir, a, true) = K2P(p) | PTE_USER_DATA;
    }
    return newsz;
}