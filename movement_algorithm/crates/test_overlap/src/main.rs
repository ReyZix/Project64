/**
 * Project 64 - Autonomous Chessboard Movement Engine
 * Comprehensive Piece Overlap & Clearance Test Suite in Rust
 */

use movement_planner::reset::{plan_reset_board_with_magnet, standard_board_layout};
use movement_planner::*;

#[derive(Debug, Default)]
pub struct CollisionReport {
    pub has_physical_overlap: bool,
    pub has_angle_violation: bool,
    pub has_restoration_error: bool,
    pub has_boundary_violation: bool,
    pub details: String,
    pub min_physical_distance_mm: f32,
    pub min_clearance_margin_mm: f32,
    pub commands_executed: usize,
}

pub fn verify_commands_for_overlap(
    initial_pieces: &[PieceState],
    expected_final_pieces: &[PieceState],
    cmds: &[GantryCommand],
    config: &DimensionsConfig,
) -> CollisionReport {
    let mut rep = CollisionReport {
        commands_executed: cmds.len(),
        min_physical_distance_mm: 1e9,
        min_clearance_margin_mm: 1e9,
        ..Default::default()
    };

    let mut current_pieces: Vec<PieceState> = initial_pieces.to_vec();
    let mut carried_piece_idx: Option<usize> = None;

    for (cmd_idx, cmd) in cmds.iter().enumerate() {
        match cmd.action {
            CommandAction::MagnetOn => {
                carried_piece_idx = None;
                let mut best_dist = 1e9f32;
                for (i, p) in current_pieces.iter().enumerate() {
                    let d = p.current_pos.dist(cmd.from);
                    if d < 0.15 && d < best_dist {
                        best_dist = d;
                        carried_piece_idx = Some(i);
                    }
                }
                if carried_piece_idx.is_none() {
                    rep.details.push_str(&format!(
                        "[Cmd {} warn: MAGNET_ON with no piece at ({:.2}, {:.2})] ",
                        cmd_idx, cmd.from.x, cmd.from.y
                    ));
                }
            }
            CommandAction::MagnetOff => {
                if let Some(c_idx) = carried_piece_idx {
                    current_pieces[c_idx].current_pos = cmd.to;
                }
                carried_piece_idx = None;
            }
            CommandAction::MovePieceStraight => {
                let Some(c_idx) = carried_piece_idx else {
                    rep.details.push_str(&format!(
                        "[Cmd {} error: MOVE_PIECE_STRAIGHT without magnet] ",
                        cmd_idx
                    ));
                    continue;
                };

                // 1. Strict Angle Invariant: Must be orthogonal or exact 45 degrees
                let dx = (cmd.to.x - cmd.from.x).abs();
                let dy = (cmd.to.y - cmd.from.y).abs();
                if dx > 1e-4 && dy > 1e-4 && (dx - dy).abs() > 1e-4 {
                    rep.has_angle_violation = true;
                    rep.details.push_str(&format!(
                        "[Illegal angle in cmd {} ('{}'): dx={:.4}, dy={:.4}] ",
                        cmd_idx, cmd.description, dx, dy
                    ));
                }

                // 2. Boundary Check: Must stay within gantry coverage (cols -2.6 to 9.6, rows -1.6 to 9.6)
                if cmd.to.x < -2.6 || cmd.to.x > 9.6 || cmd.to.y < -1.6 || cmd.to.y > 9.6 {
                    rep.has_boundary_violation = true;
                    rep.details.push_str(&format!(
                        "[Boundary violation in cmd {}: to=({:.2},{:.2})] ",
                        cmd_idx, cmd.to.x, cmd.to.y
                    ));
                }

                let carried_r = piece_radius_mm(current_pieces[c_idx].piece_type);

                // 3. Continuous Distance Check to all other stationary pieces
                for (i, p) in current_pieces.iter().enumerate() {
                    if i == c_idx {
                        continue;
                    }
                    let stat_r = piece_radius_mm(p.piece_type);
                    let required_physical_dist_mm = carried_r + stat_r;

                    let (dist_sq_units, _) = MovementPlanner::point_to_segment_distance(p.current_pos, cmd.from, cmd.to);
                    let dist_mm = dist_sq_units * config.square_size_mm;

                    if dist_mm < rep.min_physical_distance_mm {
                        rep.min_physical_distance_mm = dist_mm;
                    }
                    let margin_mm = dist_mm - required_physical_dist_mm;
                    if margin_mm < rep.min_clearance_margin_mm {
                        rep.min_clearance_margin_mm = margin_mm;
                    }

                    if dist_mm < required_physical_dist_mm - 0.05 {
                        rep.has_physical_overlap = true;
                        rep.details.push_str(&format!(
                            "[PHYSICAL OVERLAP in cmd {} ('{}'): piece {} moving ({:.2},{:.2})->({:.2},{:.2}) hits piece {} at ({:.2},{:.2}): dist={:.2}mm < required {:.2}mm] ",
                            cmd_idx, cmd.description, current_pieces[c_idx].id, cmd.from.x, cmd.from.y, cmd.to.x, cmd.to.y,
                            p.id, p.current_pos.x, p.current_pos.y, dist_mm, required_physical_dist_mm
                        ));
                    }
                }

                // Update piece position to end of move
                current_pieces[c_idx].current_pos = cmd.to;
            }
            CommandAction::TravelMagnetOff => {}
        }

        // 4. Pairwise Stationary-to-Stationary Distance Check
        for i in 0..current_pieces.len() {
            for j in (i + 1)..current_pieces.len() {
                let r1 = piece_radius_mm(current_pieces[i].piece_type);
                let r2 = piece_radius_mm(current_pieces[j].piece_type);
                let dist_mm = current_pieces[i].current_pos.dist(current_pieces[j].current_pos) * config.square_size_mm;
                if dist_mm < r1 + r2 - 0.05 {
                    rep.has_physical_overlap = true;
                    rep.details.push_str(&format!(
                        "[STATIONARY OVERLAP at cmd {}: piece {} at ({:.2},{:.2}) and piece {} at ({:.2},{:.2}) dist={:.2}mm < {:.2}mm] ",
                        cmd_idx, current_pieces[i].id, current_pieces[i].current_pos.x, current_pieces[i].current_pos.y,
                        current_pieces[j].id, current_pieces[j].current_pos.x, current_pieces[j].current_pos.y, dist_mm, r1 + r2
                    ));
                }
            }
        }
    }

    // 5. Final State Restoration Check: Every piece must end at square center
    for (i, p) in current_pieces.iter().enumerate() {
        let expected_center = MovementPlanner::square_center(expected_final_pieces[i].square);
        let dist_to_center = p.current_pos.dist(expected_center);
        if dist_to_center > 0.05 {
            rep.has_restoration_error = true;
            rep.details.push_str(&format!(
                "[Piece {} left off-center at ({:.2},{:.2}) expected ({:.2},{:.2})] ",
                p.id, p.current_pos.x, p.current_pos.y, expected_center.x, expected_center.y
            ));
        }
    }

    rep
}

fn run_test(
    category: &str,
    name: &str,
    initial_pieces: Vec<PieceState>,
    req: &MoveRequest,
    config: &DimensionsConfig,
) -> bool {
    let planner = MovementPlanner::new(*config);
    let mut pieces_copy = initial_pieces.clone();
    let cmds = planner.plan_move(&mut pieces_copy, req);

    if cmds.is_empty() {
        println!("  [FAIL] {} - {}: MovementPlanner returned 0 commands!", category, name);
        return false;
    }

    let rep = verify_commands_for_overlap(&initial_pieces, &pieces_copy, &cmds, config);

    let failed = rep.has_physical_overlap
        || rep.has_angle_violation
        || rep.has_restoration_error
        || rep.has_boundary_violation;

    if failed {
        let mut err = String::new();
        if rep.has_physical_overlap { err.push_str("[PHYSICAL OVERLAP] "); }
        if rep.has_angle_violation { err.push_str("[ANGLE VIOLATION] "); }
        if rep.has_restoration_error { err.push_str("[RESTORATION ERROR] "); }
        if rep.has_boundary_violation { err.push_str("[OUT OF BOUNDS] "); }
        err.push_str(&rep.details);
        println!("  [FAIL] {} - {}: {}", category, name, err);
        false
    } else {
        println!(
            "  [PASS] {} - {}: ({} cmds, min piece dist = {:.1}mm, min margin = {:.1}mm)",
            category, name, rep.commands_executed, rep.min_physical_distance_mm, rep.min_clearance_margin_mm
        );
        true
    }
}

fn main() {
    let mut config = DimensionsConfig::default();
    config.square_size_mm = 60.0; // Standard board 60mm squares

    println!("======================================================================");
    println!("  PROJECT 64: AUTONOMOUS CHESSBOARD MOVEMENT & COLLISION TEST SUITE   ");
    println!("  Target: Pure Rust (Zero JavaScript/TypeScript)                      ");
    println!("======================================================================\n");

    let mut total = 0;
    let mut passed = 0;

    // CATEGORY 1: Orthogonal Moves
    {
        let cat = "1. Orthogonal Moves";
        println!("\n--- {} ---", cat);

        // 1.1 Pawn 1-step e2-e3
        let b = standard_board_layout();
        let r1 = MoveRequest::simple(GridCoord::new(4, 1), GridCoord::new(4, 2));
        total += 1; if run_test(cat, "Pawn 1-step e2-e3", b, &r1, &config) { passed += 1; }

        // 1.2 Pawn 2-step d2-d4 between c2/e2 pawns
        let b = standard_board_layout();
        let r2 = MoveRequest::simple(GridCoord::new(3, 1), GridCoord::new(3, 3));
        total += 1; if run_test(cat, "Pawn 2-step d2-d4 between c2/e2 pawns", b, &r2, &config) { passed += 1; }

        // 1.3 Pawn on outer edge a2-a4
        let b = standard_board_layout();
        let r3 = MoveRequest::simple(GridCoord::new(0, 1), GridCoord::new(0, 3));
        total += 1; if run_test(cat, "Pawn 2-step a2-a4 along board edge", b, &r3, &config) { passed += 1; }

        // 1.4 Rook 6-square file slide Ra1-a7
        let mut b_rook = standard_board_layout();
        b_rook.retain(|p| !(p.square.col == 0 && p.square.row > 0));
        let r4 = MoveRequest::simple(GridCoord::new(0, 0), GridCoord::new(0, 6));
        total += 1; if run_test(cat, "Rook Ra1-a7 6-square file slide", b_rook, &r4, &config) { passed += 1; }

        // 1.5 Rook 6-square rank slide Ra1-g1
        let mut b_rook_rank = standard_board_layout();
        b_rook_rank.retain(|p| !(p.square.row == 0 && p.square.col > 0));
        let r5 = MoveRequest::simple(GridCoord::new(0, 0), GridCoord::new(6, 0));
        total += 1; if run_test(cat, "Rook Ra1-g1 6-square rank slide", b_rook_rank, &r5, &config) { passed += 1; }

        // 1.6 King 1-step orthogonal Ke1-e2
        let mut b_king = standard_board_layout();
        b_king.retain(|p| !(p.square.col == 4 && p.square.row == 1));
        let r6 = MoveRequest::simple(GridCoord::new(4, 0), GridCoord::new(4, 1));
        total += 1; if run_test(cat, "King Ke1-e2 1-step orthogonal", b_king, &r6, &config) { passed += 1; }
    }

    // CATEGORY 2: Diagonal Moves (45° Strict Angles)
    {
        let cat = "2. Diagonal Moves (45°)";
        println!("\n--- {} ---", cat);

        // 2.1 Bishop tight diagonal passing between diagonally flanking pawns
        let mut b_bish = standard_board_layout();
        for p in &mut b_bish {
            if p.square.col == 3 && p.square.row == 1 { p.square = GridCoord::new(4, 2); p.current_pos = Point2D::new(4.5, 2.5); }
            if p.square.col == 4 && p.square.row == 1 { p.square = GridCoord::new(3, 3); p.current_pos = Point2D::new(3.5, 3.5); }
        }
        let r1 = MoveRequest::simple(GridCoord::new(2, 0), GridCoord::new(6, 4));
        total += 1; if run_test(cat, "Bishop Bc1-g5 tight diagonal between flanking pieces", b_bish, &r1, &config) { passed += 1; }

        // 2.2 Queen 45° diagonal across board Qd1-h5
        let mut b_queen = standard_board_layout();
        b_queen.retain(|p| !(p.square.row == 1 && (p.square.col == 3 || p.square.col == 4 || p.square.col == 5)));
        b_queen.retain(|p| !(p.square.row == 6 && p.square.col == 7));
        let r2 = MoveRequest::simple(GridCoord::new(3, 0), GridCoord::new(7, 4));
        total += 1; if run_test(cat, "Queen Qd1-h5 long 4-square 45° diagonal", b_queen, &r2, &config) { passed += 1; }

        // 2.3 Pawn diagonal 1-step capture move e4xd5
        let mut b_pawn = standard_board_layout();
        for p in &mut b_pawn {
            if p.color == Color::White && p.square.col == 4 && p.square.row == 1 { p.square = GridCoord::new(4, 3); p.current_pos = Point2D::new(4.5, 3.5); }
            if p.color == Color::Black && p.square.col == 3 && p.square.row == 6 { p.square = GridCoord::new(3, 4); p.current_pos = Point2D::new(3.5, 4.5); }
        }
        let mut r3 = MoveRequest::simple(GridCoord::new(4, 3), GridCoord::new(3, 4));
        r3.is_capture = true;
        r3.captured_square = Some(GridCoord::new(3, 4));
        total += 1; if run_test(cat, "Pawn 45° capture move e4xd5", b_pawn, &r3, &config) { passed += 1; }
    }

    // CATEGORY 3: Knight Moves (Edge Nudging)
    {
        let cat = "3. Knight Moves (Edge Nudge)";
        println!("\n--- {} ---", cat);

        // 3.1 Knight jumping out from starting back rank Nb1-c3
        let b = standard_board_layout();
        let r1 = MoveRequest::simple(GridCoord::new(1, 0), GridCoord::new(2, 2));
        total += 1; if run_test(cat, "Knight Nb1-c3 jump from starting position", b, &r1, &config) { passed += 1; }

        // 3.2 Knight jumping out from starting back rank Ng1-f3
        let b = standard_board_layout();
        let r2 = MoveRequest::simple(GridCoord::new(6, 0), GridCoord::new(5, 2));
        total += 1; if run_test(cat, "Knight Ng1-f3 jump from starting position", b, &r2, &config) { passed += 1; }
    }

    // CATEGORY 4: Captures & Graveyard Trips
    {
        let cat = "4. Captures & Graveyard Trips";
        println!("\n--- {} ---", cat);

        // 4.1 Pawn captures pawn, escorting to outer graveyard slot
        let mut b_cap = standard_board_layout();
        for p in &mut b_cap {
            if p.color == Color::White && p.square.col == 4 && p.square.row == 1 { p.square = GridCoord::new(4, 3); p.current_pos = Point2D::new(4.5, 3.5); }
            if p.color == Color::Black && p.square.col == 3 && p.square.row == 6 { p.square = GridCoord::new(3, 4); p.current_pos = Point2D::new(3.5, 4.5); }
        }
        let mut r1 = MoveRequest::simple(GridCoord::new(4, 3), GridCoord::new(3, 4));
        r1.is_capture = true;
        r1.captured_square = Some(GridCoord::new(3, 4));
        total += 1; if run_test(cat, "Pawn capture e4xd5 with graveyard escort", b_cap, &r1, &config) { passed += 1; }
    }

    // CATEGORY 5: Castling
    {
        let cat = "5. Castling";
        println!("\n--- {} ---", cat);

        // 5.1 White Kingside Castling
        let mut b_castle_k = standard_board_layout();
        b_castle_k.retain(|p| !(p.color == Color::White && (p.square.col == 5 || p.square.col == 6) && p.square.row == 0));
        let mut r1 = MoveRequest::simple(GridCoord::new(4, 0), GridCoord::new(6, 0));
        r1.is_castling = true;
        r1.rook_from = Some(GridCoord::new(7, 0));
        r1.rook_to = Some(GridCoord::new(5, 0));
        total += 1; if run_test(cat, "White Kingside Castling e1-g1 (Rook h1-f1)", b_castle_k, &r1, &config) { passed += 1; }

        // 5.2 White Queenside Castling
        let mut b_castle_q = standard_board_layout();
        b_castle_q.retain(|p| !(p.color == Color::White && (p.square.col == 1 || p.square.col == 2 || p.square.col == 3) && p.square.row == 0));
        let mut r2 = MoveRequest::simple(GridCoord::new(4, 0), GridCoord::new(2, 0));
        r2.is_castling = true;
        r2.rook_from = Some(GridCoord::new(0, 0));
        r2.rook_to = Some(GridCoord::new(3, 0));
        total += 1; if run_test(cat, "White Queenside Castling e1-c1 (Rook a1-d1)", b_castle_q, &r2, &config) { passed += 1; }
    }

    // CATEGORY 6: Promotion
    {
        let cat = "6. Pawn Promotion";
        println!("\n--- {} ---", cat);

        // 6.1 Center file promotion e7-e8=Q
        let mut b_promo_q = standard_board_layout();
        b_promo_q.retain(|p| !(p.square.col == 4 && (p.square.row == 6 || p.square.row == 7)));
        b_promo_q.push(PieceState {
            id: 88,
            piece_type: PieceType::Pawn,
            color: Color::White,
            square: GridCoord::new(4, 6),
            current_pos: Point2D::new(4.5, 6.5),
            is_reserve: false,
            has_moved: true,
        });
        let mut r1 = MoveRequest::simple(GridCoord::new(4, 6), GridCoord::new(4, 7));
        r1.is_promotion = true;
        r1.promotion_type = PieceType::Queen;
        total += 1; if run_test(cat, "Pawn Promotion e7-e8=Q (retire pawn, deliver Queen)", b_promo_q, &r1, &config) { passed += 1; }

        // 6.2 Edge file promotion a7-a8=Q
        let mut b_promo_edge = standard_board_layout();
        b_promo_edge.retain(|p| !(p.square.col == 0 && (p.square.row == 6 || p.square.row == 7)));
        b_promo_edge.push(PieceState {
            id: 89,
            piece_type: PieceType::Pawn,
            color: Color::White,
            square: GridCoord::new(0, 6),
            current_pos: Point2D::new(0.5, 6.5),
            is_reserve: false,
            has_moved: true,
        });
        let mut r2 = MoveRequest::simple(GridCoord::new(0, 6), GridCoord::new(0, 7));
        r2.is_promotion = true;
        r2.promotion_type = PieceType::Queen;
        total += 1; if run_test(cat, "Edge Pawn Promotion a7-a8=Q on file a", b_promo_edge, &r2, &config) { passed += 1; }

        // 6.3 Black promotion on rank 1 d2-d1=Q
        let mut b_promo_black = standard_board_layout();
        b_promo_black.retain(|p| !(p.square.col == 3 && (p.square.row == 0 || p.square.row == 1)));
        b_promo_black.push(PieceState {
            id: 90,
            piece_type: PieceType::Pawn,
            color: Color::Black,
            square: GridCoord::new(3, 1),
            current_pos: Point2D::new(3.5, 1.5),
            is_reserve: false,
            has_moved: true,
        });
        let mut r3 = MoveRequest::simple(GridCoord::new(3, 1), GridCoord::new(3, 0));
        r3.is_promotion = true;
        r3.promotion_type = PieceType::Queen;
        total += 1; if run_test(cat, "Black Pawn Promotion d2-d1=Q to rank 1", b_promo_black, &r3, &config) { passed += 1; }
    }

    // CATEGORY 7: Board Magnet Reset
    {
        let cat = "7. Magnet Board Reset";
        println!("\n--- {} ---", cat);

        let planner = MovementPlanner::new(config);
        let mut scrambled = standard_board_layout();
        // Scramble a few pieces
        if let Some(p) = scrambled.iter_mut().find(|p| p.square.col == 4 && p.square.row == 1) {
            p.square = GridCoord::new(4, 3);
            p.current_pos = Point2D::new(4.5, 3.5);
        }
        if let Some(p) = scrambled.iter_mut().find(|p| p.square.col == 3 && p.square.row == 6) {
            p.square = GridCoord::new(3, 4);
            p.current_pos = Point2D::new(3.5, 4.5);
        }
        let initial_pieces = scrambled.clone();
        let cmds = plan_reset_board_with_magnet(&planner, &mut scrambled, Point2D::new(4.5, 4.5));

        total += 1;
        if cmds.is_empty() {
            println!("  [FAIL] {} - Board Reset returned 0 commands!", cat);
        } else {
            let rep = verify_commands_for_overlap(&initial_pieces, &scrambled, &cmds, &config);
            if rep.has_physical_overlap || rep.has_angle_violation || rep.has_restoration_error || rep.has_boundary_violation {
                println!("  [FAIL] {} - Reset validation failed: {}", cat, rep.details);
            } else {
                println!(
                    "  [PASS] {} - Full Board Reset: ({} cmds, min piece dist = {:.1}mm, min margin = {:.1}mm)",
                    cat, rep.commands_executed, rep.min_physical_distance_mm, rep.min_clearance_margin_mm
                );
                passed += 1;
            }
        }
    }

    println!("\n======================================================================");
    println!("  TEST SUITE SUMMARY: {}/{} tests passed", passed, total);
    println!("======================================================================");

    if passed != total {
        std::process::exit(1);
    }
}
