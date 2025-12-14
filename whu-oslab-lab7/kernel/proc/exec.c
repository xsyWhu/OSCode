#include "proc/proc.h"
#include "mem/vmem.h"
#include "lib/string.h"
#include "memlayout.h"

extern char _binary_init_bin_start[];
extern char _binary_init_bin_end[];

struct embedded_image {
    const char *path;
    const uint8 *start;
    const uint8 *end;
    uint64 entry;
};

static const struct embedded_image builtin_images[] = {
    { "/init", (const uint8*)_binary_init_bin_start, (const uint8*)_binary_init_bin_end, 0 },
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

int exec_process(struct proc *p, const char *path, const char *const argv[])
{
    if (!p || !path)
        return -1;

    const struct embedded_image *img = find_image(path);
    if (!img)
        return -1;

    pagetable_t pagetable = proc_pagetable(p);
    if (!pagetable)
        return -1;

    uint64 img_size = (uint64)(img->end - img->start);
    uint64 text_sz = PG_ROUND_UP(img_size);
    uint64 allocsz = 0;
    uint64 sz = uvmalloc(pagetable, 0, text_sz);
    if (sz == 0) {
        proc_freepagetable(pagetable, 0);
        return -1;
    }
    allocsz = sz;

    if (copyout(pagetable, 0, img->start, img_size) < 0) {
        proc_freepagetable(pagetable, allocsz);
        return -1;
    }

    uint64 stack_top = text_sz + PGSIZE;
    sz = uvmalloc(pagetable, sz, stack_top);
    if (sz == 0) {
        proc_freepagetable(pagetable, allocsz);
        return -1;
    }
    allocsz = sz;

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
    p->sz = sz;
    p->trapframe->epc = img->entry;
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
    proc_freepagetable(pagetable, allocsz);
    return -1;
}
