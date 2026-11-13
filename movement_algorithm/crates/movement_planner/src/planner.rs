/**
 * Project 64 - Autonomous Chessboard Movement & Pathfinding Engine
 * Target: ESP32 / Desktop / Robotics Motion Layer
 *
 * Full Lattice A* Router, Collision Avoidance, Edge Nudger, and Move Sequencer.
 */

use crate::types::*;
use std::cmp::Ordering;
use std::collections::BinaryHeap;

#[derive(Debug, Clone)]
pub struct Route {
    pub cost: f32,
    pub reachable: bool,
    pub waypoints: Vec<Point2D>, // Interior turn waypoints (empty = single straight leg)
}

#[derive(Debug, Clone)]
pub struct GraveyardTrip {
    pub slot: GridCoord,
    pub route: Route,
}

#[derive(Debug, Clone)]
struct NudgedPieceRecord {
    piece_index: usize,
    original_center: Point2D,
    nudged_pos: Point2D,
}

#[derive(Copy, Clone, PartialEq)]
struct StateCost {
    cost: f32,
    state: usize,
}

impl Eq for StateCost {}

impl Ord for StateCost {
    fn cmp(&self, other: &Self) -> Ordering {
        other.cost.partial_cmp(&self.cost).unwrap_or(Ordering::Equal)
    }
}

impl PartialOrd for StateCost {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

pub struct MovementPlanner {
    config: DimensionsConfig,
}

impl MovementPlanner {
    pub const LANE_FRONT_Y: f32 = -0.5;
    pub const LANE_BACK_Y: f32 = 8.5;

    pub fn new(config: DimensionsConfig) -> Self {
        Self { config }
    }

    pub fn set_config(&mut self, config: DimensionsConfig) {
        self.config = config;
    }

    pub fn config(&self) -> &DimensionsConfig {
        &self.config
    }

    pub fn square_center(sq: GridCoord) -> Point2D {
        Point2D::new(sq.col as f32 + 0.5, sq.row as f32 + 0.5)
    }

    pub fn min_center_distance_sq_units(&self) -> f32 {
        (2.0 * self.config.piece_radius_mm + self.config.safety_margin_mm) / self.config.square_size_mm
    }

    pub fn trigger_radius(&self) -> f32 {
        self.min_center_distance_sq_units().max(0.58)
    }

    pub fn hard_radius(&self) -> f32 {
        (self.trigger_radius() - self.config.max_edge_nudge_ratio).max(0.05) + 0.03
    }

    /// Distance from point P to segment AB, plus closest point on AB
    pub fn point_to_segment_distance(p: Point2D, a: Point2D, b: Point2D) -> (f32, Point2D) {
        let vx = b.x - a.x;
        let vy = b.y - a.y;
        let len_sq = vx * vx + vy * vy;
        let mut t = 0.0f32;
        if len_sq > 1e-6 {
            t = (((p.x - a.x) * vx + (p.y - a.y) * vy) / len_sq).clamp(0.0, 1.0);
        }
        let closest = Point2D::new(a.x + t * vx, a.y + t * vy);
        let dist = p.dist(closest);
        (dist, closest)
    }

    pub fn is_straight_path_clear(&self, pieces: &[PieceState], piece_idx: usize, dest: Point2D) -> bool {
        let s = pieces[piece_idx].current_pos;
        let dx = (dest.x - s.x).abs();
        let dy = (dest.y - s.y).abs();
        if dx > 1e-4 && dy > 1e-4 && (dx - dy).abs() > 1e-4 {
            return false;
        }
        let hard = self.hard_radius();
        for (i, p) in pieces.iter().enumerate() {
            if i == piece_idx {
                continue;
            }
            if Self::point_to_segment_distance(p.current_pos, s, dest).0 < hard {
                return false;
            }
        }
        true
    }

    /// Cheapest route over a half-square lattice covering the board, graveyards and transit lanes.
    pub fn find_route(&self, pieces: &[PieceState], piece_idx: usize, dest: Point2D) -> Route {
        const MIN_X: i32 = -4;
        const MAX_X: i32 = 20;
        const MIN_Y: i32 = -2;
        const MAX_Y: i32 = 18;
        const W: usize = (MAX_X - MIN_X + 1) as usize; // 25
        const H: usize = (MAX_Y - MIN_Y + 1) as usize; // 21
        const DIAG_MULT: f32 = 4.0;
        const TURN_PEN: f32 = 0.3;
        const NUDGE_PEN: f32 = 0.8;

        const DIRS: [[i32; 2]; 8] = [
            [1, 0], [-1, 0], [0, 1], [0, -1],
            [1, 1], [1, -1], [-1, 1], [-1, -1],
        ];

        let trigger = self.trigger_radius();
        let hard = self.hard_radius();

        let node_id = |ix: i32, iy: i32| -> usize {
            ((iy - MIN_Y) as usize) * W + ((ix - MIN_X) as usize)
        };

        // Cache for edge nudges: -2 = unvisited, -1 = hard blocked, >= 0 = num nudges
        let mut edge_cache = vec![-2i8; W * H * 8];

        let mut edge_nudges = |ix: i32, iy: i32, d: usize| -> i8 {
            let idx = node_id(ix, iy) * 8 + d;
            if edge_cache[idx] != -2 {
                return edge_cache[idx];
            }
            let a = Point2D::new(ix as f32 * 0.5, iy as f32 * 0.5);
            let b = Point2D::new((ix + DIRS[d][0]) as f32 * 0.5, (iy + DIRS[d][1]) as f32 * 0.5);
            let mut n = 0i8;
            for (i, p) in pieces.iter().enumerate() {
                if i == piece_idx {
                    continue;
                }
                let dist = Self::point_to_segment_distance(p.current_pos, a, b).0;
                if dist < hard {
                    n = -1;
                    break;
                }
                if dist < trigger {
                    n += 1;
                }
            }
            edge_cache[idx] = n;
            n
        };

        let start_pt = pieces[piece_idx].current_pos;
        let sx = (start_pt.x * 2.0).round() as i32;
        let sy = (start_pt.y * 2.0).round() as i32;
        let tx = (dest.x * 2.0).round() as i32;
        let ty = (dest.y * 2.0).round() as i32;

        let state_count = W * H * 9;
        let mut best = vec![1e30f32; state_count];
        let mut prev = vec![-1i32; state_count];
        let mut open = BinaryHeap::new();

        let start_state = node_id(sx, sy) * 9 + 8;
        best[start_state] = 0.0;
        open.push(StateCost { cost: 0.0, state: start_state });

        let mut goal_state: Option<usize> = None;

        while let Some(StateCost { cost, state: s }) = open.pop() {
            if cost > best[s] {
                continue;
            }
            let node = s / 9;
            let dir_in = s % 9;
            let ix = (node % W) as i32 + MIN_X;
            let iy = (node / W) as i32 + MIN_Y;

            if ix == tx && iy == ty {
                goal_state = Some(s);
                break;
            }

            let is_center = (ix & 1 != 0) && (iy & 1 != 0);
            let is_corner = (ix & 1 == 0) && (iy & 1 == 0);

            for d in 0..8 {
                if is_corner && dir_in >= 4 && d != dir_in {
                    continue;
                }
                if d >= 4 && !is_center && !(is_corner && dir_in == d) {
                    continue;
                }
                let nx = ix + DIRS[d][0];
                let ny = iy + DIRS[d][1];
                if nx < MIN_X || nx > MAX_X || ny < MIN_Y || ny > MAX_Y {
                    continue;
                }
                let nudges = edge_nudges(ix, iy, d);
                if nudges < 0 {
                    continue;
                }
                let step_cost = (if d >= 4 { 0.5 * std::f32::consts::SQRT_2 * DIAG_MULT } else { 0.5 })
                    + (nudges as f32) * NUDGE_PEN
                    + (if dir_in != 8 && dir_in != d { TURN_PEN } else { 0.0 });
                let ns = node_id(nx, ny) * 9 + d;
                if cost + step_cost < best[ns] {
                    best[ns] = cost + step_cost;
                    prev[ns] = s as i32;
                    open.push(StateCost { cost: best[ns], state: ns });
                }
            }
        }

        let Some(gs) = goal_state else {
            return Route {
                cost: 1e9,
                reachable: false,
                waypoints: Vec::new(),
            };
        };

        let mut route = Route {
            cost: best[gs],
            reachable: true,
            waypoints: Vec::new(),
        };

        let mut curr = gs as i32;
        let mut nodes = Vec::new();
        while curr >= 0 {
            let node = (curr as usize) / 9;
            let dir = (curr as usize) % 9;
            let x = ((node % W) as i32 + MIN_X) as f32 * 0.5;
            let y = ((node / W) as i32 + MIN_Y) as f32 * 0.5;
            nodes.push((Point2D::new(x, y), dir));
            curr = prev[curr as usize];
        }
        nodes.reverse();

        for i in 1..(nodes.len().saturating_sub(1)) {
            if nodes[i + 1].1 != nodes[i].1 {
                route.waypoints.push(nodes[i].0);
            }
        }

        route
    }

    pub fn plan_route(&self, pieces: &[PieceState], piece_idx: usize, dest: Point2D, allow_straight: bool) -> Route {
        if allow_straight && self.is_straight_path_clear(pieces, piece_idx, dest) {
            let s = pieces[piece_idx].current_pos;
            return Route {
                cost: s.dist(dest),
                reachable: true,
                waypoints: Vec::new(),
            };
        }
        let mut r = self.find_route(pieces, piece_idx, dest);
        if !r.reachable {
            r.cost = 1e9;
            r.waypoints.clear();
            let s = pieces[piece_idx].current_pos;
            let dx = (dest.x - s.x).abs();
            let dy = (dest.y - s.y).abs();
            if dx > 1e-4 && dy > 1e-4 && (dx - dy).abs() > 1e-4 {
                r.waypoints.push(Point2D::new(dest.x, s.y));
            }
        }
        r
    }

    pub fn free_graveyard_slots(
        &self,
        pieces: &[PieceState],
        piece_color: Color,
        piece_type: PieceType,
        near_row: f32,
    ) -> Vec<GridCoord> {
        let is_pawn = piece_type == PieceType::Pawn;
        let (primary_col, fallback_col) = if piece_color == Color::White {
            if is_pawn { (-2, -1) } else { (-1, -2) }
        } else {
            if is_pawn { (9, 8) } else { (8, 9) }
        };

        let heavy_order = [
            PieceType::Queen, PieceType::Queen,
            PieceType::Rook, PieceType::Rook,
            PieceType::Bishop, PieceType::Bishop,
            PieceType::Knight, PieceType::Knight,
        ];

        for &col in &[primary_col, fallback_col] {
            let mut free = Vec::new();
            for r in 0..8 {
                let occupied = pieces.iter().any(|p| p.square.col == col && p.square.row == r);
                let slot_type = if col == -2 || col == 9 {
                    PieceType::Pawn
                } else {
                    heavy_order[if col < 4 { (7 - r) as usize } else { r as usize }]
                };
                if !occupied && slot_type == piece_type {
                    free.push(GridCoord::new(col, r));
                }
            }
            if free.is_empty() {
                for r in 0..8 {
                    let occupied = pieces.iter().any(|p| p.square.col == col && p.square.row == r);
                    if !occupied {
                        free.push(GridCoord::new(col, r));
                    }
                }
            }
            if !free.is_empty() {
                free.sort_by(|a, b| {
                    let da = (a.row as f32 - near_row).abs();
                    let db = (b.row as f32 - near_row).abs();
                    da.partial_cmp(&db).unwrap_or(Ordering::Equal)
                });
                return free;
            }
        }

        vec![GridCoord::new(primary_col, 0)]
    }

    pub fn choose_graveyard_trip(&self, pieces: &[PieceState], piece_idx: usize) -> GraveyardTrip {
        let piece = &pieces[piece_idx];
        let mut best: Option<GraveyardTrip> = None;
        let slots = self.free_graveyard_slots(pieces, piece.color, piece.piece_type, piece.current_pos.y - 0.5);

        for slot in slots {
            let route = self.plan_route(pieces, piece_idx, Self::square_center(slot), false);
            if best.as_ref().map_or(true, |b| route.cost < b.route.cost) {
                best = Some(GraveyardTrip { slot, route });
            }
        }

        best.unwrap_or(GraveyardTrip {
            slot: GridCoord::new(if piece.color == Color::White { -2 } else { 9 }, 0),
            route: Route { cost: 1e9, reachable: false, waypoints: Vec::new() },
        })
    }

    pub fn find_promotion_piece_in_reserve(
        &self,
        pieces: &[PieceState],
        color: Color,
        desired_type: PieceType,
        target: Point2D,
    ) -> Option<usize> {
        let mut best: Option<usize> = None;
        let mut best_dist = 1e9f32;

        for (i, p) in pieces.iter().enumerate() {
            if p.color == color && p.piece_type == desired_type && (p.square.col < 0 || p.square.col > 7) {
                let d = p.current_pos.dist(target);
                if d < best_dist {
                    best_dist = d;
                    best = Some(i);
                }
            }
        }
        best
    }

    pub fn plan_point_to_point_with_edge_nudging(
        &self,
        pieces: &mut [PieceState],
        moving_piece_idx: usize,
        dest_pt: Point2D,
        cmds: &mut Vec<GantryCommand>,
        carriage_pos: &mut Point2D,
        move_label: &str,
        waypoints: &[Point2D],
    ) {
        let start_pt = pieces[moving_piece_idx].current_pos;
        let min_clearance = self.min_center_distance_sq_units();
        let max_nudge = self.config.max_edge_nudge_ratio;

        let mut path = Vec::with_capacity(waypoints.len() + 2);
        path.push(start_pt);
        path.extend_from_slice(waypoints);
        path.push(dest_pt);

        if path.len() == 2 && start_pt.dist(dest_pt) < 1e-4 {
            return;
        }

        // 1. Identify pieces within clearance of any leg of the path
        let mut nudged_list: Vec<NudgedPieceRecord> = Vec::new();

        for leg in 0..(path.len() - 1) {
            let leg_a = path[leg];
            let leg_b = path[leg + 1];
            let seg_dx = leg_b.x - leg_a.x;
            let seg_dy = leg_b.y - leg_a.y;
            let seg_len = (seg_dx * seg_dx + seg_dy * seg_dy).sqrt();
            if seg_len < 1e-4 {
                continue;
            }

            let nx = -seg_dy / seg_len;
            let ny = seg_dx / seg_len;

            for i in 0..pieces.len() {
                if i == moving_piece_idx {
                    continue;
                }
                if nudged_list.iter().any(|r| r.piece_index == i) {
                    continue;
                }

                let p_pos = pieces[i].current_pos;
                let (dist, closest_pt) = Self::point_to_segment_distance(p_pos, leg_a, leg_b);
                let t = ((closest_pt.x - leg_a.x) * seg_dx + (closest_pt.y - leg_a.y) * seg_dy) / (seg_len * seg_len);

                if leg == 0 && t <= 0.01 {
                    continue;
                }
                if leg + 2 == path.len() && t >= 0.99 {
                    continue;
                }

                let trigger_dist = min_clearance.max(0.58);
                if dist < trigger_dist {
                    let off_x = p_pos.x - closest_pt.x;
                    let off_y = p_pos.y - closest_pt.y;
                    let off_len = (off_x * off_x + off_y * off_y).sqrt();

                    let (push_dir_x, push_dir_y) = if off_len > 1e-3 {
                        (off_x / off_len, off_y / off_len)
                    } else {
                        (nx, ny)
                    };

                    let sq_center = Self::square_center(pieces[i].square);
                    let mut target_nudge = Point2D::new(
                        (sq_center.x + push_dir_x * max_nudge).clamp(sq_center.x - max_nudge, sq_center.x + max_nudge),
                        (sq_center.y + push_dir_y * max_nudge).clamp(sq_center.y - max_nudge, sq_center.y + max_nudge),
                    );

                    let candidates = [
                        Point2D::new(sq_center.x + max_nudge, sq_center.y),
                        Point2D::new(sq_center.x - max_nudge, sq_center.y),
                        Point2D::new(sq_center.x, sq_center.y + max_nudge),
                        Point2D::new(sq_center.x, sq_center.y - max_nudge),
                    ];
                    let mut best_d = -1.0f32;
                    for c in candidates {
                        let cd = Self::point_to_segment_distance(c, leg_a, leg_b).0;
                        if cd > best_d {
                            best_d = cd;
                            target_nudge = c;
                        }
                    }

                    nudged_list.push(NudgedPieceRecord {
                        piece_index: i,
                        original_center: p_pos,
                        nudged_pos: target_nudge,
                    });
                }
            }
        }

        // 2. Nudge blocking pieces to the edge of their squares
        for rec in &nudged_list {
            cmds.push(GantryCommand {
                action: CommandAction::TravelMagnetOff,
                from: *carriage_pos,
                to: rec.original_center,
                description: "Travel to blocking piece".to_string(),
            });
            cmds.push(GantryCommand {
                action: CommandAction::MagnetOn,
                from: rec.original_center,
                to: rec.original_center,
                description: "Engage magnet on blocker".to_string(),
            });
            cmds.push(GantryCommand {
                action: CommandAction::MovePieceStraight,
                from: rec.original_center,
                to: rec.nudged_pos,
                description: "Nudge piece to square edge for clearance".to_string(),
            });
            cmds.push(GantryCommand {
                action: CommandAction::MagnetOff,
                from: rec.nudged_pos,
                to: rec.nudged_pos,
                description: "Release nudged piece at square edge".to_string(),
            });
            *carriage_pos = rec.nudged_pos;
            pieces[rec.piece_index].current_pos = rec.nudged_pos;
        }

        // 3. Move the active piece along the path
        cmds.push(GantryCommand {
            action: CommandAction::TravelMagnetOff,
            from: *carriage_pos,
            to: start_pt,
            description: "Travel to active piece".to_string(),
        });
        cmds.push(GantryCommand {
            action: CommandAction::MagnetOn,
            from: start_pt,
            to: start_pt,
            description: "Engage magnet on active piece".to_string(),
        });
        for leg in 0..(path.len() - 1) {
            cmds.push(GantryCommand {
                action: CommandAction::MovePieceStraight,
                from: path[leg],
                to: path[leg + 1],
                description: move_label.to_string(),
            });
        }
        cmds.push(GantryCommand {
            action: CommandAction::MagnetOff,
            from: dest_pt,
            to: dest_pt,
            description: "Release active piece at destination".to_string(),
        });
        *carriage_pos = dest_pt;
        pieces[moving_piece_idx].current_pos = dest_pt;

        // 4. Restore nudged pieces in reverse order
        for rec in nudged_list.iter().rev() {
            cmds.push(GantryCommand {
                action: CommandAction::TravelMagnetOff,
                from: *carriage_pos,
                to: rec.nudged_pos,
                description: "Return to nudged piece".to_string(),
            });
            cmds.push(GantryCommand {
                action: CommandAction::MagnetOn,
                from: rec.nudged_pos,
                to: rec.nudged_pos,
                description: "Engage magnet on nudged piece".to_string(),
            });
            cmds.push(GantryCommand {
                action: CommandAction::MovePieceStraight,
                from: rec.nudged_pos,
                to: rec.original_center,
                description: "Restore piece from square edge to center".to_string(),
            });
            cmds.push(GantryCommand {
                action: CommandAction::MagnetOff,
                from: rec.original_center,
                to: rec.original_center,
                description: "Release restored piece".to_string(),
            });
            *carriage_pos = rec.original_center;
            pieces[rec.piece_index].current_pos = rec.original_center;
        }
    }

    /// Plans complete collision-free gantry commands for any chess move
    pub fn plan_move(&self, pieces: &mut Vec<PieceState>, mv: &MoveRequest) -> Vec<GantryCommand> {
        let mut cmds = Vec::new();
        let mut carriage_pos = Self::square_center(mv.from);

        let moving_idx = pieces.iter().position(|p| p.square == mv.from);
        let Some(moving_idx) = moving_idx else {
            return cmds;
        };

        // Step A: Handle Capture
        if mv.is_capture {
            let cap_sq = mv.captured_square.unwrap_or(mv.to);
            let cap_idx = pieces.iter().position(|p| p.square == cap_sq && p.id != pieces[moving_idx].id);

            if let Some(cap_idx) = cap_idx {
                let trip = self.choose_graveyard_trip(pieces, cap_idx);
                let grave_pt = Self::square_center(trip.slot);
                self.plan_point_to_point_with_edge_nudging(
                    pieces,
                    cap_idx,
                    grave_pt,
                    &mut cmds,
                    &mut carriage_pos,
                    "Escort captured piece to side graveyard slot",
                    &trip.route.waypoints,
                );
                pieces[cap_idx].square = trip.slot;
            }
        }

        // Step B: Execute Primary Piece Move
        let dest_pt = Self::square_center(mv.to);
        {
            let is_knight = pieces[moving_idx].piece_type == PieceType::Knight;
            let route = self.plan_route(pieces, moving_idx, dest_pt, !is_knight);
            self.plan_point_to_point_with_edge_nudging(
                pieces,
                moving_idx,
                dest_pt,
                &mut cmds,
                &mut carriage_pos,
                if is_knight { "Move knight along X/Y legs" } else { "Move active piece" },
                &route.waypoints,
            );
            pieces[moving_idx].square = mv.to;
            pieces[moving_idx].has_moved = true;
        }

        // Step C: Handle Castling
        if mv.is_castling {
            if let (Some(rook_from), Some(rook_to)) = (mv.rook_from, mv.rook_to) {
                if let Some(rook_idx) = pieces.iter().position(|p| p.square == rook_from) {
                    let rook_dest = Self::square_center(rook_to);
                    let rook_route = self.plan_route(pieces, rook_idx, rook_dest, true);
                    self.plan_point_to_point_with_edge_nudging(
                        pieces,
                        rook_idx,
                        rook_dest,
                        &mut cmds,
                        &mut carriage_pos,
                        "Castle: Move Rook past King",
                        &rook_route.waypoints,
                    );
                    pieces[rook_idx].square = rook_to;
                    pieces[rook_idx].has_moved = true;
                }
            }
        }

        // Step D: Handle Promotion
        if mv.is_promotion {
            let pawn_trip = self.choose_graveyard_trip(pieces, moving_idx);
            self.plan_point_to_point_with_edge_nudging(
                pieces,
                moving_idx,
                Self::square_center(pawn_trip.slot),
                &mut cmds,
                &mut carriage_pos,
                "Promotion: Escort promoted pawn to outer pawn slot",
                &pawn_trip.route.waypoints,
            );
            pieces[moving_idx].square = pawn_trip.slot;

            let promo_idx = self.find_promotion_piece_in_reserve(
                pieces,
                pieces[moving_idx].color,
                mv.promotion_type,
                dest_pt,
            );
            if let Some(promo_idx) = promo_idx {
                let route = self.plan_route(pieces, promo_idx, dest_pt, false);
                self.plan_point_to_point_with_edge_nudging(
                    pieces,
                    promo_idx,
                    dest_pt,
                    &mut cmds,
                    &mut carriage_pos,
                    "Promotion: Deliver promoted piece from reserve slot",
                    &route.waypoints,
                );
                pieces[promo_idx].square = mv.to;
                pieces[promo_idx].has_moved = true;
                pieces[promo_idx].is_reserve = false;
            }
        }

        cmds
    }
}
