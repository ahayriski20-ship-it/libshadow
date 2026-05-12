#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define MAX_HOOKS       16
#define TRAMPOLINE_SIZE 16

static uint8_t  g_pool[MAX_HOOKS][TRAMPOLINE_SIZE];
static int      g_pool_idx = 0;

static void thumb_patch(uintptr_t at, uintptr_t dest) {
    uint8_t* p = (uint8_t*)at;
    p[0]=0xDF; p[1]=0xF8; p[2]=0x00; p[3]=0xF0;
    *(uint32_t*)(p + 4) = (uint32_t)(dest & ~1u);
}

extern "C" int DobbyHook(void* addr, void* replace, void** orig) {
    if (g_pool_idx >= MAX_HOOKS) return -1;

    uintptr_t target  = (uintptr_t)addr;
    bool      isThumb = (target & 1u) != 0;
    uintptr_t real    = target & ~1u;

    long      pgsz = sysconf(_SC_PAGESIZE);
    uintptr_t page = real & ~(uintptr_t)(pgsz - 1);
    mprotect((void*)page, (size_t)pgsz * 2, PROT_READ | PROT_WRITE | PROT_EXEC);

    uint8_t*  tramp  = g_pool[g_pool_idx++];
    memcpy(tramp, (void*)real, 8);
    thumb_patch((uintptr_t)(tramp + 8), real + 8);

    uintptr_t tpage = (uintptr_t)tramp & ~(uintptr_t)(pgsz - 1);
    mprotect((void*)tpage, (size_t)pgsz, PROT_READ | PROT_WRITE | PROT_EXEC);
    __builtin___clear_cache((char*)tramp, (char*)tramp + TRAMPOLINE_SIZE);

    if (orig) *orig = (void*)((uintptr_t)tramp | (isThumb ? 1u : 0u));

    thumb_patch(real, (uintptr_t)replace);
    __builtin___clear_cache((char*)real, (char*)real + 8);

    mprotect((void*)page, (size_t)pgsz * 2, PROT_READ | PROT_EXEC);
    return 0;
}
