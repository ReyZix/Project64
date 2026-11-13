/**
 * Project 64 - Autonomous Chessboard Movement & Pathfinding Engine
 * Preset Scenarios for Demonstration and Verification
 */

use crate::reset::standard_board_layout;
use crate::types::*;

pub struct ScenarioSetup {
    pub name: &'static str,
    pub description: &'static str,
    pub pieces: Vec<PieceState>,
    pub move_request: MoveRequest,
}

pub fn load_scenario(name: &str) -> Option<ScenarioSetup> {
    match name {
        "knight_nudge" => {
            // Standard starting board, move White Knight g1 -> f3 between e2 and g2 pawns
            let pieces = standard_board_layout();
            let mv = MoveRequest::simple(GridCoord::new(6, 0), GridCoord::new(5, 2));
            Some(ScenarioSetup {
                name: "1. Knight Pass (Edge Nudge)",
                description: "White Knight jumps g1 -> f3, safely nudging adjacent pawns aside and restoring them.",
                pieces,
                move_request: mv,
            })
        }
        "bishop_diagonal" => {
            // Bishop tight diagonal: c1 -> g5 passing between pawns placed at e3/d4
            let mut pieces = standard_board_layout();
            for p in &mut pieces {
                if p.square.col == 3 && p.square.row == 1 {
                    p.square = GridCoord::new(4, 2);
                    p.current_pos = Point2D::new(4.5, 2.5);
                }
                if p.square.col == 4 && p.square.row == 1 {
                    p.square = GridCoord::new(3, 3);
                    p.current_pos = Point2D::new(3.5, 3.5);
                }
            }
            let mv = MoveRequest::simple(GridCoord::new(2, 0), GridCoord::new(6, 4));
            Some(ScenarioSetup {
                name: "2. Bishop Tight Diagonal",
                description: "White Bishop cuts diagonally c1 -> g5 between diagonally flanking pawns.",
                pieces,
                move_request: mv,
            })
        }
        "capture_graveyard" => {
            // White Pawn e4 captures Black Pawn d5, escorting Black pawn to side graveyard
            let mut pieces = standard_board_layout();
            for p in &mut pieces {
                if p.color == Color::White && p.square.col == 4 && p.square.row == 1 {
                    p.square = GridCoord::new(4, 3);
                    p.current_pos = Point2D::new(4.5, 3.5);
                }
                if p.color == Color::Black && p.square.col == 3 && p.square.row == 6 {
                    p.square = GridCoord::new(3, 4);
                    p.current_pos = Point2D::new(3.5, 4.5);
                }
            }
            let mut mv = MoveRequest::simple(GridCoord::new(4, 3), GridCoord::new(3, 4));
            mv.is_capture = true;
            mv.captured_square = Some(GridCoord::new(3, 4));
            Some(ScenarioSetup {
                name: "3. Capture to Graveyard",
                description: "White Pawn e4xd5 captures Black pawn, escorting it to a free graveyard slot.",
                pieces,
                move_request: mv,
            })
        }
        "castling" => {
            // White Kingside Castling: e1 -> g1, rook h1 -> f1
            let mut pieces = standard_board_layout();
            // Clear f1 and g1 (remove bishop and knight)
            pieces.retain(|p| !(p.color == Color::White && (p.square.col == 5 || p.square.col == 6) && p.square.row == 0));
            let mut mv = MoveRequest::simple(GridCoord::new(4, 0), GridCoord::new(6, 0));
            mv.is_castling = true;
            mv.rook_from = Some(GridCoord::new(7, 0));
            mv.rook_to = Some(GridCoord::new(5, 0));
            Some(ScenarioSetup {
                name: "4. Kingside Castling",
                description: "White King castles e1 -> g1; gantry maneuvers Rook h1 -> f1 around the King.",
                pieces,
                move_request: mv,
            })
        }
        "promotion" => {
            // White pawn at a7 advances to a8 to promote to Queen
            let mut pieces = standard_board_layout();
            // Clear a8
            pieces.retain(|p| !(p.square.col == 0 && p.square.row == 7));
            // Place White pawn on a7
            for p in &mut pieces {
                if p.color == Color::White && p.piece_type == PieceType::Pawn && p.square.col == 0 {
                    p.square = GridCoord::new(0, 6);
                    p.current_pos = Point2D::new(0.5, 6.5);
                }
            }
            let mut mv = MoveRequest::simple(GridCoord::new(0, 6), GridCoord::new(0, 7));
            mv.is_promotion = true;
            mv.promotion_type = PieceType::Queen;
            Some(ScenarioSetup {
                name: "5. Pawn Promotion",
                description: "White Pawn advances a7 -> a8, retires to graveyard, and fetches a reserve Queen.",
                pieces,
                move_request: mv,
            })
        }
        _ => None,
    }
}
