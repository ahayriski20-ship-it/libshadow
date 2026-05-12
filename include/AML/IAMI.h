#pragma once
#include <stdint.h>
struct IAMI {
    virtual uintptr_t GetLibraryAddress(const char* lib) = 0;
    virtual void PlaceHook(uintptr_t addr, void* func, void** orig) = 0;
};
