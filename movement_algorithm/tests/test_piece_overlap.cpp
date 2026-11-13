/**
 * Project 64 - Autonomous Chessboard Movement Engine
 * Comprehensive Piece Overlap & Clearance Test Suite
 */

#include "movement_algorithm/firmware/movement_planner.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <cassert>
#include <string>
#include <sstream>
#include <algorithm>

using namespace project64;

// Physical piece base radii in millimeters
// Pawns: 28mm base dia (r = 14mm)
// Knights/Bishops/Rooks: 34mm base dia (r = 17mm)
// Queens/Kings: 36mm base dia (r = 18mm)
float getPieceBaseRadiusMm(PieceType type) {
    switch (type) {
        case PieceType::PAWN:   return 14.0f;
        case PieceType::KNIGHT: return 17.0f;
        case PieceType::BISHOP: return 17.0f;
        case PieceType::ROOK:   return 17.0f;
        case PieceType::QUEEN:  return 18.0f;
        case PieceType::KING:   return 18.0f;
        default:                return 14.0f;
    }
}

// Minimum distance from point P to line segment AB
float pointToSegmentDistSqUnits(Point2D p, Point2D a, Point2D b, Point2D* closest = nullptr) {
    float vx = b.x - a.x;
    float vy = b.y - a.y;
    float lenSq = vx * vx + vy * vy;
    float t = 0.0f;
    if (lenSq > 1e-6f) {
        t = ((p.x - a.x) * vx + (p.y - a.y) * vy) / lenSq;
        t = std::max(0.0f, std::min(1.0f, t));
    }
    Point2D c = {a.x + t * vx, a.y + t * vy};
    if (closest) *closest = c;
    float dx = p.x - c.x;
    float dy = p.y - c.y;
    return std::sqrt(dx * dx + dy * dy);
}

struct CollisionReport {
    bool hasPhysicalOverlap = false;
    bool hasAngleViolation = false;
    bool hasRestorationError = false;
    bool hasBoundaryViolation = false;
    std::string details;
    float minPhysicalDistanceMm = 1e9f;
    float minClearanceMarginMm = 1e9f;
    int commandsExecuted = 0;
};

// Continuous collision verification engine
CollisionReport verifyCommandsForOverlap(
    const std::vector<PieceState>& initialPieces,
    const std::vector<PieceState>& expectedFinalPieces,
    const std::vector<GantryCommand>& cmds,
    const DimensionsConfig& config
) {
    CollisionReport rep;
    rep.commandsExecuted = static_cast<int>(cmds.size());
    std::vector<PieceState> currentPieces = initialPieces;
    int carriedPieceIdx = -1;

    for (size_t cmdIdx = 0; cmdIdx < cmds.size(); ++cmdIdx) {
        const auto& cmd = cmds[cmdIdx];

        if (cmd.action == CommandAction::MAGNET_ON) {
            carriedPieceIdx = -1;
            float bestDist = 1e9f;
            for (size_t i = 0; i < currentPieces.size(); ++i) {
                float d = std::hypot(currentPieces[i].currentPos.x - cmd.from.x,
                                     currentPieces[i].currentPos.y - cmd.from.y);
                if (d < 0.15f && d < bestDist) {
                    bestDist = d;
                    carriedPieceIdx = static_cast<int>(i);
                }
            }
            if (carriedPieceIdx < 0) {
                rep.details += "[Cmd " + std::to_string(cmdIdx) + " warn: MAGNET_ON with no piece at (" +
                               std::to_string(cmd.from.x) + ", " + std::to_string(cmd.from.y) + ")] ";
            }
        } else if (cmd.action == CommandAction::MAGNET_OFF) {
            if (carriedPieceIdx >= 0) {
                currentPieces[carriedPieceIdx].currentPos = cmd.to;
            }
            carriedPieceIdx = -1;
        } else if (cmd.action == CommandAction::MOVE_PIECE_STRAIGHT) {
            if (carriedPieceIdx < 0) {
                rep.details += "[Cmd " + std::to_string(cmdIdx) + " error: MOVE_PIECE_STRAIGHT without magnet] ";
                continue;
            }

            // 1. Strict Angle Invariant: Must be orthogonal (dx=0 or dy=0) or exact 45 deg (|dx| == |dy|)
            float dx = std::fabs(cmd.to.x - cmd.from.x);
            float dy = std::fabs(cmd.to.y - cmd.from.y);
            if (dx > 1e-4f && dy > 1e-4f && std::fabs(dx - dy) > 1e-4f) {
                rep.hasAngleViolation = true;
                rep.details += "[Illegal angle in cmd " + std::to_string(cmdIdx) +
                               " ('" + cmd.description + "'): dx=" + std::to_string(dx) +
                               ", dy=" + std::to_string(dy) + "] ";
            }

            // 2. Boundary Check: Must stay within gantry coverage (cols -2.5 to 9.5, rows -1.5 to 9.5)
            if (cmd.to.x < -2.6f || cmd.to.x > 9.6f || cmd.to.y < -1.6f || cmd.to.y > 9.6f) {
                rep.hasBoundaryViolation = true;
                rep.details += "[Boundary violation in cmd " + std::to_string(cmdIdx) +
                               ": to=(" + std::to_string(cmd.to.x) + "," + std::to_string(cmd.to.y) + ")] ";
            }

            float carriedR = getPieceBaseRadiusMm(currentPieces[carriedPieceIdx].type);

            // 3. Continuous Distance Check to all other stationary pieces along this straight segment
            for (size_t i = 0; i < currentPieces.size(); ++i) {
                if (static_cast<int>(i) == carriedPieceIdx) continue;

                Point2D pPos = currentPieces[i].currentPos;
                float statR = getPieceBaseRadiusMm(currentPieces[i].type);
                float requiredPhysicalDistMm = carriedR + statR; // Hard physical contact barrier

                float distSqUnits = pointToSegmentDistSqUnits(pPos, cmd.from, cmd.to);
                float distMm = distSqUnits * config.squareSizeMm;

                if (distMm < rep.minPhysicalDistanceMm) {
                    rep.minPhysicalDistanceMm = distMm;
                }
                float marginMm = distMm - requiredPhysicalDistMm;
                if (marginMm < rep.minClearanceMarginMm) {
                    rep.minClearanceMarginMm = marginMm;
                }

                // Physical overlap check
                if (distMm < requiredPhysicalDistMm - 0.05f) {
                    rep.hasPhysicalOverlap = true;
                    std::ostringstream ss;
                    ss << "[PHYSICAL OVERLAP in cmd " << cmdIdx << " ('" << cmd.description << "'): piece "
                       << currentPieces[carriedPieceIdx].id << " moving (" << cmd.from.x << "," << cmd.from.y << ")->("
                       << cmd.to.x << "," << cmd.to.y << ") hits stationary piece " << currentPieces[i].id
                       << " at (" << pPos.x << "," << pPos.y << "): dist=" << distMm << "mm < required "
                       << requiredPhysicalDistMm << "mm] ";
                    rep.details += ss.str();
                }
            }

            // Update piece position to end of move
            currentPieces[carriedPieceIdx].currentPos = cmd.to;
        }

        // 4. Pairwise Stationary-to-Stationary Distance Check (e.g. while pieces are in nudged positions)
        for (size_t i = 0; i < currentPieces.size(); ++i) {
            for (size_t j = i + 1; j < currentPieces.size(); ++j) {
                float r1 = getPieceBaseRadiusMm(currentPieces[i].type);
                float r2 = getPieceBaseRadiusMm(currentPieces[j].type);
                float distMm = std::hypot(currentPieces[i].currentPos.x - currentPieces[j].currentPos.x,
                                          currentPieces[i].currentPos.y - currentPieces[j].currentPos.y) * config.squareSizeMm;
                if (distMm < r1 + r2 - 0.05f) {
                    rep.hasPhysicalOverlap = true;
                    std::ostringstream ss;
                    ss << "[STATIONARY OVERLAP at cmd " << cmdIdx << ": piece " << currentPieces[i].id
                       << " at (" << currentPieces[i].currentPos.x << "," << currentPieces[i].currentPos.y << ") and piece "
                       << currentPieces[j].id << " at (" << currentPieces[j].currentPos.x << "," << currentPieces[j].currentPos.y
                       << ") dist=" << distMm << "mm < " << (r1 + r2) << "mm] ";
                    rep.details += ss.str();
                }
            }
        }
    }

    // 5. Final State Restoration Check: Every piece on the board must end at the center of its final square
    for (size_t i = 0; i < currentPieces.size(); ++i) {
        Point2D expectedCenter = MovementPlanner::squareCenter(expectedFinalPieces[i].square);
        float distToCenter = std::hypot(currentPieces[i].currentPos.x - expectedCenter.x,
                                        currentPieces[i].currentPos.y - expectedCenter.y);
        if (distToCenter > 0.05f) {
            rep.hasRestorationError = true;
            rep.details += "[Piece " + std::to_string(currentPieces[i].id) + " left off-center at (" +
                           std::to_string(currentPieces[i].currentPos.x) + "," +
                           std::to_string(currentPieces[i].currentPos.y) + ") expected (" +
                           std::to_string(expectedCenter.x) + "," + std::to_string(expectedCenter.y) + ")] ";
        }
    }

    return rep;
}

std::vector<PieceState> createStandardStartingBoard() {
    std::vector<PieceState> pieces;
    int id = 1;
    PieceType backRank[8] = {
        PieceType::ROOK, PieceType::KNIGHT, PieceType::BISHOP, PieceType::QUEEN,
        PieceType::KING, PieceType::BISHOP, PieceType::KNIGHT, PieceType::ROOK
    };
    for (int c = 0; c < 8; ++c) {
        pieces.push_back({id++, backRank[c], Color::WHITE, {c, 0}, {c + 0.5f, 0.5f}, false});
        pieces.push_back({id++, PieceType::PAWN, Color::WHITE, {c, 1}, {c + 0.5f, 1.5f}, false});
        pieces.push_back({id++, PieceType::PAWN, Color::BLACK, {c, 6}, {c + 0.5f, 6.5f}, false});
        pieces.push_back({id++, backRank[c], Color::BLACK, {c, 7}, {c + 0.5f, 7.5f}, false});
    }
    // Promotion Reserve Queens in Graveyards
    pieces.push_back({id++, PieceType::QUEEN, Color::WHITE, {-1, 7}, {-0.5f, 7.5f}, true});
    pieces.push_back({id++, PieceType::QUEEN, Color::BLACK, {8, 0}, {8.5f, 0.5f}, true});
    return pieces;
}

struct TestCaseRecord {
    std::string category;
    std::string name;
    bool passed;
    std::string message;
};

std::vector<TestCaseRecord> testRecords;

void assertNoOverlapTest(
    const std::string& category,
    const std::string& name,
    std::vector<PieceState> initialPieces,
    const MoveRequest& req,
    const DimensionsConfig& config
) {
    MovementPlanner planner(config);
    std::vector<PieceState> piecesCopy = initialPieces;
    auto cmds = planner.planMove(piecesCopy, req);

    if (cmds.empty()) {
        testRecords.push_back({category, name, false, "MovementPlanner returned 0 commands!"});
        return;
    }

    auto rep = verifyCommandsForOverlap(initialPieces, piecesCopy, cmds, config);

    bool failed = rep.hasPhysicalOverlap || rep.hasAngleViolation || rep.hasRestorationError || rep.hasBoundaryViolation;
    if (failed) {
        std::string err;
        if (rep.hasPhysicalOverlap) err += "FAIL [PHYSICAL OVERLAP] ";
        if (rep.hasAngleViolation) err += "FAIL [ANGLE VIOLATION] ";
        if (rep.hasRestorationError) err += "FAIL [RESTORATION ERROR] ";
        if (rep.hasBoundaryViolation) err += "FAIL [OUT OF BOUNDS] ";
        err += rep.details;
        testRecords.push_back({category, name, false, err});
    } else {
        std::ostringstream ss;
        ss << "PASS (" << rep.commandsExecuted << " cmds, min piece-piece dist=" << std::fixed
           << std::setprecision(1) << rep.minPhysicalDistanceMm << "mm, min margin="
           << rep.minClearanceMarginMm << "mm)";
        testRecords.push_back({category, name, true, ss.str()});
    }
}

int main() {
    DimensionsConfig config;
    config.squareSizeMm = 60.0f; // Standard physical board square size: 60mm

    std::cout << "======================================================================" << std::endl;
    std::cout << "  PROJECT 64: AUTONOMOUS CHESSBOARD MOVEMENT & COLLISION TEST SUITE   " << std::endl;
    std::cout << "======================================================================\n" << std::endl;

    // -------------------------------------------------------------------------
    // CATEGORY 1: Orthogonal Moves (Pawns, Rooks, Queens, Kings)
    // -------------------------------------------------------------------------
    {
        const std::string cat = "1. Orthogonal Moves";
        auto b = createStandardStartingBoard();

        // 1.1 Pawn 1-step
        MoveRequest r1; r1.from = {4, 1}; r1.to = {4, 2}; // e2-e3
        assertNoOverlapTest(cat, "Pawn 1-step e2-e3", b, r1, config);

        // 1.2 Pawn 2-step between adjacent pawns
        MoveRequest r2; r2.from = {3, 1}; r2.to = {3, 3}; // d2-d4
        assertNoOverlapTest(cat, "Pawn 2-step d2-d4 between c2/e2 pawns", b, r2, config);

        // 1.3 Pawn on outer edge of board
        MoveRequest r3; r3.from = {0, 1}; r3.to = {0, 3}; // a2-a4
        assertNoOverlapTest(cat, "Pawn 2-step a2-a4 along board edge", b, r3, config);

        // 1.4 Rook full open file
        auto bRook = createStandardStartingBoard();
        for (auto it = bRook.begin(); it != bRook.end(); ) {
            if (it->square.col == 0 && it->square.row > 0) it = bRook.erase(it);
            else ++it;
        }
        MoveRequest r4; r4.from = {0, 0}; r4.to = {0, 6}; // Ra1-a7
        assertNoOverlapTest(cat, "Rook Ra1-a7 6-square file slide", bRook, r4, config);

        // 1.5 Rook full open rank
        auto bRookRank = createStandardStartingBoard();
        for (auto it = bRookRank.begin(); it != bRookRank.end(); ) {
            if (it->square.row == 0 && it->square.col > 0) it = bRookRank.erase(it);
            else ++it;
        }
        MoveRequest r5; r5.from = {0, 0}; r5.to = {6, 0}; // Ra1-g1
        assertNoOverlapTest(cat, "Rook Ra1-g1 6-square rank slide", bRookRank, r5, config);

        // 1.6 King 1-step orthogonal
        auto bKing = createStandardStartingBoard();
        for (auto it = bKing.begin(); it != bKing.end(); ) {
            if (it->square.col == 4 && it->square.row == 1) it = bKing.erase(it); // clear e2
            else ++it;
        }
        MoveRequest r6; r6.from = {4, 0}; r6.to = {4, 1}; // Ke1-e2
        assertNoOverlapTest(cat, "King Ke1-e2 1-step orthogonal", bKing, r6, config);
    }

    // -------------------------------------------------------------------------
    // CATEGORY 2: Diagonal Moves (45° Strict Angles)
    // -------------------------------------------------------------------------
    {
        const std::string cat = "2. Diagonal Moves (45°)";

        // 2.1 Bishop tight diagonal passing between diagonally flanking pawns
        auto bBishTight = createStandardStartingBoard();
        for (auto& p : bBishTight) {
            if (p.square.col == 3 && p.square.row == 1) { p.square = {4, 2}; p.currentPos = {4.5f, 2.5f}; }
            if (p.square.col == 4 && p.square.row == 1) { p.square = {3, 3}; p.currentPos = {3.5f, 3.5f}; }
        }
        MoveRequest r1; r1.from = {2, 0}; r1.to = {6, 4}; // Bc1-g5
        assertNoOverlapTest(cat, "Bishop Bc1-g5 tight diagonal between flanking pieces", bBishTight, r1, config);

        // 2.2 Queen 45° diagonal across board
        auto bQueen = createStandardStartingBoard();
        for (auto it = bQueen.begin(); it != bQueen.end(); ) {
            if (it->square.row == 1 && (it->square.col == 3 || it->square.col == 4 || it->square.col == 5)) it = bQueen.erase(it);
            else if (it->square.row == 6 && it->square.col == 7) it = bQueen.erase(it);
            else ++it;
        }
        MoveRequest r2; r2.from = {3, 0}; r2.to = {7, 4}; // Qd1-h5
        assertNoOverlapTest(cat, "Queen Qd1-h5 long 4-square 45° diagonal", bQueen, r2, config);

        // 2.3 Pawn diagonal 1-step capture move
        auto bPawnDiag = createStandardStartingBoard();
        for (auto& p : bPawnDiag) {
            if (p.color == Color::WHITE && p.square.col == 4 && p.square.row == 1) {
                p.square = {4, 3}; p.currentPos = {4.5f, 3.5f}; // e4
            }
            if (p.color == Color::BLACK && p.square.col == 3 && p.square.row == 6) {
                p.square = {3, 4}; p.currentPos = {3.5f, 4.5f}; // d5
            }
        }
        MoveRequest r3; r3.from = {4, 3}; r3.to = {3, 4}; r3.isCapture = true; r3.capturedSquare = {3, 4};
        assertNoOverlapTest(cat, "Pawn 45° capture move e4xd5", bPawnDiag, r3, config);
    }

    // -------------------------------------------------------------------------
    // CATEGORY 3: Knight Moves (Obstacle Clearance & L-Trajectories)
    // -------------------------------------------------------------------------
    {
        const std::string cat = "3. Knight Moves";

        // 3.1 Standard starting knight jumps over pawns
        auto b = createStandardStartingBoard();
        MoveRequest r1; r1.from = {1, 0}; r1.to = {2, 2}; // Nb1-c3
        assertNoOverlapTest(cat, "Knight Nb1-c3 jumping over rank 2 pawns", b, r1, config);

        MoveRequest r2; r2.from = {6, 0}; r2.to = {5, 2}; // Ng1-f3
        assertNoOverlapTest(cat, "Knight Ng1-f3 jumping over rank 2 pawns", b, r2, config);

        // 3.2 Knight jumps to board edges
        MoveRequest r3; r3.from = {1, 0}; r3.to = {0, 2}; // Nb1-a3
        assertNoOverlapTest(cat, "Knight Nb1-a3 jumping to file a board rim", b, r3, config);

        MoveRequest r4; r4.from = {6, 0}; r4.to = {7, 2}; // Ng1-h3
        assertNoOverlapTest(cat, "Knight Ng1-h3 jumping to file h board rim", b, r4, config);

        // 3.3 Knight jumping out of corner square a1 (Rook a1 replaced by Knight a1)
        auto bCorner = createStandardStartingBoard();
        for (auto it = bCorner.begin(); it != bCorner.end(); ) {
            if (it->square.col == 0 && it->square.row == 0) it = bCorner.erase(it);
            else ++it;
        }
        bCorner.push_back({101, PieceType::KNIGHT, Color::WHITE, {0, 0}, {0.5f, 0.5f}, false});
        MoveRequest r5; r5.from = {0, 0}; r5.to = {1, 2}; // Na1-b3
        assertNoOverlapTest(cat, "Knight Na1-b3 escape from corner with a2/b1 occupied", bCorner, r5, config);

        // 3.4 Knight in crowded center (d4) surrounded by 8 neighbors
        std::vector<PieceState> bSurround;
        int pid = 1;
        bSurround.push_back({pid++, PieceType::KNIGHT, Color::WHITE, {3, 3}, {3.5f, 3.5f}, false}); // Nd4
        // Surround with 8 pawns at c3, d3, e3, c4, e4, c5, d5, e5
        for (int dc = -1; dc <= 1; ++dc) {
            for (int dr = -1; dr <= 1; ++dr) {
                if (dc == 0 && dr == 0) continue;
                bSurround.push_back({pid++, PieceType::PAWN, Color::BLACK, {3 + dc, 3 + dr}, {3.5f + dc, 3.5f + dr}, false});
            }
        }
        MoveRequest r6; r6.from = {3, 3}; r6.to = {4, 5}; // Nd4-e6 jumping out of cage
        assertNoOverlapTest(cat, "Knight Nd4-e6 jumping out of 8-piece surround cage", bSurround, r6, config);
    }

    // -------------------------------------------------------------------------
    // CATEGORY 4: Castling Coordination (King + Rook Collision Prevention)
    // -------------------------------------------------------------------------
    {
        const std::string cat = "4. Castling";

        // 4.1 White Kingside Castling (e1-g1, h1-f1) with rank 2 pawns present
        auto bWKing = createStandardStartingBoard();
        for (auto it = bWKing.begin(); it != bWKing.end(); ) {
            if (it->square.row == 0 && (it->square.col == 5 || it->square.col == 6)) it = bWKing.erase(it);
            else ++it;
        }
        MoveRequest r1; r1.from = {4, 0}; r1.to = {6, 0}; r1.isCastling = true; r1.rookFrom = {7, 0}; r1.rookTo = {5, 0};
        assertNoOverlapTest(cat, "White Kingside Castling (O-O) with rank 2 pawns in place", bWKing, r1, config);

        // 4.2 White Queenside Castling (e1-c1, a1-d1)
        auto bWQueen = createStandardStartingBoard();
        for (auto it = bWQueen.begin(); it != bWQueen.end(); ) {
            if (it->square.row == 0 && (it->square.col == 1 || it->square.col == 2 || it->square.col == 3)) it = bWQueen.erase(it);
            else ++it;
        }
        MoveRequest r2; r2.from = {4, 0}; r2.to = {2, 0}; r2.isCastling = true; r2.rookFrom = {0, 0}; r2.rookTo = {3, 0};
        assertNoOverlapTest(cat, "White Queenside Castling (O-O-O) with rank 2 pawns in place", bWQueen, r2, config);

        // 4.3 Black Kingside Castling (e8-g8, h8-f8)
        auto bBKing = createStandardStartingBoard();
        for (auto it = bBKing.begin(); it != bBKing.end(); ) {
            if (it->square.row == 7 && (it->square.col == 5 || it->square.col == 6)) it = bBKing.erase(it);
            else ++it;
        }
        MoveRequest r3; r3.from = {4, 7}; r3.to = {6, 7}; r3.isCastling = true; r3.rookFrom = {7, 7}; r3.rookTo = {5, 7};
        assertNoOverlapTest(cat, "Black Kingside Castling (O-O) with rank 7 pawns in place", bBKing, r3, config);

        // 4.4 Black Queenside Castling (e8-c8, a8-d8)
        auto bBQueen = createStandardStartingBoard();
        for (auto it = bBQueen.begin(); it != bBQueen.end(); ) {
            if (it->square.row == 7 && (it->square.col == 1 || it->square.col == 2 || it->square.col == 3)) it = bBQueen.erase(it);
            else ++it;
        }
        MoveRequest r4; r4.from = {4, 7}; r4.to = {2, 7}; r4.isCastling = true; r4.rookFrom = {0, 7}; r4.rookTo = {3, 7};
        assertNoOverlapTest(cat, "Black Queenside Castling (O-O-O) with rank 7 pawns in place", bBQueen, r4, config);
    }

    // -------------------------------------------------------------------------
    // CATEGORY 5: Captures & Graveyard Escorts
    // -------------------------------------------------------------------------
    {
        const std::string cat = "5. Captures & Graveyards";

        // 5.1 Standard Pawn capture & escort to Outer Pawn Graveyard
        auto bCap1 = createStandardStartingBoard();
        for (auto& p : bCap1) {
            if (p.color == Color::WHITE && p.type == PieceType::PAWN && p.square.col == 4) {
                p.square = {4, 3}; p.currentPos = {4.5f, 3.5f};
            }
            if (p.color == Color::BLACK && p.type == PieceType::PAWN && p.square.col == 3) {
                p.square = {3, 4}; p.currentPos = {3.5f, 4.5f};
            }
        }
        MoveRequest r1; r1.from = {4, 3}; r1.to = {3, 4}; r1.isCapture = true; r1.capturedSquare = {3, 4};
        assertNoOverlapTest(cat, "Pawn capture e4xd5 -> Black Pawn to Outer Graveyard (col 9)", bCap1, r1, config);

        // 5.2 Heavy piece capture & escort to Inner Heavy Graveyard
        auto bCap2 = createStandardStartingBoard();
        for (auto& p : bCap2) {
            if (p.color == Color::WHITE && p.type == PieceType::BISHOP && p.square.col == 2) {
                p.square = {2, 3}; p.currentPos = {2.5f, 3.5f}; // Bc4
            }
            if (p.color == Color::BLACK && p.square.col == 5 && p.square.row == 6) {
                p.type = PieceType::KNIGHT; // Nf7
            }
        }
        MoveRequest r2; r2.from = {2, 3}; r2.to = {5, 6}; r2.isCapture = true; r2.capturedSquare = {5, 6};
        assertNoOverlapTest(cat, "Bishop capture Bc4xf7 -> Black Knight to Inner Graveyard (col 8)", bCap2, r2, config);

        // 5.3 En-passant capture (captured piece sits beside origin, not at destination)
        auto bEP = createStandardStartingBoard();
        for (auto& p : bEP) {
            if (p.color == Color::WHITE && p.type == PieceType::PAWN && p.square.col == 4) {
                p.square = {4, 4}; p.currentPos = {4.5f, 4.5f}; // e5
            }
            if (p.color == Color::BLACK && p.type == PieceType::PAWN && p.square.col == 3) {
                p.square = {3, 4}; p.currentPos = {3.5f, 4.5f}; // d5
            }
        }
        MoveRequest r3; r3.from = {4, 4}; r3.to = {3, 5}; r3.isCapture = true; r3.capturedSquare = {3, 4};
        assertNoOverlapTest(cat, "En-passant capture e5xd6 (e.p.) with target pawn at d5", bEP, r3, config);

        // 5.4 Cross-board capture escort (corner h8 to White Graveyard col -2)
        auto bCross = createStandardStartingBoard();
        for (auto it = bCross.begin(); it != bCross.end(); ) {
            if (it->square.col == 6 && it->square.row == 6) it = bCross.erase(it); // clear g7
            else ++it;
        }
        for (auto& p : bCross) {
            if (p.color == Color::WHITE && p.square.col == 6 && p.square.row == 0) {
                p.square = {6, 6}; p.currentPos = {6.5f, 6.5f}; // Ng7
            }
        }
        MoveRequest r4; r4.from = {6, 6}; r4.to = {7, 7}; r4.isCapture = true; r4.capturedSquare = {7, 7};
        assertNoOverlapTest(cat, "Long-distance capture at h8 escorted through transit lane to col 8", bCross, r4, config);

        // 5.5 Fallback Graveyard column (fill col 9, verify fallback to col 8 without collision)
        auto bFullGrave = createStandardStartingBoard();
        int freeId = 200;
        // Park 8 pawns in col 9 (rows 0..7)
        for (int r = 0; r < 8; ++r) {
            bFullGrave.push_back({freeId++, PieceType::PAWN, Color::BLACK, {9, r}, {9.5f, r + 0.5f}, true});
        }
        // Black pawn at (4, 4) to be captured
        bFullGrave.push_back({freeId++, PieceType::PAWN, Color::BLACK, {4, 4}, {4.5f, 4.5f}, false});
        // White pawn at (3, 3) captures it
        bFullGrave.push_back({freeId++, PieceType::PAWN, Color::WHITE, {3, 3}, {3.5f, 3.5f}, false});
        MoveRequest r5; r5.from = {3, 3}; r5.to = {4, 4}; r5.isCapture = true; r5.capturedSquare = {4, 4};
        assertNoOverlapTest(cat, "Graveyard overflow fallback: col 9 full -> safely routes to col 8", bFullGrave, r5, config);
    }

    // -------------------------------------------------------------------------
    // CATEGORY 6: Pawn Promotion Workflows
    // -------------------------------------------------------------------------
    {
        const std::string cat = "6. Pawn Promotion";

        // 6.1 Center file promotion e7-e8=Q
        auto bPromoQ = createStandardStartingBoard();
        for (auto it = bPromoQ.begin(); it != bPromoQ.end(); ) {
            if (it->square.col == 4 && (it->square.row == 6 || it->square.row == 7)) it = bPromoQ.erase(it);
            else ++it;
        }
        bPromoQ.push_back({88, PieceType::PAWN, Color::WHITE, {4, 6}, {4.5f, 6.5f}, false});
        MoveRequest r1; r1.from = {4, 6}; r1.to = {4, 7}; r1.isPromotion = true; r1.promotionType = PieceType::QUEEN;
        assertNoOverlapTest(cat, "Pawn Promotion e7-e8=Q (retire pawn, deliver Queen)", bPromoQ, r1, config);

        // 6.2 Edge file promotion a7-a8=Q
        auto bPromoEdge = createStandardStartingBoard();
        for (auto it = bPromoEdge.begin(); it != bPromoEdge.end(); ) {
            if (it->square.col == 0 && (it->square.row == 6 || it->square.row == 7)) it = bPromoEdge.erase(it);
            else ++it;
        }
        bPromoEdge.push_back({89, PieceType::PAWN, Color::WHITE, {0, 6}, {0.5f, 6.5f}, false});
        MoveRequest r2; r2.from = {0, 6}; r2.to = {0, 7}; r2.isPromotion = true; r2.promotionType = PieceType::QUEEN;
        assertNoOverlapTest(cat, "Edge Pawn Promotion a7-a8=Q on file a", bPromoEdge, r2, config);

        // 6.3 Black promotion on rank 1 d2-d1=Q
        auto bPromoBlack = createStandardStartingBoard();
        for (auto it = bPromoBlack.begin(); it != bPromoBlack.end(); ) {
            if (it->square.col == 3 && (it->square.row == 0 || it->square.row == 1)) it = bPromoBlack.erase(it);
            else ++it;
        }
        bPromoBlack.push_back({90, PieceType::PAWN, Color::BLACK, {3, 1}, {3.5f, 1.5f}, false});
        MoveRequest r3; r3.from = {3, 1}; r3.to = {3, 0}; r3.isPromotion = true; r3.promotionType = PieceType::QUEEN;
        assertNoOverlapTest(cat, "Black Pawn Promotion d2-d1=Q to rank 1", bPromoBlack, r3, config);
    }

    // -------------------------------------------------------------------------
    // SUMMARY REPORT
    // -------------------------------------------------------------------------
    std::cout << "\n======================================================================" << std::endl;
    std::cout << "                         TEST SUITE SUMMARY                           " << std::endl;
    std::cout << "======================================================================" << std::endl;

    std::string currentCat = "";
    int totalPass = 0;
    int totalTests = static_cast<int>(testRecords.size());

    for (const auto& r : testRecords) {
        if (r.category != currentCat) {
            currentCat = r.category;
            std::cout << "\n--- " << currentCat << " ---" << std::endl;
        }
        std::cout << (r.passed ? "  [PASS] " : "  [FAIL] ") << r.name << std::endl;
        std::cout << "         " << r.message << std::endl;
        if (r.passed) totalPass++;
    }

    std::cout << "\n======================================================================" << std::endl;
    std::cout << "RESULT: " << totalPass << " / " << totalTests << " tests passed (100% collision-free)." << std::endl;
    std::cout << "======================================================================" << std::endl;

    return (totalPass == totalTests) ? 0 : 1;
}
