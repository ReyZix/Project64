use std::f32::consts::TAU;

use macroquad::{camera::Camera, prelude::*, window::screen_dpi_scale};
use movement_planner::{
    piece_diameter_mm, CommandAction, GridCoord, PieceColor, PieceState, PieceType, Point2D,
};

use crate::simulation::Simulation;
use crate::ui::{color, AMBER, MINT};

const FOV: f32 = std::f32::consts::FRAC_PI_4;
const BOARD_CENTER: Vec3 = Vec3::new(4.0, -0.15, 4.0);
pub const VIEWS: [&str; 5] = ["White", "Black", "Top", "Gantry", "Side"];

pub struct OrbitCamera {
    yaw: f32,
    pitch: f32,
    zoom: f32,
    pub preset: Option<usize>,
    press_origin: Option<Vec2>,
    last_mouse: Vec2,
    dragging: bool,
}

impl OrbitCamera {
    pub fn new() -> Self {
        let mut camera = Self {
            yaw: 0.0,
            pitch: 0.0,
            zoom: 1.0,
            preset: None,
            press_origin: None,
            last_mouse: Vec2::ZERO,
            dragging: false,
        };
        camera.set_preset(0);
        camera
    }

    pub fn set_preset(&mut self, index: usize) {
        let (yaw, pitch) = [
            (0.0f32, 50.0f32),
            (180.0, 50.0),
            (0.0, 89.5),
            (0.0, -28.0),
            (90.0, 17.0),
        ][index];
        self.yaw = yaw.to_radians();
        self.pitch = pitch.to_radians();
        self.zoom = 1.0;
        self.preset = Some(index);
    }

    pub fn view(&self, area: Rect) -> Camera3D {
        let dpi = screen_dpi_scale();
        let aspect = area.w / area.h;
        let distance = (15.0f32).max(16.8 / aspect) * self.zoom;
        let position = BOARD_CENTER
            + distance
                * vec3(
                    self.pitch.cos() * self.yaw.sin(),
                    self.pitch.sin(),
                    self.pitch.cos() * self.yaw.cos(),
                );
        Camera3D {
            position,
            target: BOARD_CENTER,
            up: Vec3::Y,
            fovy: FOV,
            aspect: Some(aspect),
            viewport: Some((
                (area.x * dpi).round() as i32,
                ((screen_height() - area.bottom()) * dpi).round() as i32,
                (area.w * dpi).round() as i32,
                (area.h * dpi).round() as i32,
            )),
            z_near: 0.08,
            z_far: 80.0,
            ..Default::default()
        }
    }

    pub fn handle_mouse(&mut self, area: Rect, sim: &mut Simulation) {
        let mouse = Vec2::from(mouse_position());
        let inside = area.contains(mouse);
        if inside && is_mouse_button_pressed(MouseButton::Left) {
            self.press_origin = Some(mouse);
            self.dragging = false;
        }
        if inside && is_mouse_button_pressed(MouseButton::Right) {
            self.press_origin = Some(mouse);
            self.dragging = true;
        }
        if let Some(origin) = self.press_origin {
            if is_mouse_button_down(MouseButton::Left) || is_mouse_button_down(MouseButton::Right) {
                self.dragging |= mouse.distance_squared(origin) > 25.0;
                if self.dragging && mouse != self.last_mouse {
                    let delta = mouse - self.last_mouse;
                    self.yaw -= delta.x * 0.007;
                    self.pitch = (self.pitch + delta.y * 0.007).clamp(-1.45, 89.5f32.to_radians());
                    self.preset = None;
                }
            } else {
                if inside && !self.dragging && is_mouse_button_released(MouseButton::Left) {
                    if let Some(square) = pick_square(mouse, area, &self.view(area)) {
                        sim.select(square);
                    }
                }
                self.press_origin = None;
            }
        }
        if inside {
            let scroll = mouse_wheel().1;
            self.zoom = (self.zoom * (-scroll.clamp(-4.0, 4.0) * 0.09).exp()).clamp(0.45, 1.9);
        }
        self.last_mouse = mouse;
    }
}

fn pick_square(mouse: Vec2, area: Rect, view: &Camera3D) -> Option<GridCoord> {
    if view.position.y <= 0.0 {
        return None;
    }
    let ndc = vec2(
        2.0 * (mouse.x - area.x) / area.w - 1.0,
        1.0 - 2.0 * (mouse.y - area.y) / area.h,
    );
    let far = view.matrix().inverse().project_point3(ndc.extend(1.0));
    let direction = far - view.position;
    if direction.y >= -0.001 {
        return None;
    }
    let hit = view.position - direction * (view.position.y / direction.y);
    Some(GridCoord::new(
        hit.x.floor() as i32,
        (8.0 - hit.z).floor() as i32,
    ))
}

pub fn project(position: Vec3, area: Rect, matrix: Mat4) -> Option<Vec2> {
    let clip = matrix * position.extend(1.0);
    if clip.w <= 0.0 {
        return None;
    }
    let point = vec2(
        area.x + (clip.x / clip.w + 1.0) * 0.5 * area.w,
        area.y + (1.0 - clip.y / clip.w) * 0.5 * area.h,
    );
    area.contains(point).then_some(point)
}

pub fn world(x: f32, y: f32, height: f32) -> Vec3 {
    vec3(x, height, 8.0 - y)
}

fn on_board(p: Point2D, height: f32) -> Vec3 {
    world(p.x, p.y, height)
}

fn tint(c: Color, light: f32) -> Color {
    Color::new(c.r * light, c.g * light, c.b * light, c.a)
}

fn slab(center: Vec3, size: Vec3, c: Color) {
    let a = center - size * 0.5;
    let b = center + size * 0.5;
    let corners = [
        a,
        vec3(b.x, a.y, a.z),
        vec3(b.x, a.y, b.z),
        vec3(a.x, a.y, b.z),
        vec3(a.x, b.y, a.z),
        vec3(b.x, b.y, a.z),
        b,
        vec3(a.x, b.y, b.z),
    ];
    let mut vertices = Vec::with_capacity(24);
    let mut indices = Vec::with_capacity(36);
    for (face, light) in [
        ([4, 5, 6, 7], 1.0),
        ([0, 1, 2, 3], 0.5),
        ([0, 1, 5, 4], 0.68),
        ([2, 3, 7, 6], 0.85),
        ([1, 2, 6, 5], 0.64),
        ([0, 3, 7, 4], 0.92),
    ] {
        let i = vertices.len() as u16;
        vertices
            .extend(face.map(|corner| Vertex::new2(corners[corner], Vec2::ZERO, tint(c, light))));
        indices.extend([i, i + 1, i + 2, i, i + 2, i + 3]);
    }
    draw_mesh(&Mesh {
        vertices,
        indices,
        texture: None,
    });
}

// A lathed profile keeps the silhouettes distinct and gives the pieces a soft directional highlight.
fn lathe(center: Vec3, radius: f32, profile: &[(f32, f32)], c: Color) {
    const SIDES: usize = 24;
    let mut vertices = Vec::with_capacity(profile.len() * SIDES);
    let mut indices = Vec::with_capacity((profile.len() - 1) * SIDES * 6);
    for (row, &(height, scale)) in profile.iter().enumerate() {
        let before = profile[row.saturating_sub(1)];
        let after = profile[(row + 1).min(profile.len() - 1)];
        let slope = vec2(after.0 - before.0, (before.1 - after.1) * radius).normalize_or_zero();
        for i in 0..SIDES {
            let (sin, cos) = (i as f32 * TAU / SIDES as f32).sin_cos();
            let normal = vec3(cos * slope.x, slope.y, sin * slope.x);
            let light = 0.65 + normal.dot(vec3(-0.53, 0.73, 0.43)).max(0.0) * 0.42;
            let position = center + vec3(cos * radius * scale, height, sin * radius * scale);
            vertices.push(Vertex::new2(position, Vec2::ZERO, tint(c, light)));
            if row + 1 < profile.len() {
                let a = (row * SIDES + i) as u16;
                let b = (row * SIDES + (i + 1) % SIDES) as u16;
                let s = SIDES as u16;
                indices.extend([a, b, b + s, a, b + s, a + s]);
            }
        }
    }
    draw_mesh(&Mesh {
        vertices,
        indices,
        texture: None,
    });
}

fn ring(center: Vec3, radius: f32, c: Color) {
    for i in 0..32 {
        let point = |i| {
            let a = i as f32 * TAU / 32.0;
            center + vec3(a.cos() * radius, 0.0, a.sin() * radius)
        };
        draw_line_3d(point(i), point(i + 1), c);
    }
}

fn draw_piece(p: &PieceState, square_mm: f32, margin_mm: f32, clearance: bool) {
    let center = on_board(p.current_pos, 0.024);
    let radius = piece_diameter_mm(p.piece_type) * 0.5 / square_mm;
    let (body, trim) = match p.color {
        PieceColor::White => (color(0xf3e6ca), color(0xa98f65)),
        PieceColor::Black => (color(0x273d4a), color(0x5a7583)),
    };
    lathe(
        center,
        radius,
        &[(0.0, 0.87), (0.025, 1.0), (0.073, 1.0), (0.125, 0.83)],
        body,
    );
    lathe(
        center,
        radius,
        &[(0.121, 0.84), (0.145, 0.82), (0.16, 0.67)],
        trim,
    );
    let profile: &[(f32, f32)] = match p.piece_type {
        PieceType::Pawn => &[
            (0.157, 0.68),
            (0.22, 0.50),
            (0.37, 0.32),
            (0.43, 0.54),
            (0.46, 0.52),
            (0.48, 0.33),
            (0.53, 0.54),
            (0.60, 0.48),
            (0.65, 0.25),
            (0.67, 0.0),
        ],
        PieceType::Knight => &[
            (0.157, 0.68),
            (0.25, 0.50),
            (0.35, 0.52),
            (0.39, 0.65),
            (0.42, 0.0),
        ],
        PieceType::Bishop => &[
            (0.157, 0.68),
            (0.26, 0.48),
            (0.51, 0.35),
            (0.55, 0.58),
            (0.59, 0.54),
            (0.62, 0.32),
            (0.70, 0.52),
            (0.78, 0.44),
            (0.88, 0.18),
            (0.91, 0.0),
        ],
        PieceType::Rook => &[
            (0.157, 0.68),
            (0.24, 0.58),
            (0.54, 0.55),
            (0.59, 0.76),
            (0.69, 0.76),
            (0.69, 0.0),
        ],
        PieceType::Queen => &[
            (0.157, 0.68),
            (0.25, 0.49),
            (0.57, 0.33),
            (0.63, 0.58),
            (0.68, 0.56),
            (0.70, 0.39),
            (0.89, 0.71),
            (0.91, 0.59),
            (0.91, 0.0),
        ],
        PieceType::King => &[
            (0.157, 0.68),
            (0.27, 0.49),
            (0.62, 0.35),
            (0.67, 0.61),
            (0.72, 0.59),
            (0.76, 0.39),
            (0.86, 0.57),
            (0.93, 0.45),
            (0.96, 0.0),
        ],
        PieceType::None => return,
    };
    lathe(center, radius, profile, body);
    match p.piece_type {
        PieceType::Knight => {
            let direction = if p.color == PieceColor::White {
                -1.0
            } else {
                1.0
            };
            let outline = [
                (-0.13, 0.38),
                (-0.13, 0.66),
                (-0.055, 0.80),
                (-0.035, 0.88),
                (0.035, 0.79),
                (0.19, 0.66),
                (0.17, 0.55),
                (0.005, 0.57),
                (0.09, 0.38),
            ];
            let mut vertices = Vec::with_capacity(outline.len() * 2);
            for (side, light) in [(-0.083, 0.98), (0.083, 0.78)] {
                vertices.extend(outline.map(|(z, y)| {
                    Vertex::new2(
                        center + vec3(side, y, z * direction),
                        Vec2::ZERO,
                        tint(body, light),
                    )
                }));
            }
            let mut indices = vec![
                0, 1, 7, 0, 7, 8, 1, 2, 7, 2, 3, 4, 2, 4, 7, 4, 5, 7, 5, 6, 7,
            ];
            let count = outline.len() as u16;
            indices.extend_from_within(..);
            let half = indices.len() / 2;
            for i in &mut indices[half..] {
                *i += count;
            }
            for i in 0..count {
                let j = (i + 1) % count;
                indices.extend([i, j, j + count, i, j + count, i + count]);
            }
            draw_mesh(&Mesh {
                vertices,
                indices,
                texture: None,
            });
        }
        PieceType::Rook => {
            for x in [-0.095, 0.095] {
                for z in [-0.095, 0.095] {
                    slab(center + vec3(x, 0.73, z), vec3(0.115, 0.10, 0.115), body);
                }
            }
        }
        PieceType::Queen | PieceType::Bishop => {
            let y = if p.piece_type == PieceType::Queen {
                0.91
            } else {
                0.88
            };
            lathe(
                center + vec3(0.0, y, 0.0),
                radius,
                &[(0.0, 0.15), (0.035, 0.25), (0.065, 0.16), (0.08, 0.0)],
                trim,
            );
        }
        PieceType::King => {
            slab(
                center + vec3(0.0, 1.035, 0.0),
                vec3(0.055, 0.18, 0.055),
                body,
            );
            slab(
                center + vec3(0.0, 1.06, 0.0),
                vec3(0.17, 0.048, 0.054),
                body,
            );
        }
        _ => {}
    }
    if clearance {
        ring(
            center + Vec3::Y * 0.01,
            radius + margin_mm / square_mm,
            AMBER,
        );
    }
}

pub fn draw(sim: &Simulation, clearance: bool, show_route: bool, under_board: bool) {
    if !under_board {
        for i in -17..=25 {
            let i = i as f32;
            let ink = color(0x1c2b34);
            draw_line_3d(vec3(i, -1.12, -17.0), vec3(i, -1.12, 25.0), ink);
            draw_line_3d(vec3(-17.0, -1.12, i), vec3(25.0, -1.12, i), ink);
        }
    }
    slab(
        world(4.0, 4.0, -0.24),
        vec3(12.55, 0.34, 10.60),
        color(0x3f5460),
    );
    slab(
        world(4.0, 4.0, -0.066),
        vec3(12.44, 0.14, 10.46),
        color(0x263a44),
    );
    for row in -1..=8 {
        for col in -2..=9 {
            let sq = GridCoord::new(col, row);
            let center = world(col as f32 + 0.5, row as f32 + 0.5, 0.012);
            let base = if sq.is_on_board() {
                if (col + row) % 2 == 0 {
                    color(0x597d7b)
                } else {
                    color(0xc4cabe)
                }
            } else if (0..8).contains(&row) {
                color(0x314852)
            } else {
                color(0x20343e)
            };
            let selected = sim.selected == Some(sq);
            draw_plane(
                center,
                vec2(0.495, 0.495),
                None,
                if selected { color(0x4abea5) } else { base },
            );
            if sim.legal_moves.iter().any(|m| m.to == sq) {
                let capture = sim.pieces.iter().any(|p| p.square == sq);
                if capture {
                    ring(center + Vec3::Y * 0.01, 0.39, MINT);
                } else {
                    lathe(
                        center,
                        0.14,
                        &[(0.003, 1.0), (0.014, 0.94), (0.014, 0.0)],
                        MINT,
                    );
                }
            } else if !sq.is_on_board() && (0..8).contains(&row) {
                ring(center + Vec3::Y * 0.003, 0.30, color(0x4f6570));
            }
        }
    }

    let config = sim.planner.config();
    for p in &sim.pieces {
        draw_piece(p, config.square_size_mm, config.safety_margin_mm, clearance);
    }
    if show_route && sim.busy() {
        for (offset, cmd) in sim.queue[sim.step..].iter().enumerate() {
            let ink = match cmd.action {
                CommandAction::TravelMagnetOff => AMBER,
                CommandAction::MovePieceStraight => MINT,
                _ => continue,
            };
            let from = if offset == 0 { sim.gantry } else { cmd.from };
            let a = on_board(from, 0.038);
            let b = on_board(cmd.to, 0.038);
            let normal = (b - a).normalize_or_zero().cross(Vec3::Y)
                * if offset == 0 { 0.032 } else { 0.017 };
            let ink = Color::new(ink.r, ink.g, ink.b, if offset == 0 { 1.0 } else { 0.52 });
            draw_mesh(&Mesh {
                vertices: [a - normal, a + normal, b + normal, b - normal]
                    .map(|p| Vertex::new2(p, Vec2::ZERO, ink))
                    .to_vec(),
                indices: vec![0, 1, 2, 0, 2, 3],
                texture: None,
            });
        }
        ring(
            on_board(sim.gantry, 0.041),
            0.32,
            if sim.magnet_on { MINT } else { AMBER },
        );
    }
    draw_gantry(sim);
}

fn draw_gantry(sim: &Simulation) {
    let metal = color(0x718e9e);
    let casing = color(0x395765);
    for y in [-0.74, 8.74] {
        slab(world(4.0, y, -0.74), vec3(12.2, 0.18, 0.30), casing);
        slab(world(4.0, y, -0.85), vec3(12.2, 0.046, 0.13), metal);
    }
    for x in [-1.88, 9.88] {
        for y in [-0.84, 8.84] {
            slab(world(x, y, -0.75), vec3(0.47, 0.72, 0.52), casing);
        }
    }
    let g = sim.gantry;
    slab(world(g.x, 4.0, -0.59), vec3(0.27, 0.18, 9.75), metal);
    slab(
        world(g.x, 4.0, -0.69),
        vec3(0.085, 0.045, 9.75),
        color(0xadc3c9),
    );
    slab(on_board(g, -0.53), vec3(0.77, 0.25, 0.70), casing);
    let coil = if sim.magnet_on {
        AMBER
    } else {
        color(0x7bc0c7)
    };
    lathe(
        on_board(g, -0.78),
        0.26,
        &[(0.0, 0.0), (0.0, 0.80), (0.04, 1.0), (0.15, 1.0)],
        coil,
    );
    ring(on_board(g, -0.79), 0.35, coil);
}
