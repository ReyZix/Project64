# Project 64 — Autonomous Chessboard Movement & Pathfinding Engine

Project 64 is an autonomous chessboard that moves pieces with an under-board XY gantry carrying a 12V electromagnet.

This module is written entirely in **Rust** (with optional standard C++ firmware headers for C++ microcontroller builds). **Zero JavaScript or TypeScript is used anywhere.**

---

## Workspace Layout

```
movement_algorithm/
├── Cargo.toml                             # Cargo Workspace definition
├── crates/
│   ├── movement_planner/                  # Core Rust Motion Planning Engine
│   │   ├── Cargo.toml
│   │   ├── src/
│   │   │   ├── lib.rs                     # Library exports
│   │   │   ├── types.rs                   # Point2D, GridCoord, PieceType, Config, Commands
│   │   │   ├── planner.rs                 # Lattice A* Router, Edge Nudging, Move Sequencer
│   │   │   ├── reset.rs                   # Automatic Magnet Board Reset & Re-racking
│   │   │   ├── chess_rules.rs             # Complete Chess Rules Engine (legality, checks, mate)
│   │   │   └── scenarios.rs               # Preset Demonstration Scenarios
│   │   └── tests/
│   │       └── test_planner.rs            # Integration & Unit Tests
│   ├── simulator/                         # Native 3D Hardware-Accelerated Simulator
│   │   ├── Cargo.toml
│   │   └── src/
│   │       └── main.rs                    # 3D Gantry Visualizer, Orbit Camera & Interactive GUI
│   └── test_overlap/                      # Collision & Invariant Verification Suite
│       ├── Cargo.toml
│       └── src/
│           └── main.rs                    # Continuous collision checking across all 18 scenarios
├── firmware/                              # Microcontroller C++17 Reference (ESP-IDF / Arduino)
│   ├── movement_planner.hpp
│   └── movement_planner.cpp
├── tests/
│   └── test_piece_overlap.cpp             # Native C++ verification test suite
└── README.md
```

---

## Quickstart (Rust)

Make sure Rust is installed (`rustc` and `cargo` in your PATH). All commands are run from `movement_algorithm` (or with `--manifest-path`):

### 1. Launch the Native 3D Gantry Simulator
```powershell
cd movement_algorithm
cargo run -p simulator
```
- **Interactive 3D Viewport**: Real-time rendering of the chessboard, under-board XY gantry carriage, 12V electromagnet with dynamic glow, and 3D procedural chess pieces.
- **Mouse Camera Controls**:
  - Left / Right drag: Orbit camera (yaw / pitch).
  - Mouse scroll wheel: Zoom in / out.
  - Camera Preset buttons: White POV, Black POV, Top-Down, Under-Board Gantry, Side Profile.
- **Click to Move**: Click any piece of the side to move to inspect legal moves, then click a highlighted destination square.
- **Algebraic Move Input**: Type moves directly into the on-screen input box (e.g. `e2e4`, `g1f3`, `e1g1`) and click **Play** or press **Enter**.
- **Scenario Demos**: One-click preset scenario buttons:
  1. *Knight Pass (Edge Nudge)*
  2. *Bishop Tight Diagonal*
  3. *Capture to Graveyard*
  4. *Kingside Castling*
  5. *Pawn Promotion*
  6. *Magnet Board Reset*
  7. *Instant Reset*
- **Playback Controls**: Pause, Resume, Skip Step, Instant Finish, and adjustable animation speed slider ($0.25\times$ to $5.0\times$).
- **Telemetry HUD**: Live $(X, Y)$ gantry position in square units and millimeters, 12V electromagnet engagement state, and a scrollable real-time command queue.

---

### 2. Run the Physical Collision & Overlap Test Suite
```powershell
cd movement_algorithm
cargo run -p test_overlap
```
The test suite performs continuous mathematical collision verification across all moves:
- **Continuous distance checking**: Verifies $\text{dist}(t) \ge R_1 + R_2$ at all times during carriage and piece movements ($t \in [0, 1]$).
- **Strict 45° angle invariant**: Verifies that every gantry movement is strictly orthogonal ($0^\circ, 90^\circ$) or exact $45^\circ$ diagonal.
- **Restoration check**: Guarantees that every nudged piece returns to the exact square center.
- **Boundary check**: Ensures the gantry never exceeds the physical bed limits ($X \in [-2.5, 9.5], Y \in [-1.5, 9.5]$).
- **All Move Scenarios**: Pawns, Knights, tight Bishop diagonals, Rooks, Queens, Kings, Castling, Captures & Graveyards, En-Passant, Promotions, and Full Board Magnet Resets.

---

### 3. Run Standard Unit Tests
```powershell
cd movement_algorithm
cargo test
```

---

## How the Planner Works

### Coordinate System

- Playable board is an 8×8 grid: `col` 0..7 (files a–h), `row` 0..7 (ranks 1–8).
- Square centers are at `(col + 0.5, row + 0.5)` in square units; multiply by `square_size_mm` for millimeters.
- Side graveyard / reserve zones hold 16 slots per side:

| Column | Side | Contents |
| :--- | :--- | :--- |
| **-2** | Left (White reserve / captured Black) | 8 pawn slots, outer edge |
| **-1** | Left | 8 heavy-piece slots, inner edge |
| **8** | Right (Black reserve / captured White) | 8 heavy-piece slots, inner edge |
| **9** | Right | 8 pawn slots, outer edge |

- Two **transit lanes** run along the front (`row -1`, center $y = -0.5$) and back (`row 8`, center $y = 8.5$) of the board, spanning `col -2..9`. They stay empty and are used to carry pieces around the board during captures and promotions.
- Heavy-piece reserve slots are marked for queens, rooks, bishops and knights; captured pieces prefer a free slot of their own type.

---

### Command Types (`CommandAction`)

| Action | Meaning |
| :--- | :--- |
| `TravelMagnetOff` | Move the carriage without a piece |
| `MagnetOn` | Engage the 12V electromagnet |
| `MovePieceStraight` | Drag a piece along a straight line (orthogonal or 45° diagonal only) |
| `MagnetOff` | Release the piece |

---

### Algorithm Sequence (`MovementPlanner::plan_move`)

1. **Capture**: The captured piece (also handles en passant via `captured_square`) is escorted to a free graveyard slot. Pawns go to the outer column, other pieces to the inner column, with a preference for a free slot marked for that piece type. The planner picks the slot with the lowest total route cost.
2. **Routing** (`find_route` / `plan_route`): Routed over a half-square lattice covering the board, both graveyards, and the two transit lanes. Cost = X/Y path length + 0.3 per turn + 0.8 per nudged neighbor per half-step + a 4x multiplier on 45° steps.
3. **Primary Move**: Pawns, bishops, rooks, queens, and kings travel in a straight line when clear. **Knights** use lattice routing (an X leg then a Y leg, nudging adjacent pieces aside).
4. **Edge Nudging** (`plan_point_to_point_with_edge_nudging`): Any piece within clearance distance of any leg of a path (`max(2r + margin, 0.58)` square units) is nudged toward its own square edge or corner (up to `max_edge_nudge_ratio`). After the active piece moves, nudged pieces are restored to square centers in reverse order.
5. **Castling**: The king moves, then the rook routes from `rook_from` to `rook_to` around the king with edge nudging.
6. **Promotion**: The pawn travels to the promotion square, is escorted to an outer pawn graveyard slot, and the closest matching reserve piece is delivered from the reserve column via the transit lanes.
7. **Magnet Board Reset** (`plan_reset_board_with_magnet`): Bipartite distance matching identifies each piece's home square, moving pieces whose destination is clear and temporarily parking occupants on spare slots if cycles exist.

---

## Configuration (`DimensionsConfig`)

| Field | Default | Description |
| :--- | :--- | :--- |
| `square_size_mm` | 60.0 | Size of one chess square (mm) |
| `piece_radius_mm` | 14.0 | Standard piece base radius (mm) |
| `safety_margin_mm` | 3.0 | Extra clearance margin between moving pieces (mm) |
| `max_edge_nudge_ratio` | 0.34 | Max nudge offset from square center (in square units) |
| `gantry_travel_speed_mm_s` | 80.0 | Carriage speed without a piece |
| `gantry_carry_speed_mm_s` | 45.0 | Carriage speed carrying a piece |

---

## C++ Microcontroller Usage (ESP32)

For developers using C++17 on ESP32 (ESP-IDF or Arduino IDE), the standalone C++ implementation remains available under `movement_algorithm/firmware/`:

```cpp
#include "movement_planner.hpp"
using namespace project64;

MovementPlanner planner;
std::vector<PieceState> pieces = /* current board state */;

MoveRequest mv;
mv.from = {6, 0}; // g1
mv.to   = {5, 2}; // f3

std::vector<GantryCommand> cmds = planner.planMove(pieces, mv);
for (const auto& c : cmds) {
    // Dispatch to stepper motor / electromagnet motion layer
}
```

Compile native C++ tests:
```powershell
g++ -std=c++17 -Wall -Wextra -I. movement_algorithm/tests/test_piece_overlap.cpp movement_algorithm/firmware/movement_planner.cpp -o test_piece_overlap.exe
.\test_piece_overlap.exe
```
