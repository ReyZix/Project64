#include "movement_planner.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>

using namespace project64;

// Create a chess piece at a given square
static PieceState piece(int id, PieceType type, Color color,
                        int col, int row) {
    GridCoord square{col, row};

    return {
        id,
        type,
        color,
        square,
        MovementPlanner::squareCenter(square),
        false
    };
}

// Verify that a piece reached its expected square
static bool at(const std::vector<PieceState>& pieces,
               int id, int col, int row) {
    for (const auto& p : pieces) {
        if (p.id == id) {
            return p.square == GridCoord{col, row} &&
                   std::hypot(
                       p.currentPos.x - (col + 0.5f),
                       p.currentPos.y - (row + 0.5f)
                   ) < 0.001f;
        }
    }
    return false;
}

// Verify the magnet command sequence
static bool sequence(const std::vector<GantryCommand>& commands) {
    bool magnetOn = false;

    for (const auto& cmd : commands) {
        if (cmd.action == CommandAction::MAGNET_ON) {
            if (magnetOn) return false;
            magnetOn = true;
        }
        else if (cmd.action == CommandAction::MAGNET_OFF) {
            if (!magnetOn) return false;
            magnetOn = false;
        }
        else if (cmd.action == CommandAction::MOVE_PIECE_STRAIGHT) {
            if (!magnetOn) return false;
        }
        else if (cmd.action == CommandAction::TRAVEL_MAGNET_OFF) {
            if (magnetOn) return false;
        }
    }

    return !magnetOn;
}


static float distanceToSegment(
    Point2D point,
    Point2D start,
    Point2D end
) {
    float dx = end.x - start.x;
    float dy = end.y - start.y;
    float lengthSquared = dx * dx + dy * dy;

    float t = 0.0f;

    if (lengthSquared > 0.000001f) {
        t = ((point.x - start.x) * dx +
             (point.y - start.y) * dy) / lengthSquared;

        t = std::max(0.0f, std::min(1.0f, t));
    }

    float closestX = start.x + t * dx;
    float closestY = start.y + t * dy;

    return std::hypot(
        point.x - closestX,
        point.y - closestY
    );
}

int main() {
    int passed = 0;
    int failed = 0;

    // Print test results
    auto test = [&](const char* name, bool success) {
        std::cout << (success ? "PASS " : "FAIL ")
                  << name << '\n';

        if (success) passed++;
        else failed++;
    };

    // Configure the planner for 60 mm squares
    DimensionsConfig config;
    config.squareSizeMm = 60.0f;
    config.pieceRadiusMm = 18.0f;

    MovementPlanner planner(config);

    // TEST A01: Pawn moves from E2 to E4
    {
        std::vector<PieceState> pieces{
            piece(1, PieceType::PAWN, Color::WHITE, 4, 1)
        };

        auto commands = planner.planMove(
            pieces, {{4, 1}, {4, 3}}
        );

        test("A01 pawn e2e4",
             !commands.empty() &&
             at(pieces, 1, 4, 3) &&
             sequence(commands));
    }

    // TEST A02: Pawn moves from E2 to E3
    {
        std::vector<PieceState> pieces{
            piece(1, PieceType::PAWN, Color::WHITE, 4, 1)
        };

        auto commands = planner.planMove(
            pieces, {{4, 1}, {4, 2}}
        );

        test("A02 pawn e2e3",
             !commands.empty() &&
             at(pieces, 1, 4, 2) &&
             sequence(commands));
    }


    // TEST A03: Knight moves from G1 to F3
    {
        std::vector<PieceState> pieces{
            piece(1, PieceType::KNIGHT, Color::WHITE, 6, 0),
            piece(2, PieceType::PAWN, Color::WHITE, 5, 1),
            piece(3, PieceType::PAWN, Color::WHITE, 6, 1)
        };

        auto commands = planner.planMove(
            pieces, {{6, 0}, {5, 2}}
        );

        test("A03 knight g1f3",
             !commands.empty() &&
             at(pieces, 1, 5, 2) &&
             at(pieces, 2, 5, 1) &&
             at(pieces, 3, 6, 1) &&
             sequence(commands));
    }

    // TEST A04: Bishop moves from C1 to G5
    {
        std::vector<PieceState> pieces{
            piece(1, PieceType::BISHOP, Color::WHITE, 2, 0)
        };

        auto commands = planner.planMove(
            pieces, {{2, 0}, {6, 4}}
        );

        test("A04 bishop c1g5",
             !commands.empty() &&
             at(pieces, 1, 6, 4) &&
             sequence(commands));
    }

    // TEST A05: Rook moves around an occupied square
    {
        std::vector<PieceState> pieces{
            piece(1, PieceType::ROOK, Color::WHITE, 0, 0),
            piece(2, PieceType::PAWN, Color::WHITE, 0, 2)
        };

        auto commands = planner.planMove(
            pieces, {{0, 0}, {0, 4}}
        );

        test("A05 rook a1a5 with obstacle",
             !commands.empty() &&
             at(pieces, 1, 0, 4) &&
             at(pieces, 2, 0, 2) &&
             sequence(commands));

        
        // Print the generated gantry commands for inspection
        std::cout << "\n--- A05 Movement Commands ---\n";

        for (size_t i = 0; i < commands.size(); ++i) {
            const auto& cmd = commands[i];

            std::cout << "Command " << i + 1 << ": ";

            switch (cmd.action) {
                case CommandAction::TRAVEL_MAGNET_OFF:
                    std::cout << "TRAVEL_MAGNET_OFF";
                    break;

                case CommandAction::MAGNET_ON:
                    std::cout << "MAGNET_ON";
                    break;

                case CommandAction::MOVE_PIECE_STRAIGHT:
                    std::cout << "MOVE_PIECE_STRAIGHT";
                    break;

                case CommandAction::MAGNET_OFF:
                    std::cout << "MAGNET_OFF";
                    break;
            }

            
            std::cout << " | From: ("
                      << cmd.from.x << ", " << cmd.from.y
                      << ") To: ("
                      << cmd.to.x << ", " << cmd.to.y
                      << ")\n";

        }

        std::cout << "-----------------------------\n";

        // TEST A05B: Check physical clearance around the pawn
        const Point2D pawnCenter = {0.5f, 2.5f};

        const float squareSizeMm = 60.0f;
        const float minimumClearanceMm = 39.0f;

        bool collisionFree = true;
        float smallestDistanceMm = 1e9f;

        for (const auto& cmd : commands) {
            if (cmd.action != CommandAction::MOVE_PIECE_STRAIGHT) {
                continue;
            }

            float distanceMm = distanceToSegment(
                pawnCenter, cmd.from, cmd.to
            ) * squareSizeMm;

            smallestDistanceMm = std::min(
                smallestDistanceMm, distanceMm
            );

            if (distanceMm < minimumClearanceMm - 0.001f) {
                collisionFree = false;
            }
        }

        std::cout << "Minimum distance to A3 pawn: "
                  << smallestDistanceMm << " mm\n";

        test("A05B collision clearance",
             collisionFree);
        
        // TEST A05C: Verify all gantry commands stay within travel limits
        const float boardSizeMm = 8.0f * squareSizeMm;
        const float gantryMarginMm = 60.0f;

        const float minX = -gantryMarginMm;
        const float maxX = boardSizeMm + gantryMarginMm;
        const float minY = -gantryMarginMm;
        const float maxY = boardSizeMm + gantryMarginMm;

        bool withinGantryBounds = true;

        auto checkPoint = [&](Point2D p) {
            float xMm = p.x * squareSizeMm;
            float yMm = p.y * squareSizeMm;

            return xMm >= minX && xMm <= maxX &&
                   yMm >= minY && yMm <= maxY;
        };

        for (const auto& cmd : commands) {
            if (!checkPoint(cmd.from) || !checkPoint(cmd.to)) {
                withinGantryBounds = false;

                std::cout << "Out-of-bounds command detected!\n";
                std::cout << "From: (" << cmd.from.x
                          << ", " << cmd.from.y << ")\n";
                std::cout << "To: (" << cmd.to.x
                          << ", " << cmd.to.y << ")\n";
            }
        }

        test("A05C gantry boundary validation",
             withinGantryBounds); 
            
        // TEST A05D: Detect when a route exceeds a smaller gantry margin
        const float restrictedMarginMm = 20.0f;

        auto checkRestrictedPoint = [&](Point2D p) {
            float xMm = p.x * squareSizeMm;
            float yMm = p.y * squareSizeMm;

            return xMm >= -restrictedMarginMm &&
                   xMm <= boardSizeMm + restrictedMarginMm &&
                   yMm >= -restrictedMarginMm &&
                   yMm <= boardSizeMm + restrictedMarginMm;
        };

        bool exceedsRestrictedBounds = false;

        for (const auto& cmd : commands) {
            if (!checkRestrictedPoint(cmd.from) ||
                !checkRestrictedPoint(cmd.to)) {
                exceedsRestrictedBounds = true;
                break;
            }
        }

        test("A05D restricted gantry margin detected",
             exceedsRestrictedBounds);


    }

    // TEST A11: Pawn captures on D5
    {
        std::vector<PieceState> pieces{
            piece(1, PieceType::PAWN, Color::WHITE, 4, 3),
            piece(2, PieceType::PAWN, Color::BLACK, 3, 4)
        };

        MoveRequest move;
        move.from = {4, 3};
        move.to = {3, 4};
        move.isCapture = true;

        auto commands = planner.planMove(pieces, move);

        test("A11 capture e4d5",
             !commands.empty() &&
             at(pieces, 1, 3, 4) &&
             pieces[1].square.col == 9 &&
             sequence(commands));
    }

    // TEST A14: White kingside castling
    {
        std::vector<PieceState> pieces{
            piece(1, PieceType::KING, Color::WHITE, 4, 0),
            piece(2, PieceType::ROOK, Color::WHITE, 7, 0)
        };

        MoveRequest move;
        move.from = {4, 0};
        move.to = {6, 0};
        move.isCastling = true;
        move.rookFrom = {7, 0};
        move.rookTo = {5, 0};

        auto commands = planner.planMove(pieces, move);

        test("A14 kingside castle",
             !commands.empty() &&
             at(pieces, 1, 6, 0) &&
             at(pieces, 2, 5, 0) &&
             sequence(commands));
    }

    // TEST A16: Pawn promotes to queen
    {
        std::vector<PieceState> pieces{
            piece(1, PieceType::PAWN, Color::WHITE, 0, 6),
            piece(2, PieceType::QUEEN, Color::WHITE, -1, 0)
        };

        MoveRequest move;
        move.from = {0, 6};
        move.to = {0, 7};
        move.isPromotion = true;
        move.promotionType = PieceType::QUEEN;

        auto commands = planner.planMove(pieces, move);

        test("A16 queen promotion",
             !commands.empty() &&
             pieces[0].square.col == -2 &&
             at(pieces, 2, 0, 7) &&
             sequence(commands));
    }

    // TEST A19: Promotion without reserve queen
    {
        std::vector<PieceState> pieces{
            piece(1, PieceType::PAWN, Color::WHITE, 0, 6)
        };

        MoveRequest move;
        move.from = {0, 6};
        move.to = {0, 7};
        move.isPromotion = true;

        auto commands = planner.planMove(pieces, move);

        bool queenFound = false;

        for (const auto& p : pieces) {
            if (p.square == GridCoord{0, 7} &&
                p.type == PieceType::QUEEN) {
                queenFound = true;
            }
        }

        test("A19 missing promotion reserve rejected",
             commands.empty() && !queenFound);
    }

    // TEST A20: Empty source square
    {
        std::vector<PieceState> pieces{
            piece(1, PieceType::PAWN, Color::WHITE, 4, 1)
        };

        auto commands = planner.planMove(
            pieces, {{2, 2}, {2, 3}}
        );

        test("A20 empty source",
             commands.empty() &&
             at(pieces, 1, 4, 1));
    }

    // Final summary
    std::cout << "\nSUMMARY "
              << passed << " passed, "
              << failed << " failed\n";

    return failed ? 1 : 0;
}
