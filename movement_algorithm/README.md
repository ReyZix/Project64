# Project 64 — Autonomous Chessboard Movement & Pathfinding Engine

Project 64 is an autonomous chessboard that moves pieces with an under-board XY gantry carrying a 12V electromagnet. This repository contains:

- **Firmware** (C++): a movement planner that turns any chess move into a collision-free sequence of gantry and magnet commands. Target: **ESP32 Core 1**.
- **Simulator** (HTML/JS): a browser-based 3D gantry and movement simulator that visualizes the planned command queue.
- **Design Draft.pdf**: the original design document.

## Repository Layout

```
Project64/
├── movement_algorithm/
│   ├── firmware/
│   │   ├── movement_planner.hpp   # Types, config, MovementPlanner class
│   │   └── movement_planner.cpp   # Planning, nudging, graveyard and promotion logic
│   ├── simulator/
│   │   └── index.html             # Single-file simulator (HTML + JS + Tailwind)
│   └── README.md
├── Design Draft.pdf
└── README.md
```

## Running the Simulator on localhost

The simulator is a single static file, so any static web server works. Run the commands below from the project root (`Project64`), then open **http://localhost:8000/movement_algorithm/simulator/index.html**.

**Option 1 — Python (no install needed beyond Python 3)**
```powershell
cd Project64
python -m http.server 8000
```

**Option 2 — Node.js**
```powershell
cd Project64\movement_algorithm\simulator
npx serve -l 8000
```
Then open http://localhost:8000.

**Option 3 — VS Code**
Install the *Live Server* extension, right-click `movement_algorithm\simulator\index.html`, and choose **Open with Live Server**.

Stop the server with `Ctrl+C`. If port 8000 is busy, use another port (for example `python -m http.server 8080`) and change the URL to match.

> **Note:** The page loads Tailwind CSS from a CDN, so you need an internet connection for the styling to appear. Opening `index.html` directly by double-click usually works too, but serving it over localhost is the recommended approach.

### Using the Simulator

- **Scenario buttons**: run preset demos (knight nudge, bishop diagonal, capture to graveyard, promotion, castling), or reset to the standard starting position.
- **Click to move**: click a piece of the side to move, then one of the highlighted squares. Only legal chess moves are accepted (check, pins, castling rules, en passant and promotion are all enforced). Turns alternate starting with White, and the badge shows check, checkmate or stalemate. Use **Reset Full Board** to start over.
- **Algebraic input**: type a move such as `g1f3`, `e2e4` or `c1g5` and press Enter. Pick the promotion piece from the dropdown.
- **Camera presets**: White POV, Black POV, Top-Down, Under-Board Gantry and Side Profile. Drag to orbit the view.
- **Overlays**: toggle the legal-move highlights (illegal moves are rejected either way), X-ray view, and piece clearance radii.
- **Playback**: pause, or skip the current animation. The simulation speed is adjustable.
- **Dimensions & nudge parameters**: adjust square size, piece diameter, nudge ratio and speed live.
- **HUD and queue**: shows magnet state, gantry X/Y in mm, and the full "Gantry Execution Queue" produced by the planner.

## How the Planner Works

### Coordinate System

- Playable board is an 8×8 grid: `col` 0..7 (files a–h), `row` 0..7 (ranks 1–8).
- Square centers are at `(col + 0.5, row + 0.5)` in square units; multiply by `squareSizeMm` for millimeters.
- Side graveyard / reserve zones hold 16 slots per side:

| Column | Side | Contents |
|--------|------|----------|
| -2 | Left (White reserve / captured Black) | 8 pawn slots, outer edge |
| -1 | Left | 8 heavy-piece slots, inner edge |
| 8 | Right (Black reserve / captured White) | 8 heavy-piece slots, inner edge |
| 9 | Right | 8 pawn slots, outer edge |

- Two **transit lanes** run along the front (`row -1`, center `y = -0.5`) and back (`row 8`, center `y = 8.5`) of the board, spanning `col -2..9`. They stay empty and are used to carry pieces around the board during captures and promotions.
- Heavy-piece reserve slots are marked for queens, rooks, bishops and knights; captured pieces prefer a free slot of their own type. The simulator shows piece-specific 3D models and matching graveyard-slot silhouettes, with one extra queen in reserve for each side.

### Command Types (`CommandAction`)

| Action | Meaning |
|--------|---------|
| `TRAVEL_MAGNET_OFF` | Move the carriage without a piece |
| `MAGNET_ON` | Engage the 12V electromagnet |
| `MOVE_PIECE_STRAIGHT` | Drag a piece along a straight line (orthogonal, diagonal, or any vector) |
| `MAGNET_OFF` | Release the piece |

### Algorithm (`MovementPlanner::planMove`)

1. **Capture**: the captured piece (also handles en passant via `capturedSquare`) is escorted to a free graveyard slot. Pawns go to the outer column, other pieces to the inner column, with a preference for a free slot marked for that piece type and a fallback to another free slot or the companion column. Among the available slots, the planner picks the one with the lowest total route cost.
2. **Routing** (`findRoute` / `planRoute`): captured pieces, retired pawns, reserve pieces and knights are routed over a half-square lattice that covers the board, both graveyards and the two **transit lanes**. A route never passes through a piece (or closer to one than a nudge can fix), so nothing is dragged through another piece. Cost = X/Y path length + 0.3 per turn + 0.8 per nudged neighbour per half-step + a 4x multiplier on 45° steps, so diagonals are used only when no axis-aligned route exists at a reasonable cost. The magnet stays on for the whole route.
3. **Primary move**: pawns, bishops, rooks, queens and kings travel in a straight line when nothing sits on that line (always true for a legal move, so bishops and queens still go diagonally). **Knights** always use `findRoute`: an X leg then a Y leg (or the reverse) when the L is clear, otherwise a short offset route that slides between neighbours, nudging them aside. If a straight line is ever blocked, the piece falls back to `findRoute` too.
4. **Edge nudging** (`planPointToPointWithEdgeNudging`): any piece within clearance of any leg of a path (`max(2r + margin, 0.58)` square units) is first nudged toward its own square edge or corner, up to `maxEdgeNudgeRatio`. After the move, nudged pieces are restored to their square centers in reverse order.
5. **Castling**: after the king moves, the rook moves from `rookFrom` to `rookTo` with the same routing, so it goes around the king instead of through it.
6. **Promotion**: the pawn first travels to the promotion square. It is then routed to the cheapest pawn graveyard slot, and the closest matching reserve piece (queen, rook, bishop or knight; default queen) is carried to the promotion square, via the transit lanes when that is cheaper.

### Configuration (`DimensionsConfig`)

| Field | Default | Description |
|-------|---------|-------------|
| `squareSizeMm` | 50 | Size of one square |
| `pieceRadiusMm` | 14 | Piece base radius |
| `safetyMarginMm` | 3 | Extra clearance between pieces |
| `maxEdgeNudgeRatio` | 0.34 | Max nudge offset from square center, in square units |
| `gantryTravelSpeedMmS` | 80 | Carriage speed without a piece |
| `gantryCarrySpeedMmS` | 45 | Carriage speed carrying a piece |

## Firmware Usage

```cpp
#include "movement_planner.hpp"
using namespace project64;

MovementPlanner planner;                 // or MovementPlanner(DimensionsConfig{...})
std::vector<PieceState> pieces = /* current board and reserve state */;

MoveRequest mv;
mv.from = {6, 0};                        // g1
mv.to   = {5, 2};                        // f3

std::vector<GantryCommand> cmds = planner.planMove(pieces, mv);
for (const auto& c : cmds) {
    // Dispatch to the stepper/magnet drivers:
    // c.action, c.from, c.to (square units), c.description
}
```

`planMove` updates `pieces` (squares and sub-square positions) as it plans, so the state stays consistent between moves. It returns an empty list if no piece is on `move.from`.

`MoveRequest` fields: `from`, `to`, `isCapture`, `capturedSquare`, `isPromotion`, `promotionType`, `isCastling`, `rookFrom`, `rookTo`.

### Building

The planner uses C++17 and has no dependencies beyond the standard library. Add both files in `movement_algorithm\firmware\` to your ESP32 project (ESP-IDF or Arduino), or compile on a desktop from the project root:

```powershell
g++ -std=c++17 -c movement_algorithm\firmware\movement_planner.cpp -o movement_planner.o
```

## Limitations

- The planner does not validate chess legality. The simulator includes a rules engine that only allows legal moves; firmware needs its own rules engine (or the host app) in front of `planMove`.
- Nudging is a single pass: pieces already at an edge, or crowded neighbors, may leave less clearance than expected.
- Firmware promotion needs the desired piece to already be in the reserve columns, otherwise the delivery step is skipped. The simulator starts with one extra queen per side; selecting another promotion type creates the replacement piece when needed.
- Coordinates are in square units. Convert to mm (`× squareSizeMm`) and to motor steps in your motion layer.
