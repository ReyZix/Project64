/**
 * Project 64 - Autonomous Chessboard 3D Simulator & Movement Planner
 * Native Rust Implementation using Macroquad (Zero JS / Zero TS)
 */

use macroquad::prelude::*;
use movement_planner::{
    parse_algebraic_square, piece_diameter_mm, plan_reset_board_with_magnet, scenarios,
    standard_board_layout, ChessBoard, ChessMove, CommandAction, DimensionsConfig,
    GantryCommand, GameState, GridCoord, MovementPlanner, MoveRequest, PieceColor, PieceState,
    PieceType, Point2D,
};

// Camera state for 3D orbital view
struct OrbitCamera {
    yaw: f32,      // radians
    pitch: f32,    // radians
    distance: f32, // distance from target
    target: Vec3,
}

impl OrbitCamera {
    fn new() -> Self {
        Self {
            yaw: 0.0,
            pitch: 45.0f32.to_radians(),
            distance: 14.5,
            target: vec3(3.5, 0.0, 3.5),
        }
    }

    fn position(&self) -> Vec3 {
        let x = self.target.x + self.distance * self.pitch.cos() * self.yaw.sin();
        let y = self.target.y + self.distance * self.pitch.sin();
        let z = self.target.z + self.distance * self.pitch.cos() * self.yaw.cos();
        vec3(x, y, z)
    }

    fn set_preset(&mut self, preset: &str) {
        match preset {
            "white" => {
                self.yaw = 0.0;
                self.pitch = 45.0f32.to_radians();
                self.distance = 14.5;
                self.target = vec3(3.5, 0.0, 3.5);
            }
            "black" => {
                self.yaw = 180.0f32.to_radians();
                self.pitch = 45.0f32.to_radians();
                self.distance = 14.5;
                self.target = vec3(3.5, 0.0, 3.5);
            }
            "top" => {
                self.yaw = 0.0;
                self.pitch = 89.5f32.to_radians();
                self.distance = 15.0;
                self.target = vec3(3.5, 0.0, 3.5);
            }
            "under" => {
                self.yaw = 0.0;
                self.pitch = -30.0f32.to_radians();
                self.distance = 14.0;
                self.target = vec3(3.5, 0.0, 3.5);
            }
            "side" => {
                self.yaw = 90.0f32.to_radians();
                self.pitch = 20.0f32.to_radians();
                self.distance = 14.0;
                self.target = vec3(3.5, 0.0, 3.5);
            }
            _ => {}
        }
    }
}

struct SimState {
    planner: MovementPlanner,
    board: ChessBoard,
    pieces: Vec<PieceState>,
    gantry_pos: Point2D,
    magnet_on: bool,
    carried_piece_id: Option<i32>,
    command_queue: Vec<GantryCommand>,
    active_cmd_idx: usize,
    active_cmd_progress: f32, // 0.0 to 1.0
    paused: bool,
    speed_mult: f32,
    selected_sq: Option<GridCoord>,
    legal_moves: Vec<ChessMove>,
    algebraic_input: String,
    promotion_choice: PieceType,
    show_radii: bool,
    #[allow(dead_code)]
    show_xray: bool,
    hud_message: String,
    last_mouse_pos: Vec2,
}

impl SimState {
    fn new() -> Self {
        let mut config = DimensionsConfig::default();
        config.square_size_mm = 60.0;
        let pieces = standard_board_layout();
        let mut board = ChessBoard::new();
        board.sync_from_pieces(&pieces);

        Self {
            planner: MovementPlanner::new(config),
            board,
            pieces,
            gantry_pos: Point2D::new(4.5, 0.5),
            magnet_on: false,
            carried_piece_id: None,
            command_queue: Vec::new(),
            active_cmd_idx: 0,
            active_cmd_progress: 0.0,
            paused: false,
            speed_mult: 1.5,
            selected_sq: None,
            legal_moves: Vec::new(),
            algebraic_input: String::new(),
            promotion_choice: PieceType::Queen,
            show_radii: true,
            show_xray: false,
            hud_message: "Ready. Click a piece or select a scenario below.".to_string(),
            last_mouse_pos: vec2(0.0, 0.0),
        }
    }

    fn reset_standard(&mut self) {
        self.command_queue.clear();
        self.active_cmd_idx = 0;
        self.active_cmd_progress = 0.0;
        self.magnet_on = false;
        self.carried_piece_id = None;
        self.pieces = standard_board_layout();
        self.board = ChessBoard::new();
        self.board.sync_from_pieces(&self.pieces);
        self.selected_sq = None;
        self.legal_moves.clear();
        self.hud_message = "Board reset to standard layout.".to_string();
    }

    fn queue_move(&mut self, req: MoveRequest) {
        self.selected_sq = None;
        self.legal_moves.clear();
        let cmds = self.planner.plan_move(&mut self.pieces, &req);
        if cmds.is_empty() {
            self.hud_message = "No commands generated for move.".to_string();
            return;
        }
        self.command_queue = cmds;
        self.active_cmd_idx = 0;
        self.active_cmd_progress = 0.0;
        self.paused = false;
        self.hud_message = format!("Executing move: {} -> {}", req.from.to_algebraic(), req.to.to_algebraic());
    }

    fn trigger_magnet_reset(&mut self) {
        self.selected_sq = None;
        self.legal_moves.clear();
        let cmds = plan_reset_board_with_magnet(&self.planner, &mut self.pieces, self.gantry_pos);
        if cmds.is_empty() {
            self.hud_message = "Board is already in starting position.".to_string();
            return;
        }
        self.command_queue = cmds;
        self.active_cmd_idx = 0;
        self.active_cmd_progress = 0.0;
        self.board = ChessBoard::new();
        self.board.sync_from_pieces(&self.pieces);
        self.paused = false;
        self.hud_message = format!("Resetting board with electromagnet ({} steps)...", self.command_queue.len());
    }

    fn select_square(&mut self, sq: GridCoord) {
        if self.selected_sq == Some(sq) {
            self.selected_sq = None;
            self.legal_moves.clear();
            return;
        }

        // Check if user clicked a destination square of a legal move
        if let Some(mv) = self.legal_moves.iter().find(|m| m.to == sq).cloned() {
            let mut req = self.board.to_move_request(&mv);
            if req.is_promotion {
                req.promotion_type = self.promotion_choice;
            }
            self.board.make_move(&mv);
            self.queue_move(req);
            return;
        }

        // Otherwise select piece on square
        if sq.is_on_board() {
            let moves = self.board.legal_moves_for_square(sq.col, sq.row);
            if !moves.is_empty() {
                self.selected_sq = Some(sq);
                self.legal_moves = moves;
                self.hud_message = format!("Selected {}. Pick a highlighted destination square.", sq.to_algebraic());
            } else {
                self.selected_sq = None;
                self.legal_moves.clear();
            }
        } else {
            self.selected_sq = None;
            self.legal_moves.clear();
        }
    }

    fn execute_algebraic(&mut self) {
        let input = self.algebraic_input.trim().to_lowercase();
        if input.len() < 4 {
            self.hud_message = "Invalid move format. Use e.g. 'e2e4' or 'g1f3'".to_string();
            return;
        }
        let from_sq = parse_algebraic_square(&input[0..2]);
        let to_sq = parse_algebraic_square(&input[2..4]);

        if let (Some(f), Some(t)) = (from_sq, to_sq) {
            let moves = self.board.legal_moves_for_square(f.col, f.row);
            if let Some(mv) = moves.iter().find(|m| m.to == t).cloned() {
                let mut req = self.board.to_move_request(&mv);
                if req.is_promotion {
                    req.promotion_type = self.promotion_choice;
                }
                self.board.make_move(&mv);
                self.queue_move(req);
                self.algebraic_input.clear();
            } else {
                self.hud_message = format!("Move {} -> {} is not legal for side to move.", f.to_algebraic(), t.to_algebraic());
            }
        } else {
            self.hud_message = "Could not parse square notation. Example: e2e4".to_string();
        }
    }

    fn update_animation(&mut self, dt: f32) {
        if self.paused || self.active_cmd_idx >= self.command_queue.len() {
            return;
        }

        let cmd = &self.command_queue[self.active_cmd_idx];
        let speed = match cmd.action {
            CommandAction::MovePieceStraight => self.planner.config().gantry_carry_speed_mm_s,
            CommandAction::TravelMagnetOff => self.planner.config().gantry_travel_speed_mm_s,
            _ => 150.0,
        };

        let seg_len_mm = cmd.from.dist(cmd.to) * self.planner.config().square_size_mm;
        let duration = if seg_len_mm < 0.1 {
            0.15 // dwell time for magnet on/off
        } else {
            seg_len_mm / (speed * self.speed_mult).max(1.0)
        };

        self.active_cmd_progress += dt / duration.max(0.01);

        if self.active_cmd_progress >= 1.0 {
            // Finalize command state
            match cmd.action {
                CommandAction::MagnetOn => {
                    self.magnet_on = true;
                    // Find piece directly at carriage position
                    self.carried_piece_id = self.pieces.iter().find(|p| p.current_pos.dist(cmd.to) < 0.25).map(|p| p.id);
                }
                CommandAction::MagnetOff => {
                    self.magnet_on = false;
                    if let Some(pid) = self.carried_piece_id {
                        if let Some(p) = self.pieces.iter_mut().find(|p| p.id == pid) {
                            p.current_pos = cmd.to;
                        }
                    }
                    self.carried_piece_id = None;
                }
                CommandAction::MovePieceStraight => {
                    self.gantry_pos = cmd.to;
                    if let Some(pid) = self.carried_piece_id {
                        if let Some(p) = self.pieces.iter_mut().find(|p| p.id == pid) {
                            p.current_pos = cmd.to;
                        }
                    }
                }
                CommandAction::TravelMagnetOff => {
                    self.gantry_pos = cmd.to;
                }
            }

            self.active_cmd_idx += 1;
            self.active_cmd_progress = 0.0;

            if self.active_cmd_idx >= self.command_queue.len() {
                self.hud_message = "All gantry commands executed.".to_string();
            }
        } else {
            // Interpolate carriage and carried piece
            let t = self.active_cmd_progress.clamp(0.0, 1.0);
            let cur_x = cmd.from.x + t * (cmd.to.x - cmd.from.x);
            let cur_y = cmd.from.y + t * (cmd.to.y - cmd.from.y);
            self.gantry_pos = Point2D::new(cur_x, cur_y);

            if self.magnet_on {
                if let Some(pid) = self.carried_piece_id {
                    if let Some(p) = self.pieces.iter_mut().find(|p| p.id == pid) {
                        p.current_pos = self.gantry_pos;
                    }
                }
            }
        }
    }
}

// ----------------------------------------------------------------------------
// 3D Rendering Helpers
// ----------------------------------------------------------------------------

fn draw_cylinder_3d(center: Vec3, radius: f32, height: f32, col: Color) {
    let segments = 24;
    let half_h = height * 0.5;
    for i in 0..segments {
        let a1 = (i as f32 / segments as f32) * std::f32::consts::TAU;
        let a2 = ((i + 1) as f32 / segments as f32) * std::f32::consts::TAU;
        let p1 = center + vec3(a1.cos() * radius, -half_h, a1.sin() * radius);
        let p2 = center + vec3(a2.cos() * radius, -half_h, a2.sin() * radius);
        let p3 = center + vec3(a2.cos() * radius, half_h, a2.sin() * radius);
        let p4 = center + vec3(a1.cos() * radius, half_h, a1.sin() * radius);

        // Sides
        draw_line_3d(p1, p2, col);
        draw_line_3d(p2, p3, col);
        draw_line_3d(p3, p4, col);
        draw_line_3d(p4, p1, col);

        // Top and bottom rings
        draw_line_3d(center + vec3(0.0, half_h, 0.0), p3, col);
        draw_line_3d(center + vec3(0.0, -half_h, 0.0), p1, col);
    }
}

fn draw_chess_piece_3d(p: &PieceState, sq_mm: f32, show_radii: bool) {
    let px = p.current_pos.x;
    let pz = p.current_pos.y;
    let base_dia = piece_diameter_mm(p.piece_type);
    let base_r = (base_dia / 2.0) / sq_mm;

    let (piece_color, accent_color) = if p.color == PieceColor::White {
        (Color::new(0.92, 0.88, 0.78, 1.0), Color::new(0.78, 0.72, 0.60, 1.0))
    } else {
        (Color::new(0.18, 0.20, 0.25, 1.0), Color::new(0.35, 0.38, 0.45, 1.0))
    };

    let base_h = 0.12f32;
    let base_y = base_h * 0.5;

    // Piece Base
    draw_cylinder_3d(vec3(px, base_y, pz), base_r, base_h, piece_color);
    draw_cylinder_3d(vec3(px, base_y + 0.05, pz), base_r * 0.88, 0.04, accent_color);

    // Procedural body per piece type
    match p.piece_type {
        PieceType::Pawn => {
            // Conical stem
            draw_cylinder_3d(vec3(px, 0.32, pz), base_r * 0.45, 0.32, piece_color);
            // Collar ring
            draw_cylinder_3d(vec3(px, 0.50, pz), base_r * 0.58, 0.06, accent_color);
            // Head sphere
            draw_sphere(vec3(px, 0.64, pz), base_r * 0.48, None, piece_color);
        }
        PieceType::Knight => {
            // Pedestal
            draw_cylinder_3d(vec3(px, 0.30, pz), base_r * 0.55, 0.28, piece_color);
            // Angled head block
            draw_cube(vec3(px, 0.58, pz), vec3(base_r * 0.7, 0.36, base_r * 0.9), None, piece_color);
            // Mane & muzzle
            draw_cube(vec3(px, 0.70, pz - 0.06), vec3(base_r * 0.5, 0.20, base_r * 0.5), None, accent_color);
        }
        PieceType::Bishop => {
            // Fluted column
            draw_cylinder_3d(vec3(px, 0.38, pz), base_r * 0.48, 0.44, piece_color);
            draw_cylinder_3d(vec3(px, 0.62, pz), base_r * 0.55, 0.06, accent_color);
            // Mitre oval
            draw_cylinder_3d(vec3(px, 0.76, pz), base_r * 0.45, 0.24, piece_color);
            // Top finial ball
            draw_sphere(vec3(px, 0.92, pz), base_r * 0.16, None, accent_color);
        }
        PieceType::Rook => {
            // Stout tower column
            draw_cylinder_3d(vec3(px, 0.40, pz), base_r * 0.62, 0.52, piece_color);
            // Castle parapet / battlements
            draw_cylinder_3d(vec3(px, 0.70, pz), base_r * 0.72, 0.14, accent_color);
            draw_cube(vec3(px, 0.78, pz), vec3(base_r * 0.8, 0.08, base_r * 0.8), None, piece_color);
        }
        PieceType::Queen => {
            // Elegant flared column
            draw_cylinder_3d(vec3(px, 0.44, pz), base_r * 0.52, 0.60, piece_color);
            draw_cylinder_3d(vec3(px, 0.74, pz), base_r * 0.68, 0.08, accent_color);
            // Coronet crown
            draw_cylinder_3d(vec3(px, 0.88, pz), base_r * 0.74, 0.20, piece_color);
            draw_sphere(vec3(px, 1.02, pz), base_r * 0.20, None, accent_color);
        }
        PieceType::King => {
            // Royal stepped body
            draw_cylinder_3d(vec3(px, 0.46, pz), base_r * 0.56, 0.64, piece_color);
            draw_cylinder_3d(vec3(px, 0.78, pz), base_r * 0.72, 0.10, accent_color);
            // Dome capital
            draw_cylinder_3d(vec3(px, 0.92, pz), base_r * 0.68, 0.20, piece_color);
            // Royal Cross
            draw_cube(vec3(px, 1.08, pz), vec3(0.06, 0.22, 0.06), None, accent_color);
            draw_cube(vec3(px, 1.12, pz), vec3(0.18, 0.06, 0.06), None, accent_color);
        }
        PieceType::None => {}
    }

    // Clearance and safety radii circles on board surface
    if show_radii {
        let margin_r = base_r + (3.0 / sq_mm);
        let segs = 32;
        for i in 0..segs {
            let a1 = (i as f32 / segs as f32) * std::f32::consts::TAU;
            let a2 = ((i + 1) as f32 / segs as f32) * std::f32::consts::TAU;
            // Physical base
            draw_line_3d(
                vec3(px + a1.cos() * base_r, 0.01, pz + a1.sin() * base_r),
                vec3(px + a2.cos() * base_r, 0.01, pz + a2.sin() * base_r),
                Color::new(0.2, 0.8, 0.9, 0.5),
            );
            // Safety margin
            draw_line_3d(
                vec3(px + a1.cos() * margin_r, 0.01, pz + a1.sin() * margin_r),
                vec3(px + a2.cos() * margin_r, 0.01, pz + a2.sin() * margin_r),
                Color::new(1.0, 0.7, 0.2, 0.35),
            );
        }
    }
}

fn draw_board_3d(sim: &SimState) {
    let light_sq = Color::new(0.85, 0.82, 0.75, 1.0);
    let dark_sq = Color::new(0.38, 0.44, 0.35, 1.0);
    let frame_col = Color::new(0.22, 0.16, 0.12, 1.0);
    let gy_col = Color::new(0.15, 0.18, 0.24, 0.9);
    let lane_col = Color::new(0.12, 0.22, 0.30, 0.85);

    // Frame bevel
    draw_cube(vec3(3.5, -0.15, 3.5), vec3(14.5, 0.25, 11.5), None, frame_col);

    // 8x8 Playable board
    for r in 0..8 {
        for c in 0..8 {
            let is_light = (c + r) % 2 != 0;
            let mut col = if is_light { light_sq } else { dark_sq };

            // Selection highlight
            if sim.selected_sq == Some(GridCoord::new(c, r)) {
                col = Color::new(0.2, 0.85, 0.95, 1.0);
            } else if sim.legal_moves.iter().any(|m| m.to == GridCoord::new(c, r)) {
                col = Color::new(0.25, 0.90, 0.45, 1.0);
            }

            draw_cube(vec3(c as f32 + 0.5, -0.01, r as f32 + 0.5), vec3(0.98, 0.04, 0.98), None, col);
        }
    }

    // Side Graveyards: Left (cols -2, -1) and Right (cols 8, 9)
    for r in 0..8 {
        for c in [-2, -1, 8, 9] {
            let center = vec3(c as f32 + 0.5, -0.01, r as f32 + 0.5);
            draw_cube(center, vec3(0.94, 0.03, 0.94), None, gy_col);
            draw_cylinder_3d(vec3(center.x, 0.01, center.z), 0.32, 0.01, Color::new(0.4, 0.5, 0.65, 0.6));
        }
    }

    // Front transit lane (row -1, center y = -0.5) and Back transit lane (row 8, center y = 8.5)
    for c in -2..=9 {
        draw_cube(vec3(c as f32 + 0.5, -0.01, -0.5), vec3(0.96, 0.02, 0.96), None, lane_col);
        draw_cube(vec3(c as f32 + 0.5, -0.01, 8.5), vec3(0.96, 0.02, 0.96), None, lane_col);
    }

    // Draw planned path waypoints
    if sim.active_cmd_idx < sim.command_queue.len() {
        for i in sim.active_cmd_idx..sim.command_queue.len() {
            let c = &sim.command_queue[i];
            let p1 = vec3(c.from.x, 0.03, c.from.y);
            let p2 = vec3(c.to.x, 0.03, c.to.y);
            let path_col = match c.action {
                CommandAction::MovePieceStraight => Color::new(0.1, 0.95, 0.4, 0.8),
                _ => Color::new(0.95, 0.6, 0.1, 0.5),
            };
            draw_line_3d(p1, p2, path_col);
            draw_sphere(p2, 0.06, None, path_col);
        }
    }
}

fn draw_under_board_gantry_3d(sim: &SimState) {
    let gx = sim.gantry_pos.x;
    let gz = sim.gantry_pos.y;
    let rail_y = -0.55;

    let metal_col = Color::new(0.45, 0.48, 0.52, 1.0);
    let carriage_col = Color::new(0.25, 0.28, 0.32, 1.0);

    // X Rail moving carriage
    draw_cube(vec3(gx, rail_y, 3.5), vec3(0.35, 0.15, 11.2), None, metal_col);

    // Carriage base plate
    draw_cube(vec3(gx, rail_y + 0.12, gz), vec3(0.70, 0.10, 0.70), None, carriage_col);

    // 12V Electromagnet
    let mag_col = if sim.magnet_on {
        Color::new(1.0, 0.35, 0.05, 1.0) // Glowing amber/red
    } else {
        Color::new(0.15, 0.55, 0.75, 0.8) // Standby cyan
    };

    draw_cylinder_3d(vec3(gx, rail_y + 0.26, gz), 0.28, 0.18, mag_col);

    // Glowing halo when magnet is engaged
    if sim.magnet_on {
        let pulse = (get_time() as f32 * 8.0).sin().abs() * 0.15;
        draw_sphere(vec3(gx, rail_y + 0.32, gz), 0.35 + pulse, None, Color::new(1.0, 0.45, 0.1, 0.4));
    }
}

// ----------------------------------------------------------------------------
// Raycasting for 3D Board Picking
// ----------------------------------------------------------------------------

fn pick_square_at_screen(mouse: Vec2, cam: &OrbitCamera, width: f32, height: f32) -> Option<GridCoord> {
    let ndc_x = (2.0 * mouse.x / width) - 1.0;
    let ndc_y = 1.0 - (2.0 * mouse.y / height);

    let fov_rad = 45.0f32.to_radians();
    let aspect = width / height;
    let tan_half_fov = (fov_rad * 0.5).tan();

    let cam_pos = cam.position();
    let forward = (cam.target - cam_pos).normalize();
    let right = forward.cross(vec3(0.0, 1.0, 0.0)).normalize();
    let up = right.cross(forward).normalize();

    let ray_dir = (forward + right * (ndc_x * tan_half_fov * aspect) + up * (ndc_y * tan_half_fov)).normalize();

    if ray_dir.y.abs() < 1e-4 {
        return None;
    }

    let t = -cam_pos.y / ray_dir.y;
    if t <= 0.0 {
        return None;
    }

    let hit_x = cam_pos.x + t * ray_dir.x;
    let hit_z = cam_pos.z + t * ray_dir.z;

    let col = hit_x.floor() as i32;
    let row = hit_z.floor() as i32;

    if col >= -2 && col <= 9 && row >= -1 && row <= 8 {
        Some(GridCoord::new(col, row))
    } else {
        None
    }
}

// ----------------------------------------------------------------------------
// 2D GUI and HUD Elements
// ----------------------------------------------------------------------------

fn draw_gui(sim: &mut SimState, cam: &mut OrbitCamera) {
    let sw = screen_width();
    let sh = screen_height();

    // Top Header Banner
    draw_rectangle(12.0, 12.0, sw - 24.0, 52.0, Color::new(0.08, 0.11, 0.16, 0.95));
    draw_rectangle_lines(12.0, 12.0, sw - 24.0, 52.0, 1.5, Color::new(0.2, 0.25, 0.35, 1.0));

    draw_text("PROJECT 64", 24.0, 42.0, 20.0, Color::new(0.3, 0.9, 0.6, 1.0));
    draw_text("Autonomous Chessboard 3D Simulator & Movement Planner", 150.0, 42.0, 18.0, WHITE);

    let turn_text = match sim.board.state {
        GameState::Checkmate => "STATUS: CHECKMATE!",
        GameState::Stalemate => "STATUS: STALEMATE",
        GameState::Check => "STATUS: CHECK!",
        GameState::Active => if sim.board.turn == PieceColor::White { "Turn: White" } else { "Turn: Black" },
    };
    let turn_col = match sim.board.state {
        GameState::Checkmate => RED,
        GameState::Check => Color::new(1.0, 0.6, 0.1, 1.0),
        _ => Color::new(0.3, 0.8, 1.0, 1.0),
    };
    draw_text(turn_text, sw - 240.0, 42.0, 18.0, turn_col);

    // Scenario Demo Buttons Bar
    let scenarios = [
        ("Knight Nudge", "knight_nudge"),
        ("Bishop Diag", "bishop_diagonal"),
        ("Capture GY", "capture_graveyard"),
        ("Castling", "castling"),
        ("Promotion", "promotion"),
        ("Magnet Reset", "reset_magnet"),
        ("Instant Reset", "reset_instant"),
    ];

    let btn_y = 72.0;
    let mut btn_x = 12.0;
    let btn_w = 115.0;
    let btn_h = 32.0;

    for (label, key) in scenarios {
        let mouse = mouse_position();
        let hovered = mouse.0 >= btn_x && mouse.0 <= btn_x + btn_w && mouse.1 >= btn_y && mouse.1 <= btn_y + btn_h;
        let bg_col = if hovered {
            Color::new(0.25, 0.35, 0.55, 0.95)
        } else {
            Color::new(0.12, 0.16, 0.24, 0.9)
        };

        draw_rectangle(btn_x, btn_y, btn_w, btn_h, bg_col);
        draw_rectangle_lines(btn_x, btn_y, btn_w, btn_h, 1.0, Color::new(0.3, 0.4, 0.6, 1.0));
        draw_text(label, btn_x + 10.0, btn_y + 21.0, 14.0, WHITE);

        if hovered && is_mouse_button_pressed(MouseButton::Left) {
            match key {
                "reset_instant" => sim.reset_standard(),
                "reset_magnet" => sim.trigger_magnet_reset(),
                name => {
                    if let Some(sc) = scenarios::load_scenario(name) {
                        sim.pieces = sc.pieces;
                        sim.board = ChessBoard::new();
                        sim.board.sync_from_pieces(&sim.pieces);
                        sim.queue_move(sc.move_request);
                        sim.hud_message = format!("{}: {}", sc.name, sc.description);
                    }
                }
            }
        }

        btn_x += btn_w + 8.0;
    }

    // Camera Presets
    let cam_presets = [("White POV", "white"), ("Black POV", "black"), ("Top-Down", "top"), ("Under", "under"), ("Side", "side")];
    for (label, key) in cam_presets {
        let hovered = mouse_position().0 >= btn_x && mouse_position().0 <= btn_x + 95.0 && mouse_position().1 >= btn_y && mouse_position().1 <= btn_y + btn_h;
        let bg = if hovered { Color::new(0.3, 0.4, 0.45, 0.95) } else { Color::new(0.15, 0.20, 0.25, 0.9) };
        draw_rectangle(btn_x, btn_y, 95.0, btn_h, bg);
        draw_rectangle_lines(btn_x, btn_y, 95.0, btn_h, 1.0, Color::new(0.35, 0.45, 0.55, 1.0));
        draw_text(label, btn_x + 8.0, btn_y + 21.0, 13.0, Color::new(0.8, 0.9, 0.95, 1.0));
        if hovered && is_mouse_button_pressed(MouseButton::Left) {
            cam.set_preset(key);
        }
        btn_x += 102.0;
    }

    // Left HUD / Info Panel
    let hud_w = 340.0;
    let hud_h = sh - 180.0;
    let hud_x = 12.0;
    let hud_y = 115.0;

    draw_rectangle(hud_x, hud_y, hud_w, hud_h, Color::new(0.08, 0.10, 0.15, 0.92));
    draw_rectangle_lines(hud_x, hud_y, hud_w, hud_h, 1.5, Color::new(0.2, 0.25, 0.35, 1.0));

    let mut line_y = hud_y + 24.0;
    draw_text("GANTRY TELEMETRY", hud_x + 14.0, line_y, 16.0, Color::new(0.3, 0.9, 0.6, 1.0));

    line_y += 24.0;
    let mm_x = sim.gantry_pos.x * sim.planner.config().square_size_mm;
    let mm_y = sim.gantry_pos.y * sim.planner.config().square_size_mm;
    draw_text(&format!("Gantry Pos: ({:.2}, {:.2}) sq", sim.gantry_pos.x, sim.gantry_pos.y), hud_x + 14.0, line_y, 14.0, WHITE);
    line_y += 18.0;
    draw_text(&format!("Physical:   X={:.1}mm, Y={:.1}mm", mm_x, mm_y), hud_x + 14.0, line_y, 14.0, Color::new(0.8, 0.85, 0.9, 1.0));

    line_y += 24.0;
    let mag_text = if sim.magnet_on { "ELECTROMAGNET: [ENGAGED 12V]" } else { "ELECTROMAGNET: [OFF]" };
    let mag_col = if sim.magnet_on { Color::new(1.0, 0.4, 0.1, 1.0) } else { Color::new(0.4, 0.7, 0.9, 1.0) };
    draw_text(mag_text, hud_x + 14.0, line_y, 14.0, mag_col);

    // Playback Controls
    line_y += 30.0;
    draw_text("EXECUTION CONTROLS", hud_x + 14.0, line_y, 15.0, Color::new(0.3, 0.9, 0.6, 1.0));
    line_y += 14.0;

    let pause_label = if sim.paused { "Resume" } else { "Pause" };
    if draw_button(hud_x + 14.0, line_y, 85.0, 26.0, pause_label) {
        sim.paused = !sim.paused;
    }
    if draw_button(hud_x + 108.0, line_y, 85.0, 26.0, "Skip Step") {
        if sim.active_cmd_idx < sim.command_queue.len() {
            sim.active_cmd_idx += 1;
            sim.active_cmd_progress = 0.0;
        }
    }
    if draw_button(hud_x + 202.0, line_y, 110.0, 26.0, "Instant Finish") {
        sim.active_cmd_idx = sim.command_queue.len();
        sim.active_cmd_progress = 0.0;
    }

    line_y += 38.0;
    draw_text(&format!("Speed: {:.1}x", sim.speed_mult), hud_x + 14.0, line_y, 14.0, WHITE);
    if draw_button(hud_x + 120.0, line_y - 14.0, 32.0, 20.0, "-") {
        sim.speed_mult = (sim.speed_mult - 0.25).max(0.25);
    }
    if draw_button(hud_x + 160.0, line_y - 14.0, 32.0, 20.0, "+") {
        sim.speed_mult = (sim.speed_mult + 0.25).min(5.0);
    }

    // Algebraic Move Input Box
    line_y += 36.0;
    draw_text("ALGEBRAIC MOVE INPUT", hud_x + 14.0, line_y, 15.0, Color::new(0.3, 0.9, 0.6, 1.0));
    line_y += 12.0;

    draw_rectangle(hud_x + 14.0, line_y, 180.0, 28.0, Color::new(0.05, 0.07, 0.10, 1.0));
    draw_rectangle_lines(hud_x + 14.0, line_y, 180.0, 28.0, 1.0, Color::new(0.4, 0.5, 0.7, 1.0));

    let display_input = if sim.algebraic_input.is_empty() { "e.g. e2e4..." } else { &sim.algebraic_input };
    let input_col = if sim.algebraic_input.is_empty() { Color::new(0.5, 0.5, 0.5, 1.0) } else { WHITE };
    draw_text(display_input, hud_x + 22.0, line_y + 19.0, 15.0, input_col);

    if draw_button(hud_x + 204.0, line_y, 75.0, 28.0, "Play") {
        sim.execute_algebraic();
    }

    // Command Queue Log View
    line_y += 42.0;
    draw_text(&format!("COMMAND QUEUE ({}/{})", sim.active_cmd_idx, sim.command_queue.len()), hud_x + 14.0, line_y, 14.0, Color::new(0.3, 0.9, 0.6, 1.0));
    line_y += 10.0;

    let log_box_y = line_y;
    let log_box_h = hud_y + hud_h - log_box_y - 14.0;
    draw_rectangle(hud_x + 14.0, log_box_y, hud_w - 28.0, log_box_h, Color::new(0.04, 0.06, 0.09, 0.9));
    draw_rectangle_lines(hud_x + 14.0, log_box_y, hud_w - 28.0, log_box_h, 1.0, Color::new(0.2, 0.25, 0.35, 1.0));

    let max_lines = (log_box_h / 20.0).floor() as usize;
    let start_idx = sim.active_cmd_idx.saturating_sub(2);

    for (line_offset, cmd_i) in (start_idx..sim.command_queue.len()).take(max_lines).enumerate() {
        let c = &sim.command_queue[cmd_i];
        let is_current = cmd_i == sim.active_cmd_idx;
        let action_str = match c.action {
            CommandAction::TravelMagnetOff => "TRAVEL",
            CommandAction::MagnetOn => "MAG_ON",
            CommandAction::MovePieceStraight => "MOVE",
            CommandAction::MagnetOff => "MAG_OFF",
        };

        let row_y = log_box_y + 18.0 + (line_offset as f32 * 20.0);
        let col = if is_current { Color::new(0.2, 0.95, 0.5, 1.0) } else { Color::new(0.65, 0.70, 0.80, 1.0) };
        let prefix = if is_current { ">" } else { " " };

        draw_text(
            &format!("{}{:2}. {:<7} ({:.1},{:.1})->({:.1},{:.1})", prefix, cmd_i + 1, action_str, c.from.x, c.from.y, c.to.x, c.to.y),
            hud_x + 20.0,
            row_y,
            12.0,
            col,
        );
    }

    // Bottom Status HUD Banner
    draw_rectangle(12.0, sh - 48.0, sw - 24.0, 36.0, Color::new(0.08, 0.11, 0.16, 0.95));
    draw_rectangle_lines(12.0, sh - 48.0, sw - 24.0, 36.0, 1.0, Color::new(0.2, 0.25, 0.35, 1.0));
    draw_text(&format!("HUD: {}", sim.hud_message), 24.0, sh - 25.0, 15.0, Color::new(0.9, 0.92, 0.96, 1.0));
}

fn draw_button(x: f32, y: f32, w: f32, h: f32, text: &str) -> bool {
    let mouse = mouse_position();
    let hovered = mouse.0 >= x && mouse.0 <= x + w && mouse.1 >= y && mouse.1 <= y + h;
    let bg = if hovered { Color::new(0.3, 0.45, 0.7, 0.95) } else { Color::new(0.18, 0.24, 0.34, 0.9) };
    draw_rectangle(x, y, w, h, bg);
    draw_rectangle_lines(x, y, w, h, 1.0, Color::new(0.4, 0.5, 0.7, 1.0));
    draw_text(text, x + 8.0, y + h * 0.65, 13.0, WHITE);
    hovered && is_mouse_button_pressed(MouseButton::Left)
}

// ----------------------------------------------------------------------------
// Main Application Loop
// ----------------------------------------------------------------------------

fn window_conf() -> Conf {
    Conf {
        window_title: "Project 64 — Autonomous Chessboard 3D Simulator & Movement Planner".to_string(),
        window_width: 1540,
        window_height: 920,
        high_dpi: true,
        sample_count: 4,
        ..Default::default()
    }
}

#[macroquad::main(window_conf)]
async fn main() {
    let mut cam = OrbitCamera::new();
    let mut sim = SimState::new();

    loop {
        let dt = get_frame_time();
        let mouse_pos = vec2(mouse_position().0, mouse_position().1);

        // Keyboard text input for algebraic move notation
        while let Some(c) = get_char_pressed() {
            if c.is_alphanumeric() && sim.algebraic_input.len() < 5 {
                sim.algebraic_input.push(c);
            }
        }
        if is_key_pressed(KeyCode::Backspace) {
            sim.algebraic_input.pop();
        }
        if is_key_pressed(KeyCode::Enter) {
            sim.execute_algebraic();
        }

        // Mouse Camera Orbit & Zoom
        let over_hud = mouse_pos.x < 370.0 && mouse_pos.y > 60.0;
        let over_header = mouse_pos.y < 110.0;
        let over_footer = mouse_pos.y > screen_height() - 55.0;
        let in_viewport = !over_hud && !over_header && !over_footer;

        if in_viewport {
            if is_mouse_button_down(MouseButton::Left) || is_mouse_button_down(MouseButton::Right) {
                let dx = mouse_pos.x - sim.last_mouse_pos.x;
                let dy = mouse_pos.y - sim.last_mouse_pos.y;
                cam.yaw += dx * 0.008;
                cam.pitch = (cam.pitch + dy * 0.008).clamp(-85.0f32.to_radians(), 89.5f32.to_radians());
            }

            // Scroll zoom
            let wheel = mouse_wheel().1;
            if wheel.abs() > 0.01 {
                cam.distance = (cam.distance - wheel * 0.8).clamp(5.0, 30.0);
            }

            // Click-to-move piece/square selection
            if is_mouse_button_pressed(MouseButton::Left) {
                if let Some(sq) = pick_square_at_screen(mouse_pos, &cam, screen_width(), screen_height()) {
                    sim.select_square(sq);
                }
            }
        }
        sim.last_mouse_pos = mouse_pos;

        // Update Gantry Kinematics and Movement Interpolation
        sim.update_animation(dt);

        // Render 3D Scene
        clear_background(Color::new(0.05, 0.07, 0.10, 1.0));

        set_camera(&Camera3D {
            position: cam.position(),
            target: cam.target,
            up: vec3(0.0, 1.0, 0.0),
            fovy: 45.0,
            aspect: Some(screen_width() / screen_height()),
            projection: Projection::Perspective,
            ..Default::default()
        });

        // 3D Scene Objects
        draw_board_3d(&sim);
        draw_under_board_gantry_3d(&sim);

        let sq_mm = sim.planner.config().square_size_mm;
        for p in &sim.pieces {
            draw_chess_piece_3d(p, sq_mm, sim.show_radii);
        }

        // Switch back to 2D screen space for HUD & UI
        set_default_camera();
        draw_gui(&mut sim, &mut cam);

        next_frame().await;
    }
}
