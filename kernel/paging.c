#include <aarch64/mmu.h>
#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <common/string.h>
#include <fs/block_device.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/pt.h>
#include <kernel/sched.h>

SpinLock listlock;
SleepLock swaplock;

void init_sections(ListNode *section_head) {
    /* (Final) TODO BEGIN */
    auto section_p = (struct section *)kalloc(sizeof(struct section));
    init_spinlock(&listlock);
    init_sleeplock(&swaplock);
    insert_into_list(&listlock, section_head, &section_p->stnode);
    section_p->begin = 0;
    section_p->end = 0;
    section_p->flags = (0 | ST_HEAP);
    /* (Final) TODO END */
}

void free_sections(struct pgdir *pd) {
    /* (Final) TODO BEGIN */
    
    /* (Final) TODO END */
}

u64 sbrk(i64 size) {
    /**
     * (Final) TODO BEGIN 
     * 
     * Increase the heap size of current process by `size`.
     * If `size` is negative, decrease heap size. `size` must
     * be a multiple of PAGE_SIZE.
     * 
     * Return the previous heap_end.
     */
    auto cur = thisproc();
	auto pgd = &cur->pgdir;
	auto sec = container_of(pgd->section_head.next, struct section, stnode);
	u64 ans = sec->end;
	sec->end += size * PAGE_SIZE;
	if (size < 0) {
		for (i64 i = 0; i < -size; ++i) {
			auto pte = get_pte(pgd, sec->end + i * PAGE_SIZE, false);
			if (pte && *pte) {
                kfree_page((void*)(P2K((*pte) & (-(1 << 12)))));
			    *pte = NULL;
            }
		}
	}
	attach_pgdir(pgd);
    arch_tlbi_vmalle1is();
	return ans;
    /* (Final) TODO END */
}

// void swap(struct pgdir *pd, struct section *st){
//     acquire_sleeplock(&swaplock);
//     u64 begin = st->begin, end = st->end;
//     for (u64 i = begin; i < end; i += PAGE_SIZE) {
//         auto pte = get_pte(pd, i, false);
//         if (pte && (*pte)) {
//             u32 bno = (*pte);
//             void *newpage = kalloc_page();
//             read_page_from_disk(newpage, (u32)bno);
//             *pte = K2P(newpage) | PTE_USER_DATA;
//             release_8_blocks(bno);
//         }
//     }
//     attach_pgdir(pd);
//     arch_tlbi_vmalle1is();
//     st->flags &= ~ST_SWAP;
//     release_sleeplock(&swaplock);
// }

int pgfault_handler(u64 iss) {
    Proc *p = thisproc();
    struct pgdir *pd = &p->pgdir;
    u64 addr =
            arch_get_far(); // Attempting to access this address caused the page fault

    /** 
     * (Final) TODO BEGIN
     * 
     * 1. Find the section struct which contains the faulting address `addr`.
     * 2. Check section flags to determine page fault type.
     * 3. Handle the page fault accordingly.
     * 4. Return to user code or kill the process.
     */
    struct section *sec = NULL;
    _for_in_list(p, &pd->section_head) {
        if (p == &pd->section_head) {
            continue;
        }
        sec = container_of(p, struct section, stnode);
        if (addr >= sec->begin) {
            break;
        }
    }
    auto pte = get_pte(pd, addr, true);
    if (*pte == NULL) {
        if (sec->flags & ST_SWAP) {
            // swap(pd, sec);
        } else {
            *pte = K2P(kalloc_page()) | PTE_USER_DATA;
        }
    } else if (*pte & PTE_RO) {
        auto p = kalloc_page();
        kfree_page((void*)P2K(PTE_ADDRESS(*pte)));
        memmove(p, (void*)P2K(PTE_ADDRESS(*pte)), PAGE_SIZE);
        *pte = K2P(p) | PTE_USER_DATA;
    } else if (!(*pte & PTE_VALID) && (sec->flags & ST_SWAP)) {
        // swap(pd, sec);
    }
    attach_pgdir(pd);
    arch_tlbi_vmalle1is();
    return iss;
    /* (Final) TODO END */
}

void copy_sections(ListNode *from_head, ListNode *to_head)
{
    /* (Final) TODO BEGIN */
    
    /* (Final) TODO END */
}
