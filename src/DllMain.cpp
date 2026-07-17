#include <windows.h>

#include <cstdint>

#include "MinHook.h"
#include "Offsets.h"

namespace {

// SMSG_AUCTION_LIST_RESULT handler: int __stdcall(int opcode, void *packet),
// RET 0x8. Runs once for every browse-list response the server sends.
using AhListResult_t = int(__stdcall *)(void *opcode, void *packet);

AhListResult_t o_AhListResult = nullptr;

int __stdcall AhListResult_h(void *opcode, void *packet) {
    // A browse-list result packet arrived, so the query the client's throttle
    // was holding the floor for is done. Clear that floor — the word
    // QueryAuctionItems armed to clock()+5000 on send — before the handler
    // fills the list and fires AUCTION_ITEM_LIST_UPDATE, so a response-driven
    // scanner sees CanSendAuctionQuery() == true in its event handler and can
    // request the next page immediately. This runs once per response regardless
    // of item-cache state, so pagination is uniform across every category.
    //
    // Self-pacing needs no interval constant: each send re-arms the floor to
    // clock()+5000, so no second query can leave until either its response
    // clears the floor here (the round-trip path) or that 5 s fallback expires
    // (a dropped response). The server's one-list-query-per-session limit is
    // therefore never violated.
    *reinterpret_cast<uint32_t *>(Offsets::VAR_AUCTION_NEXT_ALLOWED_MS) = 0;

    return o_AhListResult(opcode, packet);
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        if (MH_Initialize() != MH_OK)
            return FALSE;

        // The handler is cold (fires only on an AH list response) and is static
        // code present from process start; the throttle word is plain BSS. So a
        // MinHook co-hook installed at DLL_PROCESS_ATTACH needs no
        // wait-for-engine-init. It's reached via the network dispatch table, but
        // MinHook patches the function prologue itself, so the dispatched call
        // still lands in our detour.
        auto *target =
            reinterpret_cast<LPVOID>(Offsets::FUN_AH_LIST_RESULT_HANDLER);
        if (MH_CreateHook(target,
                          reinterpret_cast<LPVOID>(&AhListResult_h),
                          reinterpret_cast<LPVOID *>(&o_AhListResult)) != MH_OK)
            return FALSE;
        if (MH_EnableHook(target) != MH_OK)
            return FALSE;
    } else if (reason == DLL_PROCESS_DETACH) {
        MH_Uninitialize();
    }
    return TRUE;
}
