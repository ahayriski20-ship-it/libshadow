#include <stdint.h>
#include <dlfcn.h>
#include <android/log.h>
#include <stdio.h>
#include <stdarg.h>

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

// ─── Offsets dari libGTASA.so (verified readelf) ─────
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

// ─── Dobby interface ─────────────────────────────────
extern "C" {
    int DobbyHook(void* addr, void* replace, void** orig);
}

static uintptr_t gBase = 0;

static uintptr_t getBase() {
    if (gBase) return gBase;
    void* h = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
    if (h) gBase = (uintptr_t)h;
    return gBase;
}

// ─── Hook: DoShadowThisFrame → selalu true ────────────
static int hook_DoShadowThisFrame(void* self, void* physical) {
    return 1; // force render shadow untuk semua entity
}

// ─── Hook: RenderExtraPlayerShadows ──────────────────
static void hook_RenderExtraPlayerShadows(void) {
    orig_RenderExtraPlayer(); // jalankan normal
}

// ─── Hook: RenderStoredShadows ────────────────────────
static void hook_RenderStoredShadows(int unk) {
    orig_RenderStored(unk);
}

// ─── AML Interface (minimal stub) ────────────────────
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
        "1.0",
        "riskiahay",
        "Force enable ped/player shadow di GTA SA Android"
    };
    return &info;
}

EXPORT void OnModPreLoad() {
    logf("[SHADOW] OnModPreLoad - shadow mod loaded");
}

EXPORT void OnModLoad() {
    logf("[SHADOW] OnModLoad start");

    uintptr_t base = getBase();
    if (!base) {
        logf("[SHADOW] ERROR: libGTASA base = 0");
        return;
    }
    logf("[SHADOW] libGTASA base = 0x%08X", (unsigned)base);

    // ── 1. Force bRenderShadows = true ────────────────
    unsigned char* bRender = (unsigned char*)(base + OFF_bRenderShadows);
    *bRender = 1;
    logf("[SHADOW] bRenderShadows set = 1");

    // ── 2. Set MAX_DISTANCE_PED_SHADOWS (lebih jauh) ──
    float* maxDist = (float*)(base + OFF_MAX_DISTANCE_PED_SHADOWS);
    *maxDist = 50.0f; // default biasanya 15-20
    logf("[SHADOW] MAX_DISTANCE_PED_SHADOWS = 50.0");

    // ── 3. Hook DoShadowThisFrame ─────────────────────
    void* addrDoShadow = (void*)(base + OFF_DoShadowThisFrame);
    int r1 = DobbyHook(addrDoShadow,
                       (void*)hook_DoShadowThisFrame,
                       (void**)&orig_DoShadow);
    logf("[SHADOW] DobbyHook DoShadowThisFrame = %d, addr=0x%08X",
         r1, (unsigned)(base + OFF_DoShadowThisFrame));

    // ── 4. Hook RenderExtraPlayerShadows ──────────────
    void* addrRenderExtra = (void*)(base + OFF_RenderExtraPlayerShadows);
    int r2 = DobbyHook(addrRenderExtra,
                       (void*)hook_RenderExtraPlayerShadows,
                       (void**)&orig_RenderExtraPlayer);
    logf("[SHADOW] DobbyHook RenderExtraPlayerShadows = %d", r2);

    // ── 5. Hook RenderStoredShadows ───────────────────
    void* addrRenderStored = (void*)(base + OFF_RenderStoredShadows);
    int r3 = DobbyHook(addrRenderStored,
                       (void*)hook_RenderStoredShadows,
                       (void**)&orig_RenderStored);
    logf("[SHADOW] DobbyHook RenderStoredShadows = %d", r3);

    logf("[SHADOW] OnModLoad DONE");
}

} // extern "C"
