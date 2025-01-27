#include <elf.h>
#include <common/string.h>
#include <common/defines.h>
#include <kernel/console.h>
#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/pt.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <aarch64/trap.h>
#include <fs/file.h>
#include <fs/inode.h>

extern int fdalloc(struct file *f);

void handle(struct pgdir *c, struct pgdir *n, OpContext *op, Inode *ip)
{
    if (n) {
        free_pgdir(n);
    }
	if (ip) {
		inodes.unlock(ip);
		inodes.put(op, ip);
		bcache.end_op(op);
	}
	thisproc()->pgdir = *c;
}

#include <kernel/printk.h>
int execve(const char *path, char *const argv[], char *const envp[])
{
    /* (Final) TODO BEGIN */
    printk("execve\n");
    auto cur = thisproc();
    auto curp = cur->pgdir;
    struct pgdir *pgd = kalloc(sizeof(struct pgdir));
    init_pgdir(pgd);
    Inode *ip = NULL;
    OpContext op;
    if (pgd == NULL) {
        handle(&curp, pgd, &op, ip);
        return -1;
    }
    bcache.begin_op(&op);
    ip = namei(path, &op);
    if (ip == NULL) {
        bcache.end_op(&op);
        handle(&curp, pgd, &op, ip);
        return -1;
    }
    inodes.lock(ip);
	Elf64_Ehdr elf;
    if (inodes.read(ip, (u8*)&elf, 0, sizeof(elf)) != sizeof(elf)) {
		handle(&curp, pgd, &op, ip);
        return -1;
	} else if (elf.e_ident[EI_MAG0] != ELFMAG0 || elf.e_ident[EI_MAG1] != ELFMAG1 || 
               elf.e_ident[EI_MAG2] != ELFMAG2 || elf.e_ident[EI_MAG3] != ELFMAG3) {
        handle(&curp, pgd, &op, ip);
        return -1;
    } else if (elf.e_ident[EI_CLASS] != ELFCLASS64) {
        handle(&curp, pgd, &op, ip);
        return -1;
    }
    Elf64_Phdr ph;
    cur->pgdir = *pgd;
    u64 sz = 0, base = 0, stksz = 0, off = elf.e_phoff;
    int fst = 1;
    for (int i = 0; i < elf.e_phnum; ++i, off += sizeof(ph)) {
        if ((inodes.read(ip, (u8*)&ph, off, sizeof(ph))) != sizeof(ph)) {
			PANIC();
		} else if (ph.p_type != PT_LOAD) {
			continue;
		} else if (ph.p_memsz < ph.p_filesz) {
            PANIC();
        } else if (ph.p_vaddr + ph.p_memsz < ph.p_vaddr) {
            PANIC();
        }
        if (fst) {
            fst = 0;
            sz = base = ph.p_vaddr;
            if (base % PAGE_SIZE) {
                PANIC();
            }
        }
        if (!(sz = uvm_alloc(pgd, base, stksz, sz, ph.p_vaddr + ph.p_memsz))) {
            PANIC();
        }
        attach_pgdir(pgd);
        arch_tlbi_vmalle1is();
        if(inodes.read(ip, (u8*)ph.p_vaddr, ph.p_offset, ph.p_filesz) != ph.p_filesz) {
            PANIC();
        }
        memset((void*)ph.p_vaddr + ph.p_filesz, 0, ph.p_memsz - ph.p_filesz);
		arch_fence();
		arch_dccivac((void*)ph.p_vaddr, ph.p_memsz);
		arch_fence();
    }
    inodes.unlock(ip);
	inodes.put(&op, ip);
	bcache.end_op(&op);
	ip = NULL;
	attach_pgdir(&curp);
	arch_tlbi_vmalle1is();
	char *sp = (char*)USERTOP;
	int argc = 0, envc = 0;
    if (argv){
		for (; argc < 10 && argv[argc]; ++argc){
			usize len = strlen(argv[argc]) + 1;
			sp -= len;
			copyout(pgd, sp, argv[argc], len);
		}
	}
	if (envp) {
		for (; envc < 10 && envp[envc]; ++envc){
			usize len = strlen(envp[envc]) + 1;
			sp -= len;
			copyout(pgd, sp, envp[envc],len);
		}
	}
    void *newsp = (void*)(((usize)sp - (envc + argc + 4) * 8) / 16 * 16);
	copyout(pgd, newsp, NULL, (void*)sp - newsp);
	attach_pgdir(pgd);
	arch_tlbi_vmalle1is();
	u64 *newargv = newsp + 8, *newenvp = (void*)newargv + 8 * (argc + 1);
    for (int i = envc - 1; i >= 0; --i) {
		newenvp[i] = (uint64_t)sp;
		for(; *sp; ++sp);
		++sp;
	}
	for (int i = argc - 1; i >= 0; --i) {
		newargv[i] = (uint64_t)sp;
		for(; *sp; ++sp);
		++sp;
	}
    *(usize*)(newsp) = argc;
	sp = newsp;
	stksz = (USERTOP - (usize)sp + 10 * PAGE_SIZE - 1) / (10 * PAGE_SIZE) * (10 * PAGE_SIZE);
	copyout(pgd, (void *)(USERTOP - stksz), 0, stksz - (USERTOP - (usize)sp));
	cur->pgdir = *pgd;
	cur->ucontext->elr = elf.e_entry;
	cur->ucontext->sp = (uint64_t)sp;
	attach_pgdir(&cur->pgdir);
	arch_tlbi_vmalle1is();
	free_pgdir(&curp);
    return 0;
    /* (Final) TODO END */
}
