#pragma once
/**
 * Project 64 - Autonomous Chessboard Movement & Pathfinding Engine
 * Target: ESP32 Core 1 (State Machine, Pathfinding/Collision, Kinematics)
 *
 * Transit lanes (always empty, used to carry pieces around the board):
 *     Front lane: row = -1 (center y = -0.5), Back lane: row = 8 (center y = 8.5), spanning col -2..9
 *
 * Coordinate System:
 * - Playable 8x8 grid: col in [0..7] (files a..h), row in [0..7] (ranks 1..8)
 * - Square (col, row) center in square-units: (col + 0.5f, row + 0.5f)
 * - Graveyard / Reserve zones on the sides of the board (16 slots per side):
 *     Left Side  (White Reserve / Captured Black):
 *       col = -2 : Outer edge (8 Pawn slots, rows 0..7)
 *       col = -1 : Inner edge (8 Heavy piece slots, rows 0..7)
 *     Right Side (Black Reserve / Captured White):
 *       col =  8 : Inner edge (8 Heavy piece slots, rows 0..7)
 *       col =  9 : Outer edge (8 Pawn slots, rows 0..7)
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace project64 {

enum class PieceType : uint8_t {
    NONE = 0,
    PAWN,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
    KING
};

enum class Color : uint8_t {
    WHITE = 0,
    BLACK = 1
};

struct DimensionsConfig {
    float squareSizeMm = 50.0f;       // Width/height of one chess square (mm)
    float pieceRadiusMm = 14.0f;      // Physical base radius of a chess piece (mm)
    float safetyMarginMm = 3.0f;      // Extra clearance margin between moving pieces (mm)
    float maxEdgeNudgeRatio = 0.34f;  // Max offset from square center (in square units, < 0.5 - r/S)
    float gantryTravelSpeedMmS = 80.0f;
    float gantryCarrySpeedMmS = 45.0f;
};

struct Point2D {
    float x; // in square units (multiply by squareSizeMm for physical mm)
    float y;
};

struct GridCoord {
    int col; // -2..9 (0..7 is playable board)
    int row; // 0..7
    bool operator==(const GridCoord& o) const { return col == o.col && row == o.row; }
    bool operator!=(const GridCoord& o) const { return !(*this == o); }
};

enum class CommandAction : uint8_t {
    TRAVEL_MAGNET_OFF = 0, // Move carriage without piece
    MAGNET_ON,             // Engage 12V electromagnet
    MOVE_PIECE_STRAIGHT,   // Move piece in straight line (orthogonal, diagonal, or arbitrary vector)
    MAGNET_OFF             // Release 12V electromagnet
};

struct GantryCommand {
    CommandAction action;
    Point2D from;
    Point2D to;
    const char* description;
};

struct PieceState {
    int id;
    PieceType type;
    Color color;
    GridCoord square;
    Point2D currentPos; // Current sub-square position (normally square center)
    bool isReserve;
};

struct MoveRequest {
    GridCoord from;
    GridCoord to;
    bool isCapture = false;
    GridCoord capturedSquare = {-99, -99}; // Handles standard capture & en-passant
    bool isPromotion = false;
    PieceType promotionType = PieceType::QUEEN;
    bool isCastling = false;
    GridCoord rookFrom = {-99, -99};
    GridCoord rookTo = {-99, -99};
};

class MovementPlanner {
public:
    static constexpr float kLaneFrontY = -0.5f;
    static constexpr float kLaneBackY = 8.5f;

    explicit MovementPlanner(const DimensionsConfig& config = DimensionsConfig())
        : config_(config) {}

    void setConfig(const DimensionsConfig& config) { config_ = config; }
    const DimensionsConfig& getConfig() const { return config_; }

    static Point2D squareCenter(GridCoord sq) {
        return {static_cast<float>(sq.col) + 0.5f, static_cast<float>(sq.row) + 0.5f};
    }

    /**
     * Plans a complete sequence of collision-free gantry & electromagnet commands
     * for any chess move (including Knights, Bishops, Captures, Castling, and Promotions).
     */
    std::vector<GantryCommand> planMove(
        std::vector<PieceState>& pieces,
        const MoveRequest& move
    );

private:
    DimensionsConfig config_;

    struct NudgedPieceRecord {
        int pieceIndex;
        Point2D originalCenter;
        Point2D nudgedPos;
    };

    float minCenterDistanceSqUnits() const {
        return (2.0f * config_.pieceRadiusMm + config_.safetyMarginMm) / config_.squareSizeMm;
    }

    // Distance from point P to segment AB, plus closest point on AB
    static float pointToSegmentDistance(Point2D p, Point2D a, Point2D b, Point2D* closestOut = nullptr);

    struct Route {
        float cost = 1e9f;
        bool reachable = true;
        std::vector<Point2D> waypoints; // interior points only; empty = one straight leg
    };

    struct GraveyardTrip {
        GridCoord slot;
        Route route;
    };

    // Radii (square units) around a path: pieces closer than `trigger` must be nudged; pieces closer than
    // `hard` cannot be cleared by a nudge of at most maxEdgeNudgeRatio, so the path may not go there.
    float triggerRadius() const { return std::max(minCenterDistanceSqUnits(), 0.58f); }
    float hardRadius() const { return std::max(triggerRadius() - config_.maxEdgeNudgeRatio, 0.05f) + 0.03f; }

    bool isStraightPathClear(
        const std::vector<PieceState>& pieces,
        int pieceIdx,
        Point2D dest
    ) const;

    // Cheapest route over a half-square lattice covering the board, graveyards and both transit lanes.
    // Never goes through a piece nudging cannot clear, prefers X/Y legs with few turns and few nudged
    // neighbours; 45-degree steps carry a heavy penalty so they are used only when necessary.
    // waypoints are the interior turning points; `reachable` is false when no route exists.
    Route findRoute(
        const std::vector<PieceState>& pieces,
        int pieceIdx,
        Point2D dest
    ) const;

    // Line movers (pawn, bishop, rook, queen, king) go straight when nothing sits on the line
    // (allowStraight); knights, escorted pieces and blocked moves use findRoute.
    Route planRoute(
        const std::vector<PieceState>& pieces,
        int pieceIdx,
        Point2D dest,
        bool allowStraight
    ) const;
    // Picks the free graveyard slot (and route) with the lowest total cost
    GraveyardTrip chooseGraveyardTrip(
        const std::vector<PieceState>& pieces,
        int pieceIdx
    ) const;

    // Moves a single piece from its current position to destPt along `waypoints` (empty = straight line),
    // automatically nudging blocking pieces to the safe edge/corner of their squares and restoring them
    // once the piece passes.
    void planPointToPointWithEdgeNudging(
        std::vector<PieceState>& pieces,
        int movingPieceIdx,
        Point2D destPt,
        std::vector<GantryCommand>& cmds,
        Point2D& carriagePos,
        const char* moveLabel,
        const std::vector<Point2D>& waypoints = {}
    );

    // Free slots of the 16-spot side graveyard, nearest to `nearRow` first:
    // Pawns -> outer edge (col -2 or 9), Heavy pieces -> inner edge (col -1 or 8).
    // Falls back to the companion column when the primary column is full.
    std::vector<GridCoord> freeGraveyardSlots(
        const std::vector<PieceState>& pieces,
        Color pieceColor,
        PieceType type,
        float nearRow
    ) const;

    // Finds the promotion replacement piece in the reserve columns closest to the target point
    int findPromotionPieceInReserve(
        const std::vector<PieceState>& pieces,
        Color color,
        PieceType desiredType,
        Point2D target
    ) const;
};

} // namespace project64
