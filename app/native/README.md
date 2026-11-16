# Project64 native iOS app

The native app includes the migration through phases 3–4: UIKit home and game
screens, C++ chess rules and local Stockfish, BLE/Wi-Fi/mock board transports,
and Lichess online play with Keychain credentials. The implementation plan is
[`PHASE34_PLAN.md`](PHASE34_PLAN.md). The Expo app in the parent directory remains
as a reference; it is not included in the native build.

The native target does not build or run JavaScript, TypeScript, React Native,
Expo, Node, WebViews, or WASM. CMake is build tooling, not an app dependency.

## Build and test the C++ core on macOS

Requires Xcode command-line tools, CMake 3.22 or newer, and Python 3 for the local
test fixture. Python is test tooling only and is not bundled in the iOS app.

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
cmake --build build-ios --config Release --target Project64Native
```

To run the C++ tests against the iOS simulator runtime, boot a simulator in Xcode
or Simulator, then run from this directory:

```sh
cmake --build build-ios --config Release --target ChessGameTests StockfishTests
xcrun simctl spawn booted "$PWD/build-ios/Release-iphonesimulator/ChessGameTests.app/ChessGameTests"
xcrun simctl spawn booted "$PWD/build-ios/Release-iphonesimulator/StockfishTests.app/StockfishTests" "$PWD/Vendor/stockfish"
```

For an iPhone, generate a separate `build-device` directory with
`-DCMAKE_OSX_SYSROOT=iphoneos` and `-DCMAKE_OSX_ARCHITECTURES=arm64`.
Select your development team and a unique bundle identifier in Xcode before
running on the device. Simulator builds use automatic ad-hoc signing and simulated
Keychain entitlements; no development team or Apple account is needed. Do not
disable signing for simulator app builds if you want to exercise Keychain storage.
Device signing continues to use your selected team and provisioning profile.

## Structure and ownership

| Path | Purpose |
| --- | --- |
| `App/main.mm` | Native application launch and navigation |
| `App/P64GameSession.*` | Main-queue game coordinator, move delivery, clocks, online synchronization and cancellation |
| `App/P64HomeViewController.*` | UIKit home, interactive board, settings, online lobby, promotion and alignment controls |
| `App/P64PieceView.*` | Consistent native vector chess pieces for both colors; no font or emoji rendering |
| `Core/ChessGame.*` | C++ wrapper around the vendored rules library: FEN, legal UCI moves, results, history, occupancy inference |
| `Core/StockfishEngine.*` | Serialized, blocking native searches with cancellation and game-generation invalidation |
| `Interfaces/P64HardwareBridge.h` | Shared Objective-C board contract; ESP32 opcodes remain unchanged |
| `Services/Hardware/` | CoreBluetooth, native WebSocket/HTTP, mock board and payload decoding |
| `Services/Online/` | Lichess HTTP/NDJSON client and Keychain token storage |
| `Tests/` | Core, coordinator and UIKit tests, plus the isolated local server runner |
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

## Using the app

Tap **Play Game**. The app starts in Stockfish mode and automatically connects to
**Mock**, so no ESP32 or account is required. Tap a piece and a highlighted square
to move. Promotions present a piece choice. Settings provides pass-and-play,
Stockfish and Online modes, board transport/target, and engine skill 0–20.

For the ESP32, select **Bluetooth**, leave target `Project64` (or enter the
advertised name/peripheral identifier), and Connect on a physical iPhone.
The GATT contract is unchanged: CB00 service; CB01 board; CB02 move; CB03 status;
CB04 command. Home=1, emergency stop=2, reboot=3. Firmware must report
Moving/Homing followed by Idle: a successful write alone never unlocks the board.
Unknown status codes lock the game as Error.

For Wi-Fi, enter a bare IPv4 address or hostname, such as `192.168.4.1`.
The board WebSocket is `ws://<target>:81/ws`; outgoing JSON is
`{"type":"move","data":"e2e4"}` or `{"type":"cmd","data":2}`.
When the socket is unavailable, the adapter can POST the same JSON to
`http://<target>/api`. Gameplay pauses without the live status connection;
system controls can still use HTTP fallback. An explicit Disconnect clears the
target from the adapter. Loopback `localhost:PORT`/`127.0.0.1:PORT` is accepted
for tests and maps both paths to that port. IPv6 literals are not accepted by
the current target editor/adapter. Allow the iOS Bluetooth and Local Network
prompts when using hardware. ATS permits local networking without disabling
HTTPS protection for internet requests.

In **Online**, use a Lichess personal access token with `board:play` and
`challenge:write` scopes ([create a token](https://lichess.org/account/oauth/token)).
The app validates the account before saving the token in device-local Keychain;
sign-out removes it. Random seeks use 10-minute or slower controls. The lobby
also supports direct challenges, accept/decline, cancellation, resign/early abort,
results, and returning to the lobby. Only standard chess is supported. Stockfish
is never used to select moves in Online mode.

Lichess must accept a screen move before hardware receives it. Opponent moves
wait for connected, idle hardware and only update the displayed position after
the transport accepts the write. A failed move stays pending for **Retry**;
repeated stream updates do not send it twice. Sensor moves made by hand are
never echoed back to the gantry. Sensors are ignored until the board's motion
finishes, including the gap between write acknowledgement and motion status.
Emergency stop pauses further deliveries until an explicit **Retry**. Position
rebuilds, interrupted motion, and switching to a real board can require
manual alignment with the displayed position. **Board aligned — continue**
acknowledges that reconciliation; the app does not reposition every piece itself.

Local clocks use elapsed monotonic time. Online clocks follow server snapshots
and interpolate locally; only Lichess decides an online result. Returning home
keeps the current game. Games are not saved across application termination.

## Integration and UI validation

The host `ctest` command above runs five suites: rules, real Stockfish, hardware,
online client, and coordinator. Each network suite starts an isolated Python
server on a temporary loopback port. Tests do not contact Lichess or use real
credentials. They cover split UTF-8/NDJSON lines, HTTP errors/cooldown, request
serialization, WebSocket/HTTP fallback, motion locks, stale acknowledgements,
failed deliveries/retries, ordered remote catch-up, engine cancellation, and
authoritative position changes. Test login never loads or writes the production
Keychain credential.

For simulator runtime integration checks (from `app/native`, with one simulator
booted; replace `booted` with its UUID when multiple devices are running):

```sh
cmake --build build-ios --config Release --target HardwareFlowTests OnlineFlowTests NativeFlowTests UISmokeTests
python3 Tests/run_native_tests.py --simulator booted --hardware "$PWD/build-ios/Release-iphonesimulator/HardwareFlowTests.app/HardwareFlowTests"
python3 Tests/run_native_tests.py --simulator booted --online "$PWD/build-ios/Release-iphonesimulator/OnlineFlowTests.app/OnlineFlowTests"
python3 Tests/run_native_tests.py --simulator booted --session "$PWD/build-ios/Release-iphonesimulator/NativeFlowTests.app/NativeFlowTests" --networks "$PWD/Vendor/stockfish"
python3 Tests/run_native_tests.py --simulator booted --ui "$PWD/build-ios/Release-iphonesimulator/UISmokeTests.app"
```

The separate UIKit smoke app tests home/play/back, square selection and movement,
emergency stop while locked, the secure online entry field, and an isolated
Keychain save/update/load/delete cycle. It uses a test-only Keychain service and
does not access a user's saved token. A convenience launch argument
`--p64-show-game` opens the normal app directly to the board for screenshots.

Real BLE discovery, permission prompts, physical gantry motion and recovery, and
live-account Lichess behavior still need device/account acceptance testing.
Simulator tests prove the native code and controlled flows, not physical operation.

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
