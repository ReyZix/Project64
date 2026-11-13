/**
 * Project 64 - Autonomous Chessboard Movement & Pathfinding Engine
 * Magnet Board Reset Module: Automatic re-racking of all displaced/captured pieces.
 */

use crate::planner::MovementPlanner;
use crate::types::*;
use std::collections::{HashMap, HashSet};

pub fn standard_board_layout() -> Vec<PieceState> {
    let mut pieces = Vec::with_capacity(34);
    let mut id = 1;

    let back_rank = [
        PieceType::Rook,
        PieceType::Knight,
        PieceType::Bishop,
        PieceType::Queen,
        PieceType::King,
        PieceType::Bishop,
        PieceType::Knight,
        PieceType::Rook,
    ];

    // White pieces
    for c in 0..8 {
        pieces.push(PieceState {
            id,
            piece_type: back_rank[c],
            color: Color::White,
            square: GridCoord::new(c as i32, 0),
            current_pos: Point2D::new(c as f32 + 0.5, 0.5),
            is_reserve: false,
            has_moved: false,
        });
        id += 1;
        pieces.push(PieceState {
            id,
            piece_type: PieceType::Pawn,
            color: Color::White,
            square: GridCoord::new(c as i32, 1),
            current_pos: Point2D::new(c as f32 + 0.5, 1.5),
            is_reserve: false,
            has_moved: false,
        });
        id += 1;
    }

    // Black pieces
    for c in 0..8 {
        pieces.push(PieceState {
            id,
            piece_type: PieceType::Pawn,
            color: Color::Black,
            square: GridCoord::new(c as i32, 6),
            current_pos: Point2D::new(c as f32 + 0.5, 6.5),
            is_reserve: false,
            has_moved: false,
        });
        id += 1;
        pieces.push(PieceState {
            id,
            piece_type: back_rank[c],
            color: Color::Black,
            square: GridCoord::new(c as i32, 7),
            current_pos: Point2D::new(c as f32 + 0.5, 7.5),
            is_reserve: false,
            has_moved: false,
        });
        id += 1;
    }

    // Standard reserve Queens in side graveyards
    pieces.push(PieceState {
        id,
        piece_type: PieceType::Queen,
        color: Color::White,
        square: GridCoord::new(-1, 7),
        current_pos: Point2D::new(-0.5, 7.5),
        is_reserve: true,
        has_moved: false,
    });
    id += 1;

    pieces.push(PieceState {
        id,
        piece_type: PieceType::Queen,
        color: Color::Black,
        square: GridCoord::new(8, 0),
        current_pos: Point2D::new(8.5, 0.5),
        is_reserve: true,
        has_moved: false,
    });

    pieces
}

#[derive(Debug, Clone)]
struct TargetSlot {
    col: i32,
    row: i32,
    color: Color,
    piece_type: PieceType,
}

/// Plans a sequence of gantry commands to move every displaced or captured piece
/// back to its standard starting square using the under-board electromagnet.
pub fn plan_reset_board_with_magnet(
    planner: &MovementPlanner,
    pieces: &mut Vec<PieceState>,
    initial_gantry_pos: Point2D,
) -> Vec<GantryCommand> {
    let mut cmds = Vec::new();
    let mut carriage_pos = initial_gantry_pos;

    // Standard targets
    let std_pieces = standard_board_layout();
    let targets: Vec<TargetSlot> = std_pieces
        .iter()
        .map(|p| TargetSlot {
            col: p.square.col,
            row: p.square.row,
            color: p.color,
            piece_type: p.piece_type,
        })
        .collect();

    let target_keys: HashSet<(i32, i32)> = targets.iter().map(|t| (t.col, t.row)).collect();
    let is_parked = |p: &PieceState| (p.square.col < 0 || p.square.col > 7) && !target_keys.contains(&(p.square.col, p.square.row));

    let mut assigned: HashMap<i32, (i32, i32)> = HashMap::new();

    // 1. Assign interchangeable pieces of same color and type
    for color in [Color::White, Color::Black] {
        for ptype in [
            PieceType::Pawn,
            PieceType::Knight,
            PieceType::Bishop,
            PieceType::Rook,
            PieceType::Queen,
            PieceType::King,
        ] {
            let ps: Vec<&PieceState> = pieces
                .iter()
                .filter(|p| p.color == color && p.piece_type == ptype)
                .collect();
            let mut ts: Vec<&TargetSlot> = targets
                .iter()
                .filter(|t| t.color == color && t.piece_type == ptype)
                .collect();

            // First keep pieces already on their home squares
            ts.retain(|t| {
                let home = ps.iter().find(|p| p.square.col == t.col && p.square.row == t.row);
                if let Some(home) = home {
                    assigned.insert(home.id, (t.col, t.row));
                    false
                } else {
                    true
                }
            });

            // Match remaining loose pieces by Euclidean distance
            let loose: Vec<&PieceState> = ps.into_iter().filter(|p| !assigned.contains_key(&p.id)).collect();
            let mut pairs = Vec::new();
            for p in &loose {
                for t in &ts {
                    let d = ((p.square.col - t.col).pow(2) as f32 + (p.square.row - t.row).pow(2) as f32).sqrt()
                        + if is_parked(p) { 100.0 } else { 0.0 };
                    pairs.push((d, p.id, (t.col, t.row)));
                }
            }
            pairs.sort_by(|a, b| a.0.partial_cmp(&b.0).unwrap_or(std::cmp::Ordering::Equal));

            let mut claimed_t = HashSet::new();
            for (_d, pid, t) in pairs {
                if assigned.contains_key(&pid) || claimed_t.contains(&t) {
                    continue;
                }
                assigned.insert(pid, t);
                claimed_t.insert(t);
            }
        }
    }

    // 2. Extra pieces (from promotions) go to spare graveyard slots
    let mut claimed_sq: HashSet<(i32, i32)> = assigned.values().copied().collect();
    for p in pieces.iter() {
        if assigned.contains_key(&p.id) {
            continue;
        }
        if is_parked(p) {
            assigned.insert(p.id, (p.square.col, p.square.row));
            claimed_sq.insert((p.square.col, p.square.row));
            continue;
        }
        let cols = if p.color == Color::White { [-1, -2] } else { [8, 9] };
        let mut best_spot: Option<(i32, i32)> = None;
        for c in cols {
            for r in 0..8 {
                let spot = (c, r);
                if !claimed_sq.contains(&spot)
                    && !target_keys.contains(&spot)
                    && !pieces.iter().any(|o| o.square.col == c && o.square.row == r)
                {
                    best_spot = Some(spot);
                    break;
                }
            }
            if best_spot.is_some() {
                break;
            }
        }
        if let Some(spot) = best_spot {
            assigned.insert(p.id, spot);
            claimed_sq.insert(spot);
        }
    }

    // 3. Move pieces to their targets
    let dist_to_target = |p: &PieceState| -> f32 {
        if let Some(&(tc, tr)) = assigned.get(&p.id) {
            ((p.square.col - tc).pow(2) as f32 + (p.square.row - tr).pow(2) as f32).sqrt()
        } else {
            0.0
        }
    };

    let mut pending_ids: Vec<i32> = pieces
        .iter()
        .filter(|p| assigned.contains_key(&p.id) && dist_to_target(p) > 0.0)
        .map(|p| p.id)
        .collect();

    for _guard in 0..600 {
        if pending_ids.is_empty() {
            break;
        }

        pending_ids.sort_by(|&a_id, &b_id| {
            let p_a = pieces.iter().find(|p| p.id == a_id).unwrap();
            let p_b = pieces.iter().find(|p| p.id == b_id).unwrap();
            dist_to_target(p_a).partial_cmp(&dist_to_target(p_b)).unwrap_or(std::cmp::Ordering::Equal)
        });

        let mut progressed = false;

        // Try placing any piece whose home square is currently empty
        for &pid in &pending_ids {
            let p_idx = pieces.iter().position(|p| p.id == pid).unwrap();
            let &(target_col, target_row) = assigned.get(&pid).unwrap();

            let target_occupied = pieces.iter().any(|o| o.id != pid && o.square.col == target_col && o.square.row == target_row);
            if target_occupied {
                continue;
            }

            let dest_pt = MovementPlanner::square_center(GridCoord::new(target_col, target_row));
            let in_graveyard = pieces[p_idx].square.col < 0 || pieces[p_idx].square.col > 7 || target_col < 0 || target_col > 7;
            let route = planner.plan_route(pieces, p_idx, dest_pt, !in_graveyard && pieces[p_idx].piece_type != PieceType::Knight);

            if route.cost < 1e8 {
                let desc = format!(
                    "Reset {}{}: {} -> {}",
                    pieces[p_idx].color.prefix(),
                    pieces[p_idx].piece_type.char_code(),
                    pieces[p_idx].square.to_algebraic(),
                    file_rank_label(target_col, target_row)
                );
                planner.plan_point_to_point_with_edge_nudging(
                    pieces,
                    p_idx,
                    dest_pt,
                    &mut cmds,
                    &mut carriage_pos,
                    &desc,
                    &route.waypoints,
                );
                pieces[p_idx].square = GridCoord::new(target_col, target_row);
                pending_ids.retain(|&id| id != pid);
                progressed = true;
                break;
            }
        }

        if progressed {
            continue;
        }

        // Cycle breaker: all remaining pieces need squares that are occupied.
        // Park an occupant on an empty spare square.
        let occupants: Vec<i32> = pending_ids
            .iter()
            .copied()
            .filter(|&pid| {
                let p = pieces.iter().find(|o| o.id == pid).unwrap();
                pending_ids.iter().any(|&qid| {
                    if qid == pid {
                        return false;
                    }
                    let &(qc, qr) = assigned.get(&qid).unwrap();
                    qc == p.square.col && qr == p.square.row
                })
            })
            .collect();

        for pid in occupants {
            let p_idx = pieces.iter().position(|p| p.id == pid).unwrap();
            let p_col = pieces[p_idx].square.col;
            let p_row = pieces[p_idx].square.row;

            let mut spares = Vec::new();
            for c in -2..=9 {
                for r in 0..8 {
                    if claimed_sq.contains(&(c, r)) || target_keys.contains(&(c, r)) {
                        continue;
                    }
                    if pieces.iter().any(|o| o.square.col == c && o.square.row == r) {
                        continue;
                    }
                    let d = ((c - p_col).pow(2) as f32 + (r - p_row).pow(2) as f32).sqrt();
                    spares.push((d, c, r));
                }
            }
            spares.sort_by(|a, b| a.0.partial_cmp(&b.0).unwrap_or(std::cmp::Ordering::Equal));

            let mut parked = false;
            for (_d, sc, sr) in spares.into_iter().take(8) {
                let dest_pt = MovementPlanner::square_center(GridCoord::new(sc, sr));
                let route = planner.plan_route(pieces, p_idx, dest_pt, false);
                if route.cost < 1e8 {
                    let desc = format!(
                        "Reset (park) {}{}: {} -> {}",
                        pieces[p_idx].color.prefix(),
                        pieces[p_idx].piece_type.char_code(),
                        pieces[p_idx].square.to_algebraic(),
                        file_rank_label(sc, sr)
                    );
                    planner.plan_point_to_point_with_edge_nudging(
                        pieces,
                        p_idx,
                        dest_pt,
                        &mut cmds,
                        &mut carriage_pos,
                        &desc,
                        &route.waypoints,
                    );
                    pieces[p_idx].square = GridCoord::new(sc, sr);
                    parked = true;
                    break;
                }
            }
            if parked {
                progressed = true;
                break;
            }
        }

        if !progressed {
            break;
        }
    }

    // Reset status flags
    for p in pieces.iter_mut() {
        p.has_moved = false;
        if let Some(&(tc, _)) = assigned.get(&p.id) {
            p.is_reserve = tc < 0 || tc > 7;
        }
    }

    cmds
}
