use movement_planner::{
    parse_algebraic_square, plan_reset_board_with_magnet, scenarios, standard_board_layout,
    ChessBoard, ChessMove, CommandAction, DimensionsConfig, GantryCommand, GridCoord, MoveRequest,
    MovementPlanner, PieceColor, PieceState, PieceType, Point2D,
};

pub const DEMOS: [(&str, &str, &str); 5] = [
    ("Knight nudge", "g1 to f3", "knight_nudge"),
    ("Tight diagonal", "c1 to g5", "bishop_diagonal"),
    ("Capture", "e4 to d5", "capture_graveyard"),
    ("Castling", "e1 to g1", "castling"),
    ("Promotion", "a7 to a8", "promotion"),
];

pub struct Simulation {
    pub planner: MovementPlanner,
    pub board: ChessBoard,
    pub pieces: Vec<PieceState>,
    pub gantry: Point2D,
    pub magnet_on: bool,
    carried_piece: Option<usize>,
    planned_pieces: Vec<PieceState>,
    pub queue: Vec<GantryCommand>,
    pub step: usize,
    pub progress: f32,
    pub paused: bool,
    pub speed: f32,
    pub selected: Option<GridCoord>,
    pub legal_moves: Vec<ChessMove>,
    pub title: String,
    pub message: String,
    pub error: bool,
}

impl Simulation {
    pub fn new() -> Self {
        let pieces = standard_board_layout();
        let mut board = ChessBoard::new();
        board.sync_from_pieces(&pieces);
        Self {
            planner: MovementPlanner::new(DimensionsConfig {
                square_size_mm: 60.0,
                ..DimensionsConfig::default()
            }),
            board,
            pieces,
            gantry: Point2D::new(4.5, 0.5),
            magnet_on: false,
            carried_piece: None,
            planned_pieces: Vec::new(),
            queue: Vec::new(),
            step: 0,
            progress: 0.0,
            paused: false,
            speed: 1.5,
            selected: None,
            legal_moves: Vec::new(),
            title: String::new(),
            message: "Select a piece, enter a move, or run a demo.".to_owned(),
            error: false,
        }
    }

    pub fn busy(&self) -> bool {
        self.step < self.queue.len()
    }

    pub fn clear_selection(&mut self) {
        self.selected = None;
        self.legal_moves.clear();
        self.set_message("Select a piece to see its legal moves.");
    }

    pub fn reset(&mut self) {
        self.load_position(standard_board_layout());
        self.gantry = Point2D::new(4.5, 0.5);
        self.set_message("Starting position restored.");
    }

    fn load_position(&mut self, pieces: Vec<PieceState>) {
        self.pieces = pieces;
        self.board = ChessBoard::new();
        self.board.sync_from_pieces(&self.pieces);
        self.queue.clear();
        self.planned_pieces.clear();
        self.step = 0;
        self.progress = 0.0;
        self.paused = false;
        self.magnet_on = false;
        self.carried_piece = None;
        self.title.clear();
        self.clear_selection();
    }

    pub fn run_demo(&mut self, index: usize) {
        let (name, _, key) = DEMOS[index];
        if let Some(demo) = scenarios::load_scenario(key) {
            self.load_position(demo.pieces);
            // The promotion demo places both pawns on a7. Keep the advancing pawn there
            // so the rendered position and the chess position describe the same move.
            if key == "promotion" {
                self.pieces
                    .retain(|p| p.square != demo.move_request.from || p.color == PieceColor::White);
                self.board.sync_from_pieces(&self.pieces);
            }
            let req = demo.move_request;
            let chess_move = self
                .board
                .legal_moves_for_square(req.from.col, req.from.row)
                .into_iter()
                .find(|m| m.to == req.to);
            if self.plan_move(&req) {
                if let Some(mv) = chess_move {
                    self.board.make_move(&mv);
                } else {
                    // The planner demos may start from a setup that isn't a legal chess move.
                    self.board.turn = self.board.turn.opposite();
                }
                self.board.sync_from_pieces(&self.planned_pieces);
                self.title = name.to_owned();
                self.set_message(demo.description.replace(" -> ", " → "));
            }
        }
    }

    pub fn magnet_reset(&mut self) {
        if self.busy() {
            return;
        }
        let mut pieces = self.pieces.clone();
        let commands = plan_reset_board_with_magnet(&self.planner, &mut pieces, self.gantry);
        self.board = ChessBoard::new();
        self.board.sync_from_pieces(&pieces);
        if commands.is_empty() {
            self.pieces = pieces;
            self.clear_selection();
            self.set_message("Pieces are already in their starting positions.");
            return;
        }
        self.start(commands, pieces, "Magnet reset".to_owned());
        self.set_message("Returning pieces to their starting positions.");
    }

    pub fn select(&mut self, square: GridCoord) {
        if self.busy() {
            return;
        }
        if let Some(mv) = self.legal_moves.iter().find(|m| m.to == square).cloned() {
            self.play_move(mv);
            return;
        }
        if !square.is_on_board() || self.selected == Some(square) {
            self.clear_selection();
            return;
        }

        self.legal_moves = self.board.legal_moves_for_square(square.col, square.row);
        self.selected = (!self.legal_moves.is_empty()).then_some(square);
        if self.selected.is_some() {
            self.set_message(format!(
                "{} selected. Choose a highlighted square.",
                square.to_algebraic()
            ));
        } else {
            self.set_message("Select a piece with a legal move.");
        }
    }

    pub fn play_notation(&mut self, input: &str) -> bool {
        if self.busy() {
            return false;
        }
        let input = input.trim().to_ascii_lowercase();
        if !input.is_ascii() || !(4..=5).contains(&input.len()) {
            return self.move_error("Enter a move such as e2e4 or a7a8q.");
        }
        let (Some(from), Some(to)) = (
            parse_algebraic_square(&input[..2]),
            parse_algebraic_square(&input[2..4]),
        ) else {
            return self.move_error("Use files a–h and ranks 1–8, for example e2e4.");
        };
        let Some(mut mv) = self
            .board
            .legal_moves_for_square(from.col, from.row)
            .into_iter()
            .find(|m| m.to == to)
        else {
            return self.move_error("That move is unavailable for the side to move.");
        };
        if let Some(promotion) = input.as_bytes().get(4) {
            if !mv.is_promotion {
                return self.move_error("A piece suffix is only used for promotion.");
            }
            mv.promotion_type = match promotion {
                b'q' => PieceType::Queen,
                b'r' => PieceType::Rook,
                b'b' => PieceType::Bishop,
                b'n' => PieceType::Knight,
                _ => return self.move_error("Use q, r, b, or n to choose a promotion piece."),
            };
        }
        self.play_move(mv)
    }

    fn play_move(&mut self, mv: ChessMove) -> bool {
        let req = self.board.to_move_request(&mv);
        if !self.plan_move(&req) {
            return false;
        }
        self.board.make_move(&mv);
        self.board.sync_from_pieces(&self.planned_pieces);
        self.set_message(format!("Executing {}.", self.title));
        true
    }

    fn plan_move(&mut self, req: &MoveRequest) -> bool {
        // The planner updates its working position as it builds commands. Retain the
        // original position for playback and commit the planned state at completion.
        let mut pieces = self.pieces.clone();
        let mut commands = self.planner.plan_move(&mut pieces, req);
        if commands.is_empty() {
            return self.move_error("The planner could not generate a route.");
        }
        if req.is_promotion
            && !pieces
                .iter()
                .any(|p| p.square == req.to && p.piece_type == req.promotion_type)
        {
            return self.move_error("The requested promotion piece is unavailable in reserve.");
        }
        if commands[0].action == CommandAction::TravelMagnetOff {
            commands[0].from = self.gantry;
        }
        self.start(
            commands,
            pieces,
            format!("{} → {}", req.from.to_algebraic(), req.to.to_algebraic()),
        );
        true
    }

    fn start(&mut self, commands: Vec<GantryCommand>, pieces: Vec<PieceState>, title: String) {
        self.clear_selection();
        self.queue = commands;
        self.planned_pieces = pieces;
        self.title = title;
        self.step = 0;
        self.progress = 0.0;
        self.paused = false;
    }

    pub fn update(&mut self, dt: f32) {
        if self.paused || !self.busy() {
            return;
        }
        let cmd = &self.queue[self.step];
        let config = self.planner.config();
        let distance = cmd.from.dist(cmd.to) * config.square_size_mm;
        let duration = match cmd.action {
            CommandAction::MovePieceStraight => distance / config.gantry_carry_speed_mm_s,
            CommandAction::TravelMagnetOff => distance / config.gantry_travel_speed_mm_s,
            CommandAction::MagnetOn | CommandAction::MagnetOff => 0.15,
        };
        self.progress += dt / (duration / self.speed).max(0.01);
        if self.progress >= 1.0 {
            self.advance();
        } else if matches!(
            cmd.action,
            CommandAction::MovePieceStraight | CommandAction::TravelMagnetOff
        ) {
            self.gantry = Point2D::new(
                cmd.from.x + (cmd.to.x - cmd.from.x) * self.progress,
                cmd.from.y + (cmd.to.y - cmd.from.y) * self.progress,
            );
            if let Some(index) = self.carried_piece {
                self.pieces[index].current_pos = self.gantry;
            }
        }
    }

    pub fn advance(&mut self) {
        let Some(cmd) = self.queue.get(self.step) else {
            return;
        };
        self.gantry = cmd.to;
        if let Some(index) = self.carried_piece {
            self.pieces[index].current_pos = cmd.to;
        }
        match cmd.action {
            CommandAction::MagnetOn => {
                self.magnet_on = true;
                self.carried_piece = self
                    .pieces
                    .iter()
                    .position(|p| p.current_pos.dist(cmd.to) < 0.25);
            }
            CommandAction::MagnetOff => {
                self.magnet_on = false;
                self.carried_piece = None;
            }
            _ => {}
        }
        self.step += 1;
        self.progress = 0.0;
        if !self.busy() {
            self.pieces = std::mem::take(&mut self.planned_pieces);
            self.paused = false;
            self.set_message(format!("{} complete.", self.title));
        }
    }

    pub fn finish(&mut self) {
        while self.busy() {
            self.advance();
        }
    }

    fn move_error(&mut self, message: &str) -> bool {
        self.set_message(message);
        self.error = true;
        false
    }

    fn set_message(&mut self, message: impl Into<String>) {
        self.message = message.into();
        self.error = false;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn skipped_commands_apply_the_same_state_as_playback() {
        let mut sim = Simulation::new();
        assert!(sim.play_notation("e2e4"));
        let from = GridCoord::new(4, 1);
        let to = GridCoord::new(4, 3);
        assert!(sim.pieces.iter().any(|p| p.square == from));
        assert!(!sim.play_notation("e7e5"));

        sim.paused = true;
        sim.advance();
        sim.advance();
        assert!(sim.magnet_on);
        assert!(sim.paused);
        sim.advance();
        assert!(sim
            .pieces
            .iter()
            .any(|p| p.square == from && p.current_pos == MovementPlanner::square_center(to)));
        sim.finish();
        assert!(!sim.magnet_on);
        assert!(sim.pieces.iter().any(|p| p.square == to));
        assert!(sim.play_notation("e7e5"));
    }

    #[test]
    fn all_demo_queues_finish_on_the_displayed_chess_position() {
        let mut sim = Simulation::new();
        for i in 0..DEMOS.len() {
            sim.run_demo(i);
            assert!(sim.busy(), "{}", DEMOS[i].0);
            sim.finish();
            let mut physical_board = ChessBoard::new();
            physical_board.sync_from_pieces(&sim.pieces);
            assert_eq!(physical_board.squares, sim.board.squares, "{}", DEMOS[i].0);
            assert!(!sim.magnet_on);
        }
    }
}
