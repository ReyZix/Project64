#include "movement_planner.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <utility>

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

std::vector<GridCoord> MovementPlanner::freeGraveyardSlots(
    const std::vector<PieceState>& pieces,
    Color pieceColor,
    PieceType type,
    float nearRow
) const {
    bool isPawn = (type == PieceType::PAWN);
    int primaryCol, fallbackCol;
    if (pieceColor == Color::WHITE) {
        primaryCol = isPawn ? -2 : -1;
        fallbackCol = isPawn ? -1 : -2;
    } else {
        primaryCol = isPawn ? 9 : 8;
        fallbackCol = isPawn ? 8 : 9;
    }

    const PieceType heavyOrder[] = {
        PieceType::QUEEN, PieceType::QUEEN,
        PieceType::ROOK, PieceType::ROOK,
        PieceType::BISHOP, PieceType::BISHOP,
        PieceType::KNIGHT, PieceType::KNIGHT
    };

    for (int col : {primaryCol, fallbackCol}) {
        std::vector<GridCoord> free;
        for (int r = 0; r < 8; ++r) {
            bool occupied = false;
            for (const auto& p : pieces) {
                if (p.square.col == col && p.square.row == r) { occupied = true; break; }
            }
            // The heavy-piece slot order is mirrored on the left and right columns.
            const PieceType slotType = (col == -2 || col == 9)
                ? PieceType::PAWN
                : heavyOrder[col < 4 ? 7 - r : r];
            if (!occupied && slotType == type) free.push_back({col, r});
        }
        if (free.empty()) {
            for (int r = 0; r < 8; ++r) {
                bool occupied = false;
                for (const auto& p : pieces) {
                    if (p.square.col == col && p.square.row == r) { occupied = true; break; }
                }
                if (!occupied) free.push_back({col, r});
            }
        }
        if (!free.empty()) {
            std::sort(free.begin(), free.end(), [nearRow](const GridCoord& a, const GridCoord& b) {
                return std::fabs(a.row - nearRow) < std::fabs(b.row - nearRow);
            });
            return free;
        }
    }
    return {{primaryCol, 0}};
}

int MovementPlanner::findPromotionPieceInReserve(
    const std::vector<PieceState>& pieces,
    Color color,
    PieceType desiredType,
    Point2D target
) const {
    int best = -1;
    float bestDist = 1e9f;
    for (size_t i = 0; i < pieces.size(); ++i) {
        const auto& p = pieces[i];
        if (p.color == color && p.type == desiredType && (p.square.col < 0 || p.square.col > 7)) {
            float d = std::hypot(p.currentPos.x - target.x, p.currentPos.y - target.y);
            if (d < bestDist) { bestDist = d; best = static_cast<int>(i); }
        }
    }
    return best;
}

bool MovementPlanner::isStraightPathClear(
    const std::vector<PieceState>& pieces,
    int pieceIdx,
    Point2D dest
) const {
    Point2D s = pieces[pieceIdx].currentPos;
    float hard = hardRadius();
    for (size_t i = 0; i < pieces.size(); ++i) {
        if (static_cast<int>(i) == pieceIdx) continue;
        if (pointToSegmentDistance(pieces[i].currentPos, s, dest) < hard) return false;
    }
    return true;
}

MovementPlanner::Route MovementPlanner::findRoute(
    const std::vector<PieceState>& pieces,
    int pieceIdx,
    Point2D dest
) const {
    constexpr int kMinX = -4, kMaxX = 20, kMinY = -2, kMaxY = 18;
    constexpr int kW = kMaxX - kMinX + 1, kH = kMaxY - kMinY + 1;
    constexpr float kDiagMult = 4.0f, kTurnPen = 0.3f, kNudgePen = 0.8f;
    static const int kDirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    const float trigger = triggerRadius();
    const float hard = hardRadius();

    auto nodeId = [](int ix, int iy) { return (iy - kMinY) * kW + (ix - kMinX); };

    // -1 = blocked by a piece nudging cannot clear, otherwise the number of pieces that must be nudged
    std::vector<int8_t> edgeCache(kW * kH * 8, -2);
    auto edgeNudges = [&](int ix, int iy, int d) -> int {
        int8_t& slot = edgeCache[nodeId(ix, iy) * 8 + d];
        if (slot != -2) return slot;
        Point2D a = {ix * 0.5f, iy * 0.5f};
        Point2D b = {(ix + kDirs[d][0]) * 0.5f, (iy + kDirs[d][1]) * 0.5f};
        int n = 0;
        for (size_t i = 0; i < pieces.size(); ++i) {
            if (static_cast<int>(i) == pieceIdx) continue;
            float dist = pointToSegmentDistance(pieces[i].currentPos, a, b);
            if (dist < hard) { n = -1; break; }
            if (dist < trigger) ++n;
        }
        slot = static_cast<int8_t>(n);
        return n;
    };

    Point2D startPt = pieces[pieceIdx].currentPos;
    int sx = static_cast<int>(std::lround(startPt.x * 2.0f)), sy = static_cast<int>(std::lround(startPt.y * 2.0f));
    int tx = static_cast<int>(std::lround(dest.x * 2.0f)), ty = static_cast<int>(std::lround(dest.y * 2.0f));

    const int stateCount = kW * kH * 9;
    std::vector<float> best(stateCount, 1e30f);
    std::vector<int> prev(stateCount, -1);
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;

    int startState = nodeId(sx, sy) * 9 + 8;
    best[startState] = 0.0f;
    open.push({0.0f, startState});
    int goalState = -1;
    while (!open.empty()) {
        auto [cost, s] = open.top();
        open.pop();
        if (cost > best[s]) continue;
        int node = s / 9, dirIn = s % 9;
        int ix = node % kW + kMinX, iy = node / kW + kMinY;
        if (ix == tx && iy == ty) { goalState = s; break; }
        for (int d = 0; d < 8; ++d) {
            int nx = ix + kDirs[d][0], ny = iy + kDirs[d][1];
            if (nx < kMinX || nx > kMaxX || ny < kMinY || ny > kMaxY) continue;
            int nudges = edgeNudges(ix, iy, d);
            if (nudges < 0) continue;
            float stepCost = (d >= 4 ? 0.5f * 1.41421356f * kDiagMult : 0.5f) + nudges * kNudgePen +
                             ((dirIn != 8 && dirIn != d) ? kTurnPen : 0.0f);
            int ns = nodeId(nx, ny) * 9 + d;
            if (cost + stepCost < best[ns]) {
                best[ns] = cost + stepCost;
                prev[ns] = s;
                open.push({best[ns], ns});
            }
        }
    }

    Route route;
    if (goalState < 0) {
        route.reachable = false;
        return route;
    }
    route.cost = best[goalState];

    struct Node { Point2D pt; int dir; };
    std::vector<Node> nodes;
    for (int s = goalState; s >= 0; s = prev[s]) {
        int node = s / 9;
        nodes.push_back({{(node % kW + kMinX) * 0.5f, (node / kW + kMinY) * 0.5f}, s % 9});
    }
    std::reverse(nodes.begin(), nodes.end());
    for (size_t i = 1; i + 1 < nodes.size(); ++i) {
        if (nodes[i + 1].dir != nodes[i].dir) route.waypoints.push_back(nodes[i].pt);
    }
    return route;
}

MovementPlanner::Route MovementPlanner::planRoute(
    const std::vector<PieceState>& pieces,
    int pieceIdx,
    Point2D dest,
    bool allowStraight
) const {
    if (allowStraight && isStraightPathClear(pieces, pieceIdx, dest)) {
        Route r;
        Point2D s = pieces[pieceIdx].currentPos;
        r.cost = std::hypot(dest.x - s.x, dest.y - s.y);
        return r;
    }
    Route r = findRoute(pieces, pieceIdx, dest);
    if (!r.reachable) {
        r.cost = 1e9f;
        r.waypoints.clear();
    }
    return r;
}

MovementPlanner::GraveyardTrip MovementPlanner::chooseGraveyardTrip(
    const std::vector<PieceState>& pieces,
    int pieceIdx
) const {
    const PieceState& piece = pieces[pieceIdx];
    GraveyardTrip best;
    bool haveBest = false;
    for (const auto& slot : freeGraveyardSlots(pieces, piece.color, piece.type, piece.currentPos.y - 0.5f)) {
        Route route = planRoute(pieces, pieceIdx, squareCenter(slot), false);
        if (!haveBest || route.cost < best.route.cost) {
            best.slot = slot;
            best.route = route;
            haveBest = true;
        }
    }
    return best;
}

void MovementPlanner::planPointToPointWithEdgeNudging(
    std::vector<PieceState>& pieces,
    int movingPieceIdx,
    Point2D destPt,
    std::vector<GantryCommand>& cmds,
    Point2D& carriagePos,
    const char* moveLabel,
    const std::vector<Point2D>& waypoints
) {
    Point2D startPt = pieces[movingPieceIdx].currentPos;
    float minClearance = minCenterDistanceSqUnits();
    float maxNudge = config_.maxEdgeNudgeRatio;

    std::vector<Point2D> path = {startPt};
    path.insert(path.end(), waypoints.begin(), waypoints.end());
    path.push_back(destPt);
    if (path.size() == 2 && std::hypot(destPt.x - startPt.x, destPt.y - startPt.y) < 1e-4f) return;

    // 1. Identify any pieces within clearance of any leg of the path
    std::vector<NudgedPieceRecord> nudgedList;

    for (size_t leg = 0; leg + 1 < path.size(); ++leg) {
    Point2D legA = path[leg];
    Point2D legB = path[leg + 1];
    float segDx = legB.x - legA.x;
    float segDy = legB.y - legA.y;
    float segLen = std::sqrt(segDx * segDx + segDy * segDy);
    if (segLen < 1e-4f) continue;

    // Unit normal to path segment
    float nx = -segDy / segLen;
    float ny =  segDx / segLen;

    for (size_t i = 0; i < pieces.size(); ++i) {
        if (static_cast<int>(i) == movingPieceIdx) continue;
        bool alreadyNudged = false;
        for (const auto& rec : nudgedList) {
            if (rec.pieceIndex == static_cast<int>(i)) { alreadyNudged = true; break; }
        }
        if (alreadyNudged) continue;

        Point2D pPos = pieces[i].currentPos;
        Point2D closestPt;
        float dist = pointToSegmentDistance(pPos, legA, legB, &closestPt);
        float t = ((closestPt.x - legA.x) * segDx + (closestPt.y - legA.y) * segDy) / (segLen * segLen);
        if (leg == 0 && t <= 0.01f) continue;                 // start of the whole path
        if (leg + 2 == path.size() && t >= 0.99f) continue;   // end of the whole path

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

    // 3. Move the active piece along the path (one straight leg per path segment, magnet stays on)
    cmds.push_back({CommandAction::TRAVEL_MAGNET_OFF, carriagePos, startPt, "Travel to active piece"});
    cmds.push_back({CommandAction::MAGNET_ON, startPt, startPt, "Engage magnet on active piece"});
    for (size_t leg = 0; leg + 1 < path.size(); ++leg) {
        cmds.push_back({CommandAction::MOVE_PIECE_STRAIGHT, path[leg], path[leg + 1], moveLabel});
    }
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
            GraveyardTrip trip = chooseGraveyardTrip(pieces, capIdx);
            Point2D gravePt = squareCenter(trip.slot);
            planPointToPointWithEdgeNudging(
                pieces, capIdx, gravePt, cmds, carriagePos,
                "Escort captured piece to side graveyard slot",
                trip.route.waypoints
            );
            pieces[capIdx].square = trip.slot;
        }
    }

    // Step B: Execute Primary Piece Move. A promoting pawn also travels to the last rank;
    // it is retired to the graveyard in Step D.
    Point2D destPt = squareCenter(move.to);
    {
        bool isKnight = (pieces[movingIdx].type == PieceType::KNIGHT);
        Route route = planRoute(pieces, movingIdx, destPt, !isKnight);
        planPointToPointWithEdgeNudging(
            pieces, movingIdx, destPt, cmds, carriagePos,
            isKnight ? "Move knight along X/Y legs" : "Move active piece", route.waypoints
        );
        pieces[movingIdx].square = move.to;
    }

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
            Route rookRoute = planRoute(pieces, rookIdx, rookDest, true);
            planPointToPointWithEdgeNudging(
                pieces, rookIdx, rookDest, cmds, carriagePos,
                "Castle: Move Rook past King", rookRoute.waypoints
            );
            pieces[rookIdx].square = move.rookTo;
        }
    }

    // Step D: Handle Pawn Promotion
    if (move.isPromotion) {
        // 1. Move the promoted pawn from the promotion square to the cheapest outer Pawn Graveyard slot
        GraveyardTrip pawnTrip = chooseGraveyardTrip(pieces, movingIdx);
        planPointToPointWithEdgeNudging(
            pieces, movingIdx, squareCenter(pawnTrip.slot), cmds, carriagePos,
            "Promotion: Escort promoted pawn to outer pawn slot",
            pawnTrip.route.waypoints
        );
        pieces[movingIdx].square = pawnTrip.slot;

        // 2. Fetch the closest matching piece from the reserve columns, via the transit lanes if cheaper
        int promoIdx = findPromotionPieceInReserve(
            pieces, pieces[movingIdx].color, move.promotionType, destPt
        );
        if (promoIdx >= 0) {
            Route route = planRoute(pieces, promoIdx, destPt, false);
            planPointToPointWithEdgeNudging(
                pieces, promoIdx, destPt, cmds, carriagePos,
                "Promotion: Deliver promoted piece from reserve slot",
                route.waypoints
            );
            pieces[promoIdx].square = move.to;
        }
    }

    return cmds;
}

} // namespace project64
