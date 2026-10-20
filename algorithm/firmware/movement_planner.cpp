#include "movement_planner.hpp"
#include <algorithm>
#include <cmath>

namespace project64 {

float MovementPlanner::pointToSegmentDistance(Point2D p, Point2D a, Point2D b, Point2D* closestOut) {
    float vx = b.x - a.x;
    float vy = b.y - a.y;
    float lenSq = vx * vx + vy * vy;
    float t = 0.0f;
    if (lenSq > 1e-6f) {
        t = ((p.x - a.x) * vx + (p.y - a.y) * vy) / lenSq;
        t = std::max(0.0f, std::min(1.0f, t));
    }
    Point2D closest = {a.x + t * vx, a.y + t * vy};
    if (closestOut) {
        *closestOut = closest;
    }
    float dx = p.x - closest.x;
    float dy = p.y - closest.y;
    return std::sqrt(dx * dx + dy * dy);
}

GridCoord MovementPlanner::findAvailableGraveyardSlot(
    const std::vector<PieceState>& pieces,
    Color pieceColor,
    PieceType type
) const {
    // 16 spots per side:
    // Pawns go on the outer edge (col -2 on left, col 9 on right)
    // Heavy pieces go on the inside column (col -1 on left, col 8 on right)
    bool isPawn = (type == PieceType::PAWN);
    int targetCol;
    if (pieceColor == Color::WHITE) {
        targetCol = isPawn ? -2 : -1;
    } else {
        targetCol = isPawn ? 9 : 8;
    }

    for (int r = 0; r < 8; ++r) {
        GridCoord candidate = {targetCol, r};
        bool occupied = false;
        for (const auto& p : pieces) {
            if (p.square == candidate) {
                occupied = true;
                break;
            }
        }
        if (!occupied) {
            return candidate;
        }
    }
    // Fallback to the companion column if primary column is full
    int fallbackCol = (pieceColor == Color::WHITE) ? (isPawn ? -1 : -2) : (isPawn ? 8 : 9);
    for (int r = 0; r < 8; ++r) {
        GridCoord candidate = {fallbackCol, r};
        bool occupied = false;
        for (const auto& p : pieces) {
            if (p.square == candidate) {
                occupied = true;
                break;
            }
        }
        if (!occupied) {
            return candidate;
        }
    }
    return {targetCol, 0};
}

int MovementPlanner::findPromotionPieceInReserve(
    const std::vector<PieceState>& pieces,
    Color color,
    PieceType desiredType
) const {
    // Look in the inner heavy-piece column first (-1 for White, 8 for Black)
    for (size_t i = 0; i < pieces.size(); ++i) {
        const auto& p = pieces[i];
        if (p.color == color && p.type == desiredType && (p.square.col < 0 || p.square.col > 7)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void MovementPlanner::planPointToPointWithEdgeNudging(
    std::vector<PieceState>& pieces,
    int movingPieceIdx,
    Point2D destPt,
    std::vector<GantryCommand>& cmds,
    Point2D& carriagePos,
    const char* moveLabel
) {
    Point2D startPt = pieces[movingPieceIdx].currentPos;
    float minClearance = minCenterDistanceSqUnits();
    float maxNudge = config_.maxEdgeNudgeRatio;

    // 1. Identify any pieces whose position is within minClearance of the straight-line segment
    std::vector<NudgedPieceRecord> nudgedList;

    float segDx = destPt.x - startPt.x;
    float segDy = destPt.y - startPt.y;
    float segLen = std::sqrt(segDx * segDx + segDy * segDy);
    if (segLen < 1e-4f) return;

    // Unit normal to path segment
    float nx = -segDy / segLen;
    float ny =  segDx / segLen;

    for (size_t i = 0; i < pieces.size(); ++i) {
        if (static_cast<int>(i) == movingPieceIdx) continue;
        Point2D pPos = pieces[i].currentPos;
        Point2D closestPt;
        float dist = pointToSegmentDistance(pPos, startPt, destPt, &closestPt);

        // If piece is within clearance of the moving piece's straight path (or within 0.65 sq units for Knights/Bishops)
        float triggerDist = std::max(minClearance, 0.58f);
        if (dist < triggerDist) {
            // Determine direction from path to the blocking piece
            float offX = pPos.x - closestPt.x;
            float offY = pPos.y - closestPt.y;
            float offLen = std::sqrt(offX * offX + offY * offY);

            float pushDirX, pushDirY;
            if (offLen > 1e-3f) {
                pushDirX = offX / offLen;
                pushDirY = offY / offLen;
            } else {
                // Piece center lies directly on the line (e.g., orthogonal blocker on same line)
                // Push perpendicular toward the square edge with more room
                pushDirX = nx;
                pushDirY = ny;
            }

            // Clamp nudged position to stay safely inside the piece's own square (at its edge/corner)
            Point2D sqCenter = squareCenter(pieces[i].square);
            Point2D targetNudge = {
                sqCenter.x + pushDirX * maxNudge,
                sqCenter.y + pushDirY * maxNudge
            };
            targetNudge.x = std::max(sqCenter.x - maxNudge, std::min(sqCenter.x + maxNudge, targetNudge.x));
            targetNudge.y = std::max(sqCenter.y - maxNudge, std::min(sqCenter.y + maxNudge, targetNudge.y));

            nudgedList.push_back({static_cast<int>(i), pPos, targetNudge});
        }
    }

    // 2. Nudge blocking pieces to the edge of their squares
    for (const auto& rec : nudgedList) {
        cmds.push_back({CommandAction::TRAVEL_MAGNET_OFF, carriagePos, rec.originalCenter, "Travel to blocking piece"});
        cmds.push_back({CommandAction::MAGNET_ON, rec.originalCenter, rec.originalCenter, "Engage magnet on blocker"});
        cmds.push_back({CommandAction::MOVE_PIECE_STRAIGHT, rec.originalCenter, rec.nudgedPos, "Nudge piece to square edge for clearance"});
        cmds.push_back({CommandAction::MAGNET_OFF, rec.nudgedPos, rec.nudgedPos, "Release nudged piece at square edge"});
        carriagePos = rec.nudgedPos;
        pieces[rec.pieceIndex].currentPos = rec.nudgedPos;
    }

    // 3. Move the active piece in a straight line from startPt to destPt
    cmds.push_back({CommandAction::TRAVEL_MAGNET_OFF, carriagePos, startPt, "Travel to active piece"});
    cmds.push_back({CommandAction::MAGNET_ON, startPt, startPt, "Engage magnet on active piece"});
    cmds.push_back({CommandAction::MOVE_PIECE_STRAIGHT, startPt, destPt, moveLabel});
    cmds.push_back({CommandAction::MAGNET_OFF, destPt, destPt, "Release active piece at destination"});
    carriagePos = destPt;
    pieces[movingPieceIdx].currentPos = destPt;

    // 4. Restore nudged pieces from the edge of their square back to square center (in reverse order)
    for (auto it = nudgedList.rbegin(); it != nudgedList.rend(); ++it) {
        cmds.push_back({CommandAction::TRAVEL_MAGNET_OFF, carriagePos, it->nudgedPos, "Return to nudged piece"});
        cmds.push_back({CommandAction::MAGNET_ON, it->nudgedPos, it->nudgedPos, "Engage magnet on nudged piece"});
        cmds.push_back({CommandAction::MOVE_PIECE_STRAIGHT, it->nudgedPos, it->originalCenter, "Restore piece from square edge to center"});
        cmds.push_back({CommandAction::MAGNET_OFF, it->originalCenter, it->originalCenter, "Release restored piece"});
        carriagePos = it->originalCenter;
        pieces[it->pieceIndex].currentPos = it->originalCenter;
    }
}

std::vector<GantryCommand> MovementPlanner::planMove(
    std::vector<PieceState>& pieces,
    const MoveRequest& move
) {
    std::vector<GantryCommand> cmds;
    Point2D carriagePos = squareCenter(move.from);

    // Find moving piece
    int movingIdx = -1;
    for (size_t i = 0; i < pieces.size(); ++i) {
        if (pieces[i].square == move.from) {
            movingIdx = static_cast<int>(i);
            break;
        }
    }
    if (movingIdx < 0) return cmds;

    // Step A: Handle Capture (escort captured piece to its 16-slot graveyard spot)
    if (move.isCapture) {
        GridCoord capSq = (move.capturedSquare.col != -99) ? move.capturedSquare : move.to;
        int capIdx = -1;
        for (size_t i = 0; i < pieces.size(); ++i) {
            if (static_cast<int>(i) != movingIdx && pieces[i].square == capSq) {
                capIdx = static_cast<int>(i);
                break;
            }
        }
        if (capIdx >= 0) {
            GridCoord graveSlot = findAvailableGraveyardSlot(
                pieces, pieces[capIdx].color, pieces[capIdx].type
            );
            Point2D gravePt = squareCenter(graveSlot);
            planPointToPointWithEdgeNudging(
                pieces, capIdx, gravePt, cmds, carriagePos,
                "Escort captured piece to side graveyard slot"
            );
            pieces[capIdx].square = graveSlot;
        }
    }

    // Step B: Execute Primary Piece Move
    Point2D destPt = squareCenter(move.to);
    planPointToPointWithEdgeNudging(
        pieces, movingIdx, destPt, cmds, carriagePos,
        "Move active piece along straight trajectory"
    );
    pieces[movingIdx].square = move.to;

    // Step C: Handle Castling Rook Movement
    if (move.isCastling) {
        int rookIdx = -1;
        for (size_t i = 0; i < pieces.size(); ++i) {
            if (pieces[i].square == move.rookFrom) {
                rookIdx = static_cast<int>(i);
                break;
            }
        }
        if (rookIdx >= 0) {
            Point2D rookDest = squareCenter(move.rookTo);
            planPointToPointWithEdgeNudging(
                pieces, rookIdx, rookDest, cmds, carriagePos,
                "Castle: Move Rook past King (with edge nudge)"
            );
            pieces[rookIdx].square = move.rookTo;
        }
    }

    // Step D: Handle Pawn Promotion
    if (move.isPromotion) {
        // 1. Move promoted pawn to outer Pawn Graveyard column
        GridCoord pawnSlot = findAvailableGraveyardSlot(
            pieces, pieces[movingIdx].color, PieceType::PAWN
        );
        Point2D pawnGravePt = squareCenter(pawnSlot);
        planPointToPointWithEdgeNudging(
            pieces, movingIdx, pawnGravePt, cmds, carriagePos,
            "Promotion: Escort promoted pawn to outer pawn slot"
        );
        pieces[movingIdx].square = pawnSlot;

        // 2. Fetch Queen (or chosen heavy piece) from inner Heavy-Piece reserve column
        int promoIdx = findPromotionPieceInReserve(
            pieces, pieces[movingIdx].color, move.promotionType
        );
        if (promoIdx >= 0) {
            planPointToPointWithEdgeNudging(
                pieces, promoIdx, destPt, cmds, carriagePos,
                "Promotion: Deliver Queen from inner heavy-piece slot"
            );
            pieces[promoIdx].square = move.to;
        }
    }

    return cmds;
}

} // namespace project64
