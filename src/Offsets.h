#pragma once
#include <cstdint>

// All VAs are for C:\WoW\Octo\WoW.exe (vanilla 1.12.1, ImageBase 0x00400000).
namespace Offsets {

// The single client-side throttle word gating the auction browse-list query:
// the earliest engine-clock millisecond at which the next QueryAuctionItems
// may be sent. 0 = "send now".
//
// QueryAuctionItems (0x004CE980) re-arms it to clock()+5000 on every real send;
// CanSendAuctionQuery (0x004CFBB0) is nothing but a read of it. Verified via
// xrefs: this word is read ONLY by those two functions, so it exclusively
// throttles the browse-list scan (the owner/bidder list queries don't use it).
constexpr uintptr_t VAR_AUCTION_NEXT_ALLOWED_MS = 0x00B72638;

// Network handler for SMSG_AUCTION_LIST_RESULT (opcode 0x25C), the browse-list
// response. `int __stdcall(int opcode, void *packet)` (RET 0x8). Registered by
// the AH init FUN_004CC0F0 via FUN_005AB650(0x25C, 0x004CC7F0, 0). It parses the
// packet, fills the browse list, and signals AUCTION_ITEM_LIST_UPDATE (0x1A8).
//
// This is THE per-response hook point: it runs exactly once for every browse
// result the server sends, independent of the client item cache. (Do NOT hook
// the completion callback 0x004CF290 — that is the item-cache *resolution*
// refire, fired once per uncached item as its data arrives, so it never fires
// for a fully-cached page and the throttle would stay armed for the full 5 s.
// That was the cause of per-category pagination-speed variance.)
constexpr uintptr_t FUN_AH_LIST_RESULT_HANDLER = 0x004CC7F0;

} // namespace Offsets
