# Project64 native iOS foundation

This is migration phases 1–2: an independent UIKit/Objective-C++ app target,
C++ chess rules, and in-process native Stockfish. The small integration screen
accepts UCI moves, requests a computer move, and resets/cancels a game.
The complete board UI, BLE/Wi-Fi/mock adapters, and Lichess are not ported yet.
The Expo app in the parent directory remains available during this migration.

The native target does not build or run JavaScript, TypeScript, React Native,
Expo, Node, WebViews, or WASM. CMake is build tooling, not an app dependency.

## Build and test the C++ core on macOS

Requires Xcode command-line tools and CMake 3.22 or newer.

```sh
cd app/native
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --parallel 4
ctest --test-dir build-host --output-on-failure
```

## Generate and open the native Xcode project

Run these commands from `app/native` (not the existing Expo `app/ios` directory):

```sh
cmake -S . -B build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
open build-ios/Project64Native.xcodeproj
```

Select the `Project64Native` scheme and an available iPhone simulator, then Run.
On an Intel Mac, use `-DCMAKE_OSX_ARCHITECTURES=x86_64` instead.
The equivalent command-line build is:

```sh
cmake --build build-ios --config Release --target Project64Native -- CODE_SIGNING_ALLOWED=NO
```

To run the C++ tests against the iOS simulator runtime, boot a simulator in Xcode
or Simulator, then run from this directory:

```sh
cmake --build build-ios --config Release --target ChessGameTests StockfishTests -- CODE_SIGNING_ALLOWED=NO
xcrun simctl spawn booted "$PWD/build-ios/Release-iphonesimulator/ChessGameTests.app/ChessGameTests"
xcrun simctl spawn booted "$PWD/build-ios/Release-iphonesimulator/StockfishTests.app/StockfishTests" "$PWD/Vendor/stockfish"
```

For an iPhone, generate a separate `build-device` directory with
`-DCMAKE_OSX_SYSROOT=iphoneos` and `-DCMAKE_OSX_ARCHITECTURES=arm64`.
Select your development team and a unique bundle identifier in Xcode before
running on the device. Signing is not needed for simulator builds.

## Structure and ownership

| Path | Purpose |
| --- | --- |
| `App/main.mm` | Objective-C++ UIKit launch and integration screen; game state lives on the main queue |
| `Core/ChessGame.*` | C++ wrapper around the vendored rules library: FEN, legal UCI moves, results, history, occupancy inference |
| `Core/StockfishEngine.*` | Serialized, blocking native searches with cancellation and game-generation invalidation |
| `Interfaces/P64HardwareBridge.h` | Objective-C contract for later transport implementations; ESP32 opcodes remain unchanged |
| `Tests/` | Host tests for rules and the real native engine |
| `Vendor/` | Pinned dependencies, neural-network resources, licenses and provenance |

Run Stockfish searches on a worker queue. Never update UIKit or the live game
from that queue. Apply an engine move only after validating it against the current
game, and discard results whose game revision has changed. `newGame()` invalidates
engine work and clears the engine's search state before its next search.
Pass the initial FEN and full UCI move history to searches so the engine retains
repetition context. Engine instances serialize searches across the process
because upstream Stockfish has shared tablebase state.

Occupancy uses bit 0 for a1 and bit 63 for h8. Inference returns all matching
legal moves: promotion and some captures cannot be distinguished from occupancy
alone. Transport implementations must not select an arbitrary ambiguous move.

## Dependencies and licenses

Rules: [Disservin/chess-library](https://github.com/Disservin/chess-library), MIT.
Engine: [official-stockfish/Stockfish](https://github.com/official-stockfish/Stockfish),
Stockfish 17.1, GPL-3.0-or-later. This deliberately pins a native release rather
than reusing the previous Stockfish 19 JavaScript/WASM bundle.

The Stockfish networks are bundled as resources, so computer play needs no
network connection. See each vendor directory for the exact revision, file
hashes, source, and license. The native target links GPL Stockfish; retain its
license and provide the corresponding source/build instructions when distributing
the linked app. This repository contains the engine source and native build setup.
