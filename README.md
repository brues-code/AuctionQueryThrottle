# AuctionQueryThrottle

A tiny DLL for World of Warcraft 1.12.1 that makes the client-side auction-house
**browse-query throttle response-driven**. Instead of forcing the stock fixed
**5-second** wait between auction searches, the next query is allowed the moment
the server's result arrives — so paging through the AH runs at round-trip speed
(a fraction of a second on a healthy connection).

It does exactly one thing: it never blocks a query longer than the server takes
to answer the previous one. It works with the **default auction UI** and with
any scanning addon; **no companion addon is required**.

## Installation

Download the prebuilt `AuctionQueryThrottle.dll` from the
[latest release](https://github.com/brues-code/AuctionQueryThrottle/releases/latest)
(or [build it yourself](#building)). It's loaded with
[VanillaFixes](https://github.com/hannesmann/vanillafixes):

1. Install VanillaFixes if it isn't already.
2. Copy `AuctionQueryThrottle.dll` into your game directory (next to `WoW.exe`).
3. Add `AuctionQueryThrottle.dll` to `dlls.txt`.
4. Launch the game with `VanillaFixes.exe`.

## How it works

The 1.12 client rate-limits the browse query through a single timer word: on each
search it refuses to send another for 5000 ms. That floor is what makes stock AH
browsing feel sluggish — it waits the full 5 s even though the server has usually
replied in a fraction of that.

This DLL hooks the `SMSG_AUCTION_LIST_RESULT` network handler and clears that
floor the instant a result packet arrives (before the client dispatches
`AUCTION_ITEM_LIST_UPDATE`). The engine's own re-arm on send stays in place as
the in-flight lock and dropped-packet fallback, so:

- each query re-arms the floor (no second query can leave while one is pending);
- each response clears it (the next query is allowed exactly then).

There is **no fixed interval and nothing to tune** — pacing falls out of the
round trip. This matches a mangos server, which accepts one list query per
session at a time and clears its in-flight lock at the same instant it sends the
result, so a query sent right after a response is always accepted.

The reverse-engineered offsets and the full rationale (including why the packet
handler is hooked rather than the item-cache resolution callback) are documented
in the comments of [`src/Offsets.h`](src/Offsets.h) and
[`src/DllMain.cpp`](src/DllMain.cpp).

## Building

Requires CMake (3.10+) and an MSVC toolchain that can target 32-bit Windows.
`WoW.exe` is x86, so the DLL must be built as **Win32**; an x64 build will not
load.

```powershell
git submodule update --init --recursive   # fetches MinHook
cmake -B build -A Win32
cmake --build build --config Release
```

The output is `build/Release/AuctionQueryThrottle.dll`.

## License

GPL v3 or later (a derivative of [ClassicAPI](https://github.com/brues-code/ClassicAPI)).
See [LICENSE](LICENSE) for the full notice.
