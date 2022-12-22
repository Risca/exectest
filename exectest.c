#include <errno.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define ALIGN_MEM(x, a)         (void*)ALIGN((intptr_t)(x), a)
#define ALIGN_MEM_DOWN(x, a)    (void*)ALIGN_DOWN((intptr_t)(x), a)
#define ALIGN(x,a)              __ALIGN_MASK(x, (typeof(x))(a)-1)
#define ALIGN_DOWN(x, a)        __ALIGN_MASK(x - ((a) - 1), (typeof(x))(a)-1)
#define __ALIGN_MASK(x,mask)    (((x)+(mask))&~(mask))

typedef void (*hello_function_t)(void);
typedef void (*trampoline_function_t)(hello_function_t);

static void hello_function(void)
{
    puts("Hello world!");
}

__attribute__((section("trampoline_function_section")))
static void trampoline(hello_function_t hello)
{
    hello();
}

extern const intptr_t __start_trampoline_function_section;
extern const intptr_t __stop_trampoline_function_section;

static const intptr_t trampoline_function_start = (intptr_t)&__start_trampoline_function_section;
static const intptr_t trampoline_function_stop = (intptr_t)&__stop_trampoline_function_section;

static uint8_t bss_buf[4096]; // should be enough

enum MemoryType {
    MEM_TYPE_STATIC,
    MEM_TYPE_DYNAMIC,
    MEM_TYPE_MAPPED,
};
struct memory {
    enum MemoryType type;
    void *buf;
};

static void print_usage(FILE* out, const char* executable)
{
    fprintf(out, "%s <stack|heap|freed_heap|bss|mmap|memfd>\n", executable);
}

int main(int argc, char* argv[])
{
    const long page_size = sysconf(_SC_PAGESIZE);
    const size_t trampoline_function_size = trampoline_function_stop - trampoline_function_start;
    const size_t aligned_size = ALIGN(trampoline_function_size, page_size);
    uint8_t stack_buf[page_size + trampoline_function_size];
    trampoline_function_t tramp;
    struct memory mem;

    if (argc != 2) {
        print_usage(stderr, argv[0]);
        return -EINVAL;
    }
    else if (!strcmp(argv[1], "stack")) {
        mem.type = MEM_TYPE_STATIC;
        mem.buf = ALIGN_MEM(stack_buf, page_size);
    }
    else if (!strcmp(argv[1], "heap")) {
        mem.type = MEM_TYPE_DYNAMIC;
        mem.buf = memalign(page_size, aligned_size);
    }
    else if (!strcmp(argv[1], "freed_heap")) {
        mem.type = MEM_TYPE_STATIC;
        mem.buf = memalign(page_size, aligned_size);
        free(mem.buf);
    }
    else if (!strcmp(argv[1], "bss")) {
        mem.type = MEM_TYPE_STATIC;
        mem.buf = ALIGN_MEM(bss_buf, page_size);
    }
    else if (!strcmp(argv[1], "mmap")) {
        mem.type = MEM_TYPE_MAPPED;
        mem.buf = mmap(NULL, trampoline_function_size, PROT_EXEC|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        memcpy(mem.buf, (void*)trampoline, trampoline_function_size);
    }
    else if (!strcmp(argv[1], "memfd")) {
        int fd = memfd_create("trampoline", 0);
        if (fd < 0) {
            perror("memfd_create");
            return -1;
        }
        if (write(fd, (void*)trampoline, trampoline_function_size) != trampoline_function_size) {
            perror("write");
            close(fd);
            return -1;
        }
        mem.buf = mmap(NULL, trampoline_function_size, PROT_EXEC, MAP_PRIVATE, fd, 0);
        mem.type = MEM_TYPE_MAPPED;
        close(fd);
    }
    else {
        print_usage(stderr, argv[0]);
        return -EINVAL;
    }

    tramp = (trampoline_function_t)mem.buf;

    if (mem.type != MEM_TYPE_MAPPED) {
        memcpy(mem.buf, (void*)trampoline, trampoline_function_size);
        if (mprotect(ALIGN_MEM_DOWN(mem.buf, page_size), aligned_size, PROT_EXEC))
            perror("mprotect(PROT_EXEC)");
    }

    tramp(hello_function);

    if (mem.type == MEM_TYPE_MAPPED)
        munmap(mem.buf, trampoline_function_size);
    else
        if (mprotect(ALIGN_MEM_DOWN(mem.buf, page_size), aligned_size, PROT_READ|PROT_WRITE))
            perror("mprotect(PROT_READ|PROT_WRITE)");

    if (mem.type == MEM_TYPE_DYNAMIC)
        free(mem.buf);

    return 0;
}
