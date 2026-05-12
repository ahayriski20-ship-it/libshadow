#include <stdint.h>
#include <dlfcn.h>
#include <android/log.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define LOG_TAG  "libshadow"
#define LOGFILE  "/storage/emulated/0/shadow_log.txt"
#define EXPORT   __attribute__((visibility("default")))

static void logf(const char* fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    FILE* f = fopen(LOGFILE, "a");
    if (f) { fprintf(f, "%s\n", buf); fclose(f); }
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "%s", buf);
}

// ─── Tipe fungsi shadow ───────────────────────────────
typedef void (*RenderExtraPlayerShadows_t)(void);
typedef void (*RenderStoredShadows_t)(int);
typedef void (*StoreRealTimeShadow_t)(void*, float,float,float,float,float,float);
typedef int  (*DoShadowThisFrame_t)(void*, void*);
typedef void (*RTShadowUpdate_t)(void*);
typedef void (*RTShadowManagerUpdate_t)(void*);

// ─── Pointer original ────────────────────────────────
static RenderExtraPlayerShadows_t orig_RenderExtraPlayer = nullptr;
static RenderStoredShadows_t      orig_RenderStored      = nullptr;
static DoShadowThisFrame_t        orig_DoShadow          = nullptr;

// ─── Offsets dari libGTASA.so ────────────────────────
#define OFF_RenderExtraPlayerShadows  0x5BDAC4
#define OFF_RenderStoredShadows       0x5BA720
#define OFF_RenderStaticShadows       0x5BB2E4
#define OFF_StoreRealTimeShadow       0x5BA020
#define OFF_DoShadowThisFrame         0x5B86B4
#define OFF_RTShadowManager_Update    0x5B83FC
#define OFF_RTShadowManager_Init      0x5B8220
#define OFF_gpShadowPedTex            0x00A48244
#define OFF_g_realTimeShadowMan       0x00A4816C
#define OFF_bRenderShadows            0x00A46D3C
#define OFF_ShadowsStoredToBeRendered 0x00A48270
#define OFF_MAX_DISTANCE_PED_SHADOWS  0x00A53530
#define OFF_UseAdvancedShadows        0x5B94EC

// ─── DobbyHook (dari hook.cpp) ───────────────────────
extern "C" {
    int DobbyHook(void* addr, void* replace, void** orig);
}

static uintptr_t gBase = 0;

// ─── getBase via /proc/self/maps (paling reliable) ───
static uintptr_t getBase() {
    if (gBase) return gBase;

    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) {
        logf("[SHADOW] ERROR: cannot open /proc/self/maps");
        return 0;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "libGTASA.so") && strstr(line, "r-xp")) {
            uintptr_t start = 0;
            sscanf(line, "%lx-", (unsigned long*)&start);
            if (start != 0) {
                gBase = start;
                logf("[SHADOW] getBase via maps = 0x%08X", (unsigned)gBase);
                break;
            }
        }
    }
    fclose(f);

    if (!gBase)
        logf("[SHADOW] ERROR: libGTASA.so tidak ditemukan di maps");

    return gBase;
}

// ─── Validasi base address ────────────────────────────
static bool isValidBase(uintptr_t base) {
    // Harus page-aligned (kelipatan 0x1000) dan masuk akal
    if (base == 0)          return false;
    if (base & 0xFFF)       return false; // tidak page-aligned
    if (base < 0x10000000)  return false; // terlalu kecil
    if (base > 0xF0000000)  return false; // terlalu besar
    return true;
}

// ─── Hook: DoShadowThisFrame → selalu true ────────────
static int hook_DoShadowThisFrame(void* self, void* physical) {
    return 1;
}

// ─── Hook: RenderExtraPlayerShadows ──────────────────
static void hook_RenderExtraPlayerShadows(void) {
    if (orig_RenderExtraPlayer) orig_RenderExtraPlayer();
}

// ─── Hook: RenderStoredShadows ────────────────────────
static void hook_RenderStoredShadows(int unk) {
    if (orig_RenderStored) orig_RenderStored(unk);
}

// ─── AML ModInfo ─────────────────────────────────────
struct ModInfo {
    const char* id;
    const char* name;
    const char* version;
    const char* author;
    const char* description;
};

extern "C" {

EXPORT ModInfo* __GetModInfo() {
    static ModInfo info = {
        "libshadow",
        "Shadow Mod",
        "1.1",
        "riskiahay",
        "Force enable ped/player shadow di GTA SA Android"
    };
    return &info;
}

EXPORT void OnModPreLoad() {
    logf("[SHADOW] OnModPreLoad - shadow mod loaded v1.1");
}

EXPORT void OnModLoad() {
    logf("[SHADOW] OnModLoad start");

    uintptr_t base = getBase();

    if (!isValidBase(base)) {
        logf("[SHADOW] ERROR: base tidak valid = 0x%08X", (unsigned)base);
        logf("[SHADOW] Dump maps untuk debug:");
        FILE* f = fopen("/proc/self/maps", "r");
        if (f) {
            char line[256];
            int n = 0;
            while (fgets(line, sizeof(line), f) && n < 30) {
                if (strstr(line, "GTASA") || strstr(line, "gtasa") ||
                    strstr(line, "libGTA")) {
                    logf("  %s", line);
                    n++;
                }
            }
            fclose(f);
        }
        return;
    }

    logf("[SHADOW] libGTASA base = 0x%08X", (unsigned)base);

    // ── 1. Force bRenderShadows = true ────────────────
    volatile unsigned char* bRender =
        (volatile unsigned char*)(base + OFF_bRenderShadows);
    *bRender = 1;
    logf("[SHADOW] bRenderShadows set = 1 @ 0x%08X",
         (unsigned)(base + OFF_bRenderShadows));

    // ── 2. Set MAX_DISTANCE_PED_SHADOWS ───────────────
    volatile float* maxDist =
        (volatile float*)(base + OFF_MAX_DISTANCE_PED_SHADOWS);
    *maxDist = 50.0f;
    logf("[SHADOW] MAX_DISTANCE_PED_SHADOWS = 50.0");

    // ── 3. Hook DoShadowThisFrame ─────────────────────
    void* addrDoShadow = (void*)((base + OFF_DoShadowThisFrame) | 1);
    int r1 = DobbyHook(addrDoShadow,
                       (void*)hook_DoShadowThisFrame,
                       (void**)&orig_DoShadow);
    logf("[SHADOW] Hook DoShadowThisFrame = %d", r1);

    // ── 4. Hook RenderExtraPlayerShadows ──────────────
    void* addrExtra = (void*)((base + OFF_RenderExtraPlayerShadows) | 1);
    int r2 = DobbyHook(addrExtra,
                       (void*)hook_RenderExtraPlayerShadows,
                       (void**)&orig_RenderExtraPlayer);
    logf("[SHADOW] Hook RenderExtraPlayerShadows = %d", r2);

    // ── 5. Hook RenderStoredShadows ───────────────────
    void* addrStored = (void*)((base + OFF_RenderStoredShadows) | 1);
    int r3 = DobbyHook(addrStored,
                       (void*)hook_RenderStoredShadows,
                       (void**)&orig_RenderStored);
    logf("[SHADOW] Hook RenderStoredShadows = %d", r3);

    logf("[SHADOW] OnModLoad DONE - shadow aktif");
}

} // extern "C"
