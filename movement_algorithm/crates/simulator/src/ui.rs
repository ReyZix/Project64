use macroquad::{camera::Camera, prelude::*};
use movement_planner::{CommandAction, GameState, PieceColor};

use crate::{
    scene::{project, world, OrbitCamera, VIEWS},
    simulation::{Simulation, DEMOS},
};

pub const fn color(hex: u32) -> Color {
    Color::new(
        ((hex >> 16) & 255) as f32 / 255.0,
        ((hex >> 8) & 255) as f32 / 255.0,
        (hex & 255) as f32 / 255.0,
        1.0,
    )
}

pub const BACKGROUND: Color = color(0x0c131a);
pub const MINT: Color = color(0x79e3c5);
pub const AMBER: Color = color(0xf2bf78);
const PANEL: Color = color(0x141f28);
const VIEWPORT: Color = color(0x101b23);
const WELL: Color = color(0x101920);
const BORDER: Color = color(0x2a3a46);
const SURFACE: Color = color(0x202f3a);
const HOVER: Color = color(0x2c414d);
const TEXT: Color = color(0xe9eff0);
const MUTED: Color = color(0xa1b2bc);
const QUIET: Color = color(0x778d9b);
const ACTIVE: Color = color(0x1d3e3b);
const ERROR: Color = color(0xffa5a1);

pub struct Layout {
    width: f32,
    height: f32,
    scale: f32,
    stage: Rect,
    viewport: Rect,
    rail: Rect,
}

impl Layout {
    pub fn new(width: f32, height: f32) -> Self {
        let scale = (width / 975.0).min(height / 700.0).clamp(0.01, 1.0);
        let width = width / scale;
        let height = height / scale;
        let rail = Rect::new(width - 368.0, 92.0, 344.0, height - 154.0);
        let stage = Rect::new(24.0, rail.y, rail.x - 40.0, rail.h);
        let viewport = Rect::new(
            stage.x + 1.0,
            stage.y + 55.0,
            stage.w - 2.0,
            stage.h - 135.0,
        );
        Self {
            width,
            height,
            scale,
            stage,
            viewport,
            rail,
        }
    }

    pub fn viewport(&self) -> Rect {
        let r = self.viewport;
        Rect::new(
            r.x * self.scale,
            r.y * self.scale,
            r.w * self.scale,
            r.h * self.scale,
        )
    }

    pub fn camera(&self) -> Camera2D {
        Camera2D {
            target: vec2(self.width * 0.5, self.height * 0.5),
            zoom: vec2(2.0 / self.width, 2.0 / self.height),
            ..Default::default()
        }
    }
}

pub struct Interface {
    font: Font,
    strong: Font,
    input: String,
    focused: bool,
    mouse: Vec2,
    clicked: bool,
    slider_drag: bool,
    queue_top: Option<usize>,
    previous_step: usize,
    pub show_clearance: bool,
    pub show_route: bool,
}

#[derive(Clone, Copy)]
enum Button {
    Normal,
    Primary,
    Selected,
    Disabled,
}

impl Interface {
    pub fn new() -> Self {
        Self {
            font: load_ttf_font_from_bytes(include_bytes!("../assets/Inter-Regular.ttf")).unwrap(),
            strong: load_ttf_font_from_bytes(include_bytes!("../assets/Inter-Semibold.ttf"))
                .unwrap(),
            input: String::new(),
            focused: false,
            mouse: Vec2::ZERO,
            clicked: false,
            slider_drag: false,
            queue_top: None,
            previous_step: 0,
            show_clearance: false,
            show_route: true,
        }
    }

    pub fn handle_keyboard(&mut self, sim: &mut Simulation) {
        if is_key_pressed(KeyCode::Escape) {
            if self.focused {
                self.focused = false;
            } else if sim.selected.is_some() {
                sim.clear_selection();
            }
        }
        if sim.busy() {
            self.focused = false;
        } else if is_key_pressed(KeyCode::Tab) {
            self.focused = !self.focused;
        }
        while let Some(c) = get_char_pressed() {
            if !sim.busy() && c.is_ascii_alphanumeric() && self.input.len() < 5 {
                self.input.push(c.to_ascii_lowercase());
                self.focused = true;
            }
        }
        if self.focused {
            if is_key_pressed(KeyCode::Backspace) {
                self.input.pop();
            }
            if is_key_pressed(KeyCode::Enter) {
                self.submit(sim);
            }
        } else if sim.busy() && is_key_pressed(KeyCode::Space) {
            sim.paused = !sim.paused;
        }
    }

    fn submit(&mut self, sim: &mut Simulation) {
        if sim.play_notation(&self.input) {
            self.input.clear();
            self.focused = false;
            self.queue_top = None;
        } else {
            self.focused = true;
        }
    }

    pub fn draw_workspace(&self, layout: &Layout) {
        card(layout.stage);
        fill(layout.viewport, VIEWPORT);
    }

    pub fn draw(
        &mut self,
        layout: &Layout,
        view: &Camera3D,
        camera: &mut OrbitCamera,
        sim: &mut Simulation,
    ) {
        self.mouse = Vec2::from(mouse_position()) / layout.scale;
        self.clicked = is_mouse_button_pressed(MouseButton::Left);

        self.header(layout, sim);
        self.board_controls(layout, view, camera, sim);
        let rail = layout.rail;
        self.move_entry(Rect::new(rail.x, rail.y, rail.w, 101.0), sim);
        self.demos(Rect::new(rail.x, rail.y + 111.0, rail.w, 174.0), sim);
        self.playback(Rect::new(rail.x, rail.y + 295.0, rail.w, 130.0), sim);
        let command_hint = self.commands(
            Rect::new(rail.x, rail.y + 435.0, rail.w, rail.h - 435.0),
            sim,
        );

        let footer = Rect::new(24.0, layout.height - 48.0, layout.width - 48.0, 34.0);
        rounded(footer, 7.0, PANEL);
        let ink = if sim.error { ERROR } else { MUTED };
        draw_circle(
            footer.x + 14.0,
            footer.y + 17.0,
            3.0,
            if sim.error { ERROR } else { MINT },
        );
        self.fit_text(
            command_hint.unwrap_or(&sim.message),
            footer.x + 26.0,
            footer.y + 22.0,
            footer.w - 38.0,
            13,
            ink,
        );
    }

    fn header(&self, layout: &Layout, sim: &Simulation) {
        let logo = Rect::new(24.0, 25.0, 42.0, 42.0);
        rounded(logo, 9.0, ACTIVE);
        for row in 0..2 {
            for col in 0..2 {
                let ink = if (row + col) % 2 == 0 {
                    MINT
                } else {
                    color(0x4c9c8c)
                };
                rounded(
                    Rect::new(35.0 + col as f32 * 11.0, 36.0 + row as f32 * 11.0, 9.0, 9.0),
                    1.5,
                    ink,
                );
            }
        }
        self.text("PROJECT 64", 80.0, 32.0, 11, MINT, true);
        self.text("Movement Simulator", 79.0, 62.0, 29, TEXT, true);

        let (state, ink) = if sim.busy() {
            if sim.paused {
                ("Paused", AMBER)
            } else {
                ("Running", MINT)
            }
        } else if sim.queue.is_empty() {
            ("Ready", MINT)
        } else {
            ("Complete", MINT)
        };
        let status = Rect::new(layout.width - 130.0, 32.0, 106.0, 29.0);
        rounded(
            status,
            7.0,
            if sim.paused { color(0x352d23) } else { ACTIVE },
        );
        draw_circle(status.x + 15.0, status.y + 14.5, 3.0, ink);
        self.text(state, status.x + 26.0, status.y + 19.0, 13, ink, true);
    }

    fn board_controls(
        &mut self,
        layout: &Layout,
        view: &Camera3D,
        camera: &mut OrbitCamera,
        sim: &Simulation,
    ) {
        let r = layout.stage;
        self.text("Board view", r.x + 16.0, r.y + 33.0, 16, TEXT, true);
        for (i, label) in VIEWS.iter().enumerate() {
            let rect = Rect::new(r.x + 110.0 + i as f32 * 50.0, r.y + 12.0, 46.0, 30.0);
            if self.button(rect, label, toggle(camera.preset == Some(i))) {
                camera.set_preset(i);
            }
        }
        let clearance = Rect::new(r.right() - 100.0, r.y + 12.0, 86.0, 30.0);
        let route = Rect::new(clearance.x - 62.0, clearance.y, 56.0, clearance.h);
        if self.button(route, "Route", toggle(self.show_route)) {
            self.show_route = !self.show_route;
        }
        if self.button(clearance, "Clearance", toggle(self.show_clearance)) {
            self.show_clearance = !self.show_clearance;
        }

        let area = layout.viewport;
        let matrix = view.matrix();
        if view.position.y > 1.0 {
            let front = view.position.z >= view.target.z;
            let right = view.position.x > view.target.x + 1.0;
            let rank_axis = if right { 8.20 } else { -0.20 };
            let file_axis = if front { -0.36 } else { 8.36 };
            for i in 0..8 {
                let file = ((b'a' + i) as char).to_string();
                let rank = (i + 1).to_string();
                for (label, point) in [
                    (file, world(i as f32 + 0.5, file_axis, 0.024)),
                    (rank, world(rank_axis, i as f32 + 0.5, 0.024)),
                ] {
                    if let Some(p) = project(point, area, matrix) {
                        if area.contains(p - vec2(8.0, 8.0)) && area.contains(p + vec2(8.0, 8.0)) {
                            self.centered(&label, p, 11, TEXT, true);
                        }
                    }
                }
            }
        }

        let help = Rect::new(area.x + 12.0, area.bottom() - 36.0, 224.0, 26.0);
        rounded(help, 6.0, WELL);
        self.text(
            "Drag to orbit  ·  Scroll to zoom",
            help.x + 10.0,
            help.y + 17.0,
            12,
            MUTED,
            false,
        );
        if self.show_route && sim.busy() && view.position.y > 0.0 {
            let legend = Rect::new(area.right() - 156.0, area.y + 12.0, 144.0, 26.0);
            rounded(legend, 6.0, WELL);
            draw_circle(legend.x + 12.0, legend.y + 13.0, 3.0, AMBER);
            self.text("Travel", legend.x + 21.0, legend.y + 17.0, 12, MUTED, false);
            draw_circle(legend.x + 79.0, legend.y + 13.0, 3.0, MINT);
            self.text("Carry", legend.x + 88.0, legend.y + 17.0, 12, MUTED, false);
        }
        self.telemetry(r, sim);
    }

    fn telemetry(&self, r: Rect, sim: &Simulation) {
        let x = r.x + 14.0;
        let y = r.bottom() - 66.0;
        let width = (r.w - 44.0) / 3.0;
        let units = sim.planner.config().square_size_mm;
        for (i, (label, value, ink)) in [
            (
                "X position",
                format!("{:.1} mm", sim.gantry.x * units),
                TEXT,
            ),
            (
                "Y position",
                format!("{:.1} mm", sim.gantry.y * units),
                TEXT,
            ),
            (
                "Electromagnet",
                if sim.magnet_on {
                    "Engaged · 12 V"
                } else {
                    "Off"
                }
                .to_owned(),
                if sim.magnet_on { AMBER } else { TEXT },
            ),
        ]
        .iter()
        .enumerate()
        {
            let cell = Rect::new(x + i as f32 * (width + 8.0), y, width, 52.0);
            rounded(cell, 7.0, WELL);
            self.text(label, cell.x + 12.0, y + 18.0, 11, MUTED, false);
            self.text(value, cell.x + 12.0, y + 38.0, 16, *ink, true);
        }
    }

    fn move_entry(&mut self, r: Rect, sim: &mut Simulation) {
        card(r);
        self.text("Make a move", r.x + 14.0, r.y + 27.0, 16, TEXT, true);
        let side = if sim.board.turn == PieceColor::White {
            "White"
        } else {
            "Black"
        };
        let (turn, ink) = match sim.board.state {
            GameState::Active => (format!("{side} to move"), MINT),
            GameState::Check => (format!("{side} in check"), AMBER),
            GameState::Checkmate => ("Checkmate".to_owned(), AMBER),
            GameState::Stalemate => ("Stalemate".to_owned(), MUTED),
        };
        self.right_text(&turn, r.right() - 14.0, r.y + 26.0, 12, ink, true);

        let field = Rect::new(r.x + 14.0, r.y + 39.0, r.w - 109.0, 34.0);
        if self.clicked {
            self.focused = field.contains(self.mouse) && !sim.busy();
        }
        let border = if self.focused {
            if sim.error {
                ERROR
            } else {
                MINT
            }
        } else {
            BORDER
        };
        rounded(field, 6.0, border);
        rounded(inset(field, 1.0), 5.0, WELL);
        if self.input.is_empty() && !self.focused {
            self.text(
                "e.g. e2e4",
                field.x + 10.0,
                field.y + 22.0,
                14,
                QUIET,
                false,
            );
        } else {
            self.text(&self.input, field.x + 10.0, field.y + 22.0, 14, TEXT, false);
        }
        if self.focused && get_time() % 1.0 < 0.65 {
            let caret = field.x + 11.0 + self.measure(&self.input, 14, false).width;
            draw_line(caret, field.y + 8.0, caret, field.bottom() - 8.0, 1.0, MINT);
        }
        if self.button(
            Rect::new(field.right() + 8.0, field.y, 73.0, field.h),
            "Play",
            enabled(!sim.busy(), Button::Primary),
        ) {
            self.submit(sim);
        }
        let hint = if sim.busy() {
            "Finish playback to make another move."
        } else {
            "Click a piece or type a move and press Enter."
        };
        self.text(hint, r.x + 14.0, r.y + 90.0, 11, MUTED, false);
    }

    fn demos(&mut self, r: Rect, sim: &mut Simulation) {
        card(r);
        self.text("Demos & reset", r.x + 14.0, r.y + 27.0, 16, TEXT, true);
        let width = (r.w - 35.0) * 0.5;
        for (i, (name, movement, _)) in DEMOS.iter().enumerate() {
            let rect = Rect::new(
                r.x + 14.0 + (i % 2) as f32 * (width + 7.0),
                r.y + 37.0 + (i / 2) as f32 * 33.0,
                width,
                29.0,
            );
            let active = sim.title == *name;
            self.button_surface(rect, toggle(active));
            self.text(
                name,
                rect.x + 8.0,
                rect.y + 19.0,
                12,
                if active { MINT } else { TEXT },
                true,
            );
            self.right_text(
                movement,
                rect.right() - 7.0,
                rect.y + 19.0,
                10,
                MUTED,
                false,
            );
            if self.clicked && rect.contains(self.mouse) {
                self.queue_top = None;
                sim.run_demo(i);
            }
        }
        let magnet = Rect::new(r.x + 21.0 + width, r.y + 103.0, width, 29.0);
        if self.button(magnet, "Magnet reset", enabled(!sim.busy(), Button::Normal)) {
            self.queue_top = None;
            sim.magnet_reset();
        }
        let reset = Rect::new(r.x + 14.0, r.y + 136.0, r.w - 28.0, 26.0);
        if self.button(reset, "Instant reset", Button::Normal) {
            self.queue_top = None;
            self.input.clear();
            sim.reset();
        }
    }

    fn playback(&mut self, r: Rect, sim: &mut Simulation) {
        card(r);
        self.text("Playback", r.x + 14.0, r.y + 26.0, 16, TEXT, true);
        if !sim.title.is_empty() {
            self.right_text(&sim.title, r.right() - 14.0, r.y + 25.0, 12, MINT, true);
        }
        let width = (r.w - 42.0) / 3.0;
        for (i, label) in [
            if sim.paused { "Resume" } else { "Pause" },
            "Skip step",
            "Finish",
        ]
        .iter()
        .enumerate()
        {
            let rect = Rect::new(
                r.x + 14.0 + i as f32 * (width + 7.0),
                r.y + 37.0,
                width,
                29.0,
            );
            if self.button(
                rect,
                label,
                enabled(
                    sim.busy(),
                    if i == 0 {
                        Button::Primary
                    } else {
                        Button::Normal
                    },
                ),
            ) {
                match i {
                    0 => sim.paused = !sim.paused,
                    1 => sim.advance(),
                    _ => sim.finish(),
                }
            }
        }
        let speed_y = r.y + 88.0;
        self.text("Speed", r.x + 14.0, speed_y + 4.0, 12, MUTED, false);
        self.right_text(
            &format!("{:.2}×", sim.speed),
            r.right() - 14.0,
            speed_y + 4.0,
            12,
            TEXT,
            true,
        );
        let track = Rect::new(r.x + 64.0, speed_y - 2.0, r.w - 128.0, 4.0);
        if self.clicked
            && Rect::new(track.x - 6.0, track.y - 8.0, track.w + 12.0, 20.0).contains(self.mouse)
        {
            self.slider_drag = true;
        }
        if self.slider_drag {
            let fraction = ((self.mouse.x - track.x) / track.w).clamp(0.0, 1.0);
            sim.speed = (1.0 + (fraction * 19.0).round()) * 0.25;
            self.slider_drag = is_mouse_button_down(MouseButton::Left);
        }
        let fraction = (sim.speed - 0.25) / 4.75;
        rounded(track, 2.0, BORDER);
        fill(
            Rect::new(track.x, track.y, track.w * fraction, track.h),
            MINT,
        );
        draw_circle(track.x + track.w * fraction, speed_y, 5.5, MINT);

        let progress = (sim.step as f32 + sim.progress) / sim.queue.len().max(1) as f32;
        let bar = Rect::new(r.x + 14.0, r.bottom() - 18.0, r.w - 28.0, 4.0);
        rounded(bar, 2.0, BORDER);
        fill(Rect::new(bar.x, bar.y, bar.w * progress, bar.h), MINT);
    }

    fn commands<'a>(&mut self, r: Rect, sim: &'a Simulation) -> Option<&'a str> {
        card(r);
        self.text("Command queue", r.x + 14.0, r.y + 26.0, 16, TEXT, true);
        self.right_text(
            &format!("{} / {}", sim.step, sim.queue.len()),
            r.right() - 14.0,
            r.y + 25.0,
            12,
            MUTED,
            true,
        );
        let list = Rect::new(r.x + 10.0, r.y + 36.0, r.w - 20.0, r.h - 46.0);
        rounded(list, 6.0, WELL);
        if sim.queue.is_empty() {
            self.centered(
                "Commands appear when a move is planned.",
                list.point() + list.size() * 0.5,
                12,
                QUIET,
                false,
            );
            self.previous_step = 0;
            return None;
        }

        let row_height = 24.0;
        let rows = ((list.h - 6.0) / row_height).floor() as usize;
        let max_top = sim.queue.len().saturating_sub(rows);
        let auto_top = sim.step.saturating_sub(1).min(max_top);
        if sim.step < self.previous_step {
            self.queue_top = None;
        }
        self.previous_step = sim.step;
        if list.contains(self.mouse) {
            let scroll = mouse_wheel().1;
            if scroll.abs() > 0.01 {
                let shift = (scroll.abs().ceil() as usize).clamp(1, 6);
                let top = self.queue_top.unwrap_or(auto_top);
                self.queue_top = Some(if scroll > 0.0 {
                    top.saturating_sub(shift)
                } else {
                    (top + shift).min(max_top)
                });
            }
            if self.clicked {
                self.queue_top = None;
            }
        }
        let top = self.queue_top.unwrap_or(auto_top).min(max_top);
        let mut hint = None;
        for (i, cmd) in sim.queue.iter().enumerate().skip(top).take(rows) {
            let row = Rect::new(
                list.x + 3.0,
                list.y + 3.0 + (i - top) as f32 * row_height,
                list.w - 14.0,
                row_height,
            );
            let current = i == sim.step;
            if current {
                rounded(row, 4.0, ACTIVE);
            } else if row.contains(self.mouse) {
                rounded(row, 4.0, SURFACE);
            }
            if row.contains(self.mouse) {
                hint = Some(cmd.description.as_str());
            }
            let (label, ink) = match cmd.action {
                CommandAction::TravelMagnetOff => ("Travel", AMBER),
                CommandAction::MagnetOn => ("Magnet on", MINT),
                CommandAction::MovePieceStraight => ("Carry piece", MINT),
                CommandAction::MagnetOff => ("Magnet off", TEXT),
            };
            let y = row.y + 16.0;
            self.text(
                &format!("{:02}", i + 1),
                row.x + 7.0,
                y,
                10,
                if current { MINT } else { QUIET },
                true,
            );
            self.text(
                label,
                row.x + 34.0,
                y,
                11,
                if i < sim.step { MUTED } else { ink },
                current,
            );
            self.right_text(
                &format!(
                    "{:.1}, {:.1}  →  {:.1}, {:.1}",
                    cmd.from.x, cmd.from.y, cmd.to.x, cmd.to.y
                ),
                row.right() - 7.0,
                y,
                11,
                MUTED,
                false,
            );
        }
        if max_top > 0 {
            let height = list.h - 8.0;
            let thumb = (height * rows as f32 / sim.queue.len() as f32).max(9.0);
            let y = list.y + 4.0 + (height - thumb) * top as f32 / max_top as f32;
            rounded(Rect::new(list.right() - 6.0, y, 3.0, thumb), 1.5, QUIET);
        }
        hint
    }

    fn button_surface(&self, r: Rect, style: Button) -> Color {
        let hovered = r.contains(self.mouse);
        let (bg, ink) = match (style, hovered) {
            (Button::Disabled, _) => (WELL, QUIET),
            (Button::Normal, false) => (SURFACE, TEXT),
            (Button::Normal, true) => (HOVER, TEXT),
            (Button::Selected, false) => (ACTIVE, MINT),
            (Button::Selected, true) => (color(0x2a514b), MINT),
            (Button::Primary, false) => (MINT, WELL),
            (Button::Primary, true) => (color(0xa3f2da), WELL),
        };
        rounded(r, 6.0, bg);
        ink
    }

    fn button(&self, r: Rect, label: &str, style: Button) -> bool {
        let ink = self.button_surface(r, style);
        self.centered(label, r.point() + r.size() * 0.5, 12, ink, true);
        !matches!(style, Button::Disabled) && self.clicked && r.contains(self.mouse)
    }

    fn font(&self, strong: bool) -> &Font {
        if strong {
            &self.strong
        } else {
            &self.font
        }
    }

    fn measure(&self, s: &str, size: u16, strong: bool) -> TextDimensions {
        measure_text(s, Some(self.font(strong)), size, 1.0)
    }

    fn text(&self, s: &str, x: f32, baseline: f32, size: u16, ink: Color, strong: bool) {
        draw_text_ex(
            s,
            x,
            baseline,
            TextParams {
                font: Some(self.font(strong)),
                font_size: size,
                color: ink,
                ..Default::default()
            },
        );
    }

    fn centered(&self, s: &str, p: Vec2, size: u16, ink: Color, strong: bool) {
        let m = self.measure(s, size, strong);
        self.text(
            s,
            p.x - m.width * 0.5,
            p.y + m.offset_y - m.height * 0.5,
            size,
            ink,
            strong,
        );
    }

    fn right_text(&self, s: &str, right: f32, baseline: f32, size: u16, ink: Color, strong: bool) {
        self.text(
            s,
            right - self.measure(s, size, strong).width,
            baseline,
            size,
            ink,
            strong,
        );
    }

    fn fit_text(&self, s: &str, x: f32, baseline: f32, width: f32, size: u16, ink: Color) {
        if self.measure(s, size, false).width <= width {
            self.text(s, x, baseline, size, ink, false);
        } else {
            let mut shortened = s.to_owned();
            while self.measure(&shortened, size, false).width + 13.0 > width {
                shortened.pop();
            }
            shortened.push_str("...");
            self.text(&shortened, x, baseline, size, ink, false);
        }
    }
}

fn toggle(selected: bool) -> Button {
    if selected {
        Button::Selected
    } else {
        Button::Normal
    }
}

fn enabled(available: bool, style: Button) -> Button {
    if available {
        style
    } else {
        Button::Disabled
    }
}

fn fill(r: Rect, c: Color) {
    draw_rectangle(r.x, r.y, r.w, r.h, c);
}

fn inset(r: Rect, margin: f32) -> Rect {
    Rect::new(
        r.x + margin,
        r.y + margin,
        r.w - margin * 2.0,
        r.h - margin * 2.0,
    )
}

fn rounded(r: Rect, radius: f32, c: Color) {
    fill(Rect::new(r.x + radius, r.y, r.w - radius * 2.0, r.h), c);
    fill(Rect::new(r.x, r.y + radius, r.w, r.h - radius * 2.0), c);
    for x in [r.x + radius, r.right() - radius] {
        for y in [r.y + radius, r.bottom() - radius] {
            draw_circle(x, y, radius, c);
        }
    }
}

fn card(r: Rect) {
    rounded(r, 10.0, BORDER);
    rounded(inset(r, 1.0), 9.0, PANEL);
}
