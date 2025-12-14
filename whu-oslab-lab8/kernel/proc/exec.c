#include "proc/proc.h"
#include "mem/vmem.h"
#include "mem/pmem.h"
#include "lib/string.h"
#include "memlayout.h"
#include "elf.h"

extern char _binary_init_elf_start[];
extern char _binary_init_elf_end[];
extern char _binary_logread_elf_start[];
extern char _binary_logread_elf_end[];
extern char _binary_nice_elf_start[];
extern char _binary_nice_elf_end[];
extern char _binary_elfdemo_elf_start[];
extern char _binary_elfdemo_elf_end[];
extern char _binary_msgdemo_elf_start[];
extern char _binary_msgdemo_elf_end[];
extern char _binary_cowtest_elf_start[];
extern char _binary_cowtest_elf_end[];
extern char _binary_rtdemo_elf_start[];
extern char _binary_rtdemo_elf_end[];

struct embedded_image {
    const char *path;
    const uint8 *start;
    const uint8 *end;
    uint64 entry;
};

static const struct embedded_image builtin_images[] = {
    { "/init", (const uint8*)_binary_init_elf_start, (const uint8*)_binary_init_elf_end, 0 },
    { "/logread", (const uint8*)_binary_logread_elf_start, (const uint8*)_binary_logread_elf_end, 0 },
    { "/nice", (const uint8*)_binary_nice_elf_start, (const uint8*)_binary_nice_elf_end, 0 },
    { "/elfdemo", (const uint8*)_binary_elfdemo_elf_start, (const uint8*)_binary_elfdemo_elf_end, 0 },
    { "/msgdemo", (const uint8*)_binary_msgdemo_elf_start, (const uint8*)_binary_msgdemo_elf_end, 0 },
    { "/cowtest", (const uint8*)_binary_cowtest_elf_start, (const uint8*)_binary_cowtest_elf_end, 0 },
    { "/rtdemo", (const uint8*)_binary_rtdemo_elf_start, (const uint8*)_binary_rtdemo_elf_end, 0 },
};

static int path_equals(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static const struct embedded_image* find_image(const char *path)
{
    for (uint32 i = 0; i < sizeof(builtin_images) / sizeof(builtin_images[0]); i++) {
        if (path_equals(path, builtin_images[i].path)) {
            return &builtin_images[i];
        }
    }
    return NULL;
}

static int loadseg(pagetable_t pagetable, uint64 va, const uint8 *src, uint64 filesz)
{
    for (uint64 i = 0; i < filesz; i += PGSIZE) {
        uint64 pa = vm_walkaddr(pagetable, va + i);
        if (pa == 0) {
            return -1;
        }
        uint64 n = filesz - i;
        if (n > PGSIZE) {
            n = PGSIZE;
        }
        memmove((void*)pa, src + i, n);
    }
    return 0;
}

static int map_segment(pagetable_t pagetable, uint64 va, uint64 sz, int perm)
{
    uint64 a = PG_ROUND_DOWN(va);
    uint64 last = PG_ROUND_UP(va + sz);
    for (; a < last; a += PGSIZE) {
        void *mem = pmem_alloc(false);
        if (mem == NULL) {
            return -1;
        }
        memset(mem, 0, PGSIZE);
        vm_mappages(pagetable, a, (uint64)mem, PGSIZE, perm | PTE_U);
    }
    return 0;
}

int exec_process(struct proc *p, const char *path, const char *const argv[])
{
    if (!p || !path)
        return -1;

    const struct embedded_image *img = find_image(path);
    if (!img)
        return -1;

    uint64 img_size = (uint64)(img->end - img->start);
    if (img_size < sizeof(struct elfhdr)) {
        return -1;
    }

    struct elfhdr elf;
    memmove(&elf, img->start, sizeof(elf));
    if (elf.magic != ELF_MAGIC) {
        return -1;
    }
    if (elf.phentsize != sizeof(struct proghdr)) {
        return -1;
    }
    if (elf.phoff + (uint64)elf.phnum * sizeof(struct proghdr) > img_size) {
        return -1;
    }

    pagetable_t pagetable = proc_pagetable(p);
    if (!pagetable)
        return -1;

    uint64 sz = 0;
    uint64 mapped_sz = 0;
    const struct proghdr *ph = (const struct proghdr*)(img->start + elf.phoff);
    for (uint16 i = 0; i < elf.phnum; i++, ph++) {
        if (ph->type != ELF_PROG_LOAD) {
            continue;
        }
        if (ph->memsz < ph->filesz) {
            goto bad;
        }
        if (ph->vaddr + ph->memsz < ph->vaddr || ph->vaddr + ph->memsz >= TRAPFRAME) {
            goto bad;
        }
        if (ph->off + ph->filesz > img_size) {
            goto bad;
        }
        int perm = PTE_U;
        if (ph->flags & ELF_PROG_FLAG_READ) perm |= PTE_R;
        if (ph->flags & ELF_PROG_FLAG_WRITE) perm |= PTE_W;
        if (ph->flags & ELF_PROG_FLAG_EXEC) perm |= PTE_X;
        if (map_segment(pagetable, ph->vaddr, ph->memsz, perm) < 0) {
            goto bad;
        }
        if (loadseg(pagetable, ph->vaddr, img->start + ph->off, ph->filesz) < 0) {
            goto bad;
        }
        uint64 end = ph->vaddr + ph->memsz;
        if (end > sz) {
            sz = end;
        }
        if (sz > mapped_sz) {
            mapped_sz = sz;
        }
    }

    sz = PG_ROUND_UP(sz);
    mapped_sz = sz;
    uint64 stack_top = sz + PGSIZE;
    if (map_segment(pagetable, sz, PGSIZE, PTE_R | PTE_W) < 0) {
        goto bad;
    }
    mapped_sz = stack_top;

    uint64 sp = stack_top;
    uint64 stackbase = stack_top - PGSIZE;
    uint64 ustack[EXEC_MAXARG + 1];
    int argc = 0;

    if (argv) {
        while (argv[argc]) {
            if (argc >= EXEC_MAXARG) {
                goto bad;
            }
            uint64 len = (uint64)strlen(argv[argc]) + 1;
            sp -= len;
            sp &= ~0xFULL;
            if (sp < stackbase) {
                goto bad;
            }
            if (copyout(pagetable, sp, argv[argc], len) < 0) {
                goto bad;
            }
            ustack[argc] = sp;
            argc++;
        }
    }
    ustack[argc] = 0;

    sp -= (uint64)(argc + 1) * sizeof(uint64);
    sp &= ~0xFULL;
    if (sp < stackbase) {
        goto bad;
    }
    if (copyout(pagetable, sp, ustack, (uint64)(argc + 1) * sizeof(uint64)) < 0) {
        goto bad;
    }

    pagetable_t old = p->pagetable;
    uint64 oldsz = p->sz;

    memset(p->trapframe, 0, sizeof(*p->trapframe));
    p->pagetable = pagetable;
    p->sz = stack_top;
    p->trapframe->epc = elf.entry;
    p->trapframe->sp = sp;
    p->trapframe->a0 = argc;
    p->trapframe->a1 = sp;

    const char *last = img->path;
    for (const char *s = img->path; s && *s; s++) {
        if (*s == '/')
            last = s + 1;
    }
    safestrcpy(p->name, last, sizeof(p->name));

    if (old) {
        proc_freepagetable(old, oldsz);
    }
    return argc;

bad:
    proc_freepagetable(pagetable, mapped_sz);
    return -1;
}
