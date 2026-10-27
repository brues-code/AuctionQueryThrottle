#include <windows.h>

#include <cstdint>

#include "MinHook.h"
#include "Offsets.h"

namespace {

// SMSG_AUCTION_LIST_RESULT handler, in the network dispatch's handler
// convention: context in ECX, opcode in EDX, then time and message on the
// stack (RET 0x8). Runs once for every browse-list response the server sends.
using AhListResult_t = uint32_t(__fastcall *)(void *param, uint32_t opcode,
                                              uint32_t time, void *msg);

AhListResult_t o_AhListResult = nullptr;

uint32_t __fastcall AhListResult_h(void *param, uint32_t opcode, uint32_t time,
                                   void *msg) {
    // A browse-list result packet arrived, so the query the client's throttle
    // was holding the floor for is done. Clear that floor — the word
    // QueryAuctionItems armed to clock()+5000 on send — before the handler
    // fills the list. On a fully-cached page the handler fires
    // AUCTION_ITEM_LIST_UPDATE synchronously, so a response-driven scanner sees
    // CanSendAuctionQuery() == true in its event handler and can request the
    // next page immediately; otherwise the event fires later, from the last
    // item-cache arrival (0x004CCA10), with the gate already open. This runs
    // once per response regardless of item-cache state, so pagination is
    // uniform across every category.
    //
    // Self-pacing needs no interval constant: each send re-arms the floor to
    // clock()+5000, so no second query can leave until either its response
    // clears the floor here (the round-trip path) or that 5 s fallback expires
    // (a dropped response). The server's one-list-query-per-session limit is
    // therefore never violated.
    *reinterpret_cast<uint32_t *>(Offsets::VAR_AUCTION_NEXT_ALLOWED_MS) = 0;

    return o_AhListResult(param, opcode, time, msg);
}

// ---------------------------------------------------------------------------
// Hook install — kept OFF the Windows loader lock.
//
// MH_EnableHook freezes all process threads (CreateToolhelp32Snapshot +
// SuspendThread/GetThreadContext each), which MinHook documents as unsafe to
// run from DllMain under the loader lock. So DllMain installs nothing; the
// install runs later, on a thread that does NOT hold the lock, via one of two
// triggers that both funnel through the latched EnsureInitialized():
//   * VanillaFixes' `Load` export, called on the game's main thread after
//     injection (the sanctioned VF extension point), or
//   * a fallback worker thread we spawn from DllMain, ONLY when VanillaFixes
//     is not the loader (no VfPatcher.dll in the process), for injectors that
//     never call `Load`.
//
// The worker MUST stand down under VanillaFixes: VF injects every dlls.txt
// DLL sequentially via remote threads, and a worker spawned from our DllMain
// would run its thread-freezing install while VF is still injecting the NEXT
// dlls.txt DLL, racing that DLL's DllMain (the failure mode ClassicAPI hit
// with SuperWoWhook). `Load` has no such race: VfPatcher calls it on the
// game's main thread after every DllMain in the injection chain completed,
// serialized in dlls.txt order.
//
// The target itself needs no waiting either way: the handler is static code
// present from process start, and the throttle word is plain BSS. It's
// reached via the network dispatch table, but MinHook patches the function
// prologue itself, so the dispatched call still lands in our detour.
// ---------------------------------------------------------------------------

volatile LONG g_initClaimed = 0;    // 0 until a thread takes the installer role
volatile LONG g_initDone = 0;       // 0 until the install has finished
volatile LONG g_initResult = 1;     // 0 = success, 1 = failure (until proven)
volatile LONG g_mhInitialized = 0;  // MH_Initialize succeeded (gates detach teardown)

bool InstallHook() {
    if (MH_Initialize() != MH_OK)
        return false;
    InterlockedExchange(&g_mhInitialized, 1);

    auto *target = reinterpret_cast<LPVOID>(Offsets::FUN_AH_LIST_RESULT_HANDLER);
    if (MH_CreateHook(target,
                      reinterpret_cast<LPVOID>(&AhListResult_h),
                      reinterpret_cast<LPVOID *>(&o_AhListResult)) != MH_OK)
        return false;
    return MH_EnableHook(target) == MH_OK;
}

// Runs InstallHook exactly once. Returns 0 on success, 1 on failure. If a
// second caller arrives while the install is in flight, it blocks briefly so
// both callers observe the real result.
DWORD EnsureInitialized() {
    if (InterlockedCompareExchange(&g_initClaimed, 1, 0) == 0) {
        g_initResult = InstallHook() ? 0 : 1;
        InterlockedExchange(&g_initDone, 1);
    } else {
        while (InterlockedCompareExchange(&g_initDone, 0, 0) == 0)
            Sleep(1);
    }
    return static_cast<DWORD>(g_initResult);
}

DWORD WINAPI InitWorker(LPVOID) {
    EnsureInitialized();
    return 0;
}

} // namespace

// VanillaFixes calls this on the game's MAIN thread after injection
// (InitAdditionalDLLs -> GetProcAddress(module, "Load")), outside the loader
// lock and with no timeout — the sanctioned VF extension point. Returns 0 on
// success; VF reports any non-zero result to the user. Exported undecorated
// as "Load" via src/AuctionQueryThrottle.def.
extern "C" DWORD Load() { return EnsureInitialized(); }

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // Install nothing here (loader lock — see the block comment above).
        //
        // Under VanillaFixes, `Load` is the ONLY install trigger. VfPatcher.dll
        // is injected before any dlls.txt DLL, so its presence identifies VF as
        // the loader; and the `Load` mechanism shipped in the same VF release
        // (v1.4) as dlls.txt support itself, so any VF that loaded us will call
        // it. Spawning the worker here would race the DllMains of the dlls.txt
        // DLLs VF injects after us (see the block comment above).
        if (GetModuleHandleW(L"VfPatcher.dll") == nullptr) {
            // Any other injector: no `Load` caller exists, so install from a
            // worker off the lock. The new thread cannot run its body until
            // DllMain returns and the loader lock releases, and we never wait
            // on it. DisableThreadLibraryCalls suppressed its THREAD_ATTACH.
            HANDLE worker = CreateThread(nullptr, 0, InitWorker, nullptr, 0, nullptr);
            if (worker != nullptr)
                CloseHandle(worker);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        // Clean /quit path — only tear down if we actually initialized.
        if (g_mhInitialized)
            MH_Uninitialize();
    }
    return TRUE;
}
