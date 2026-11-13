/**
 * Project 64 - Autonomous Chessboard Movement & Pathfinding Engine
 * Complete Chess Rules Engine: Move generation, legality, check/mate detection,
 * en-passant, castling, and preset scenarios.
 */

use crate::types::*;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GameState {
    Active,
    Check,
    Checkmate,
    Stalemate,
}

#[derive(Debug, Clone)]
pub struct ChessMove {
    pub from: GridCoord,
    pub to: GridCoord,
    pub is_capture: bool,
    pub captured_square: Option<GridCoord>,
    pub is_castling: bool,
    pub rook_from: Option<GridCoord>,
    pub rook_to: Option<GridCoord>,
    pub is_promotion: bool,
    pub promotion_type: PieceType,
}

pub struct ChessBoard {
    pub squares: [Option<(Color, PieceType, usize)>; 64], // (Color, Type, PieceState index)
    pub turn: Color,
    pub ep_target: Option<GridCoord>,
    pub white_kingside_castling: bool,
    pub white_queenside_castling: bool,
    pub black_kingside_castling: bool,
    pub black_queenside_castling: bool,
    pub state: GameState,
}

impl ChessBoard {
    pub fn new() -> Self {
        Self {
            squares: [None; 64],
            turn: Color::White,
            ep_target: None,
            white_kingside_castling: true,
            white_queenside_castling: true,
            black_kingside_castling: true,
            black_queenside_castling: true,
            state: GameState::Active,
        }
    }

    pub fn idx(col: i32, row: i32) -> usize {
        (row * 8 + col) as usize
    }

    pub fn sync_from_pieces(&mut self, pieces: &[PieceState]) {
        self.squares = [None; 64];
        for (i, p) in pieces.iter().enumerate() {
            if p.square.is_on_board() {
                self.squares[Self::idx(p.square.col, p.square.row)] = Some((p.color, p.piece_type, i));
            }
        }
    }

    pub fn is_attacked(&self, col: i32, row: i32, by_color: Color) -> bool {
        // Pawns
        let pawn_p_row = if by_color == Color::White { row - 1 } else { row + 1 };
        if pawn_p_row >= 0 && pawn_p_row < 8 {
            for dc in [-1, 1] {
                let pc = col + dc;
                if pc >= 0 && pc < 8 {
                    if let Some((c, pt, _)) = self.squares[Self::idx(pc, pawn_p_row)] {
                        if c == by_color && pt == PieceType::Pawn {
                            return true;
                        }
                    }
                }
            }
        }

        // Knights
        let knight_offsets = [
            (-2, -1), (-2, 1), (-1, -2), (-1, 2),
            (1, -2), (1, 2), (2, -1), (2, 1),
        ];
        for (dc, dr) in knight_offsets {
            let nc = col + dc;
            let nr = row + dr;
            if nc >= 0 && nc < 8 && nr >= 0 && nr < 8 {
                if let Some((c, pt, _)) = self.squares[Self::idx(nc, nr)] {
                    if c == by_color && pt == PieceType::Knight {
                        return true;
                    }
                }
            }
        }

        // King (1 step)
        for dc in -1..=1 {
            for dr in -1..=1 {
                if dc == 0 && dr == 0 {
                    continue;
                }
                let kc = col + dc;
                let kr = row + dr;
                if kc >= 0 && kc < 8 && kr >= 0 && kr < 8 {
                    if let Some((c, pt, _)) = self.squares[Self::idx(kc, kr)] {
                        if c == by_color && pt == PieceType::King {
                            return true;
                        }
                    }
                }
            }
        }

        // Sliders: Diagonals (Bishop, Queen)
        let diag_dirs = [(1, 1), (1, -1), (-1, 1), (-1, -1)];
        for (dc, dr) in diag_dirs {
            let mut c = col + dc;
            let mut r = row + dr;
            while c >= 0 && c < 8 && r >= 0 && r < 8 {
                if let Some((color, pt, _)) = self.squares[Self::idx(c, r)] {
                    if color == by_color && (pt == PieceType::Bishop || pt == PieceType::Queen) {
                        return true;
                    }
                    break;
                }
                c += dc;
                r += dr;
            }
        }

        // Sliders: Orthogonals (Rook, Queen)
        let ortho_dirs = [(1, 0), (-1, 0), (0, 1), (0, -1)];
        for (dc, dr) in ortho_dirs {
            let mut c = col + dc;
            let mut r = row + dr;
            while c >= 0 && c < 8 && r >= 0 && r < 8 {
                if let Some((color, pt, _)) = self.squares[Self::idx(c, r)] {
                    if color == by_color && (pt == PieceType::Rook || pt == PieceType::Queen) {
                        return true;
                    }
                    break;
                }
                c += dc;
                r += dr;
            }
        }

        false
    }

    pub fn is_in_check(&self, color: Color) -> bool {
        for r in 0..8 {
            for c in 0..8 {
                if let Some((sq_color, pt, _)) = self.squares[Self::idx(c, r)] {
                    if sq_color == color && pt == PieceType::King {
                        return self.is_attacked(c, r, color.opposite());
                    }
                }
            }
        }
        false
    }

    pub fn legal_moves_for_square(&self, col: i32, row: i32) -> Vec<ChessMove> {
        let Some((color, ptype, _)) = self.squares[Self::idx(col, row)] else {
            return Vec::new();
        };

        if color != self.turn {
            return Vec::new();
        }

        let mut candidate_moves = Vec::new();

        match ptype {
            PieceType::Pawn => {
                let dir = if color == Color::White { 1 } else { -1 };
                let start_row = if color == Color::White { 1 } else { 6 };
                let promo_row = if color == Color::White { 7 } else { 0 };

                // 1 step forward
                let nr1 = row + dir;
                if nr1 >= 0 && nr1 < 8 && self.squares[Self::idx(col, nr1)].is_none() {
                    let is_promo = nr1 == promo_row;
                    candidate_moves.push(ChessMove {
                        from: GridCoord::new(col, row),
                        to: GridCoord::new(col, nr1),
                        is_capture: false,
                        captured_square: None,
                        is_castling: false,
                        rook_from: None,
                        rook_to: None,
                        is_promotion: is_promo,
                        promotion_type: PieceType::Queen,
                    });

                    // 2 steps forward
                    let nr2 = row + 2 * dir;
                    if row == start_row && self.squares[Self::idx(col, nr2)].is_none() {
                        candidate_moves.push(ChessMove {
                            from: GridCoord::new(col, row),
                            to: GridCoord::new(col, nr2),
                            is_capture: false,
                            captured_square: None,
                            is_castling: false,
                            rook_from: None,
                            rook_to: None,
                            is_promotion: false,
                            promotion_type: PieceType::Queen,
                        });
                    }
                }

                // Captures & En Passant
                for dc in [-1, 1] {
                    let nc = col + dc;
                    let nr = row + dir;
                    if nc >= 0 && nc < 8 && nr >= 0 && nr < 8 {
                        let is_promo = nr == promo_row;
                        if let Some((occ_color, _, _)) = self.squares[Self::idx(nc, nr)] {
                            if occ_color == color.opposite() {
                                candidate_moves.push(ChessMove {
                                    from: GridCoord::new(col, row),
                                    to: GridCoord::new(nc, nr),
                                    is_capture: true,
                                    captured_square: Some(GridCoord::new(nc, nr)),
                                    is_castling: false,
                                    rook_from: None,
                                    rook_to: None,
                                    is_promotion: is_promo,
                                    promotion_type: PieceType::Queen,
                                });
                            }
                        } else if let Some(ep) = self.ep_target {
                            if ep.col == nc && ep.row == nr {
                                candidate_moves.push(ChessMove {
                                    from: GridCoord::new(col, row),
                                    to: GridCoord::new(nc, nr),
                                    is_capture: true,
                                    captured_square: Some(GridCoord::new(nc, row)),
                                    is_castling: false,
                                    rook_from: None,
                                    rook_to: None,
                                    is_promotion: false,
                                    promotion_type: PieceType::Queen,
                                });
                            }
                        }
                    }
                }
            }
            PieceType::Knight => {
                let knight_offsets = [
                    (-2, -1), (-2, 1), (-1, -2), (-1, 2),
                    (1, -2), (1, 2), (2, -1), (2, 1),
                ];
                for (dc, dr) in knight_offsets {
                    let nc = col + dc;
                    let nr = row + dr;
                    if nc >= 0 && nc < 8 && nr >= 0 && nr < 8 {
                        if let Some((occ_color, _, _)) = self.squares[Self::idx(nc, nr)] {
                            if occ_color == color.opposite() {
                                candidate_moves.push(ChessMove {
                                    from: GridCoord::new(col, row),
                                    to: GridCoord::new(nc, nr),
                                    is_capture: true,
                                    captured_square: Some(GridCoord::new(nc, nr)),
                                    is_castling: false,
                                    rook_from: None,
                                    rook_to: None,
                                    is_promotion: false,
                                    promotion_type: PieceType::Queen,
                                });
                            }
                        } else {
                            candidate_moves.push(ChessMove {
                                from: GridCoord::new(col, row),
                                to: GridCoord::new(nc, nr),
                                is_capture: false,
                                captured_square: None,
                                is_castling: false,
                                rook_from: None,
                                rook_to: None,
                                is_promotion: false,
                                promotion_type: PieceType::Queen,
                            });
                        }
                    }
                }
            }
            PieceType::Bishop | PieceType::Rook | PieceType::Queen => {
                let mut directions = Vec::new();
                if ptype == PieceType::Bishop || ptype == PieceType::Queen {
                    directions.extend_from_slice(&[(1, 1), (1, -1), (-1, 1), (-1, -1)]);
                }
                if ptype == PieceType::Rook || ptype == PieceType::Queen {
                    directions.extend_from_slice(&[(1, 0), (-1, 0), (0, 1), (0, -1)]);
                }
                for (dc, dr) in directions {
                    let mut nc = col + dc;
                    let mut nr = row + dr;
                    while nc >= 0 && nc < 8 && nr >= 0 && nr < 8 {
                        if let Some((occ_color, _, _)) = self.squares[Self::idx(nc, nr)] {
                            if occ_color == color.opposite() {
                                candidate_moves.push(ChessMove {
                                    from: GridCoord::new(col, row),
                                    to: GridCoord::new(nc, nr),
                                    is_capture: true,
                                    captured_square: Some(GridCoord::new(nc, nr)),
                                    is_castling: false,
                                    rook_from: None,
                                    rook_to: None,
                                    is_promotion: false,
                                    promotion_type: PieceType::Queen,
                                });
                            }
                            break;
                        }
                        candidate_moves.push(ChessMove {
                            from: GridCoord::new(col, row),
                            to: GridCoord::new(nc, nr),
                            is_capture: false,
                            captured_square: None,
                            is_castling: false,
                            rook_from: None,
                            rook_to: None,
                            is_promotion: false,
                            promotion_type: PieceType::Queen,
                        });
                        nc += dc;
                        nr += dr;
                    }
                }
            }
            PieceType::King => {
                for dc in -1..=1 {
                    for dr in -1..=1 {
                        if dc == 0 && dr == 0 {
                            continue;
                        }
                        let nc = col + dc;
                        let nr = row + dr;
                        if nc >= 0 && nc < 8 && nr >= 0 && nr < 8 {
                            if let Some((occ_color, _, _)) = self.squares[Self::idx(nc, nr)] {
                                if occ_color == color.opposite() {
                                    candidate_moves.push(ChessMove {
                                        from: GridCoord::new(col, row),
                                        to: GridCoord::new(nc, nr),
                                        is_capture: true,
                                        captured_square: Some(GridCoord::new(nc, nr)),
                                        is_castling: false,
                                        rook_from: None,
                                        rook_to: None,
                                        is_promotion: false,
                                        promotion_type: PieceType::Queen,
                                    });
                                }
                            } else {
                                candidate_moves.push(ChessMove {
                                    from: GridCoord::new(col, row),
                                    to: GridCoord::new(nc, nr),
                                    is_capture: false,
                                    captured_square: None,
                                    is_castling: false,
                                    rook_from: None,
                                    rook_to: None,
                                    is_promotion: false,
                                    promotion_type: PieceType::Queen,
                                });
                            }
                        }
                    }
                }

                // Castling
                if !self.is_in_check(color) {
                    let rank = if color == Color::White { 0 } else { 7 };
                    let kingside = if color == Color::White { self.white_kingside_castling } else { self.black_kingside_castling };
                    let queenside = if color == Color::White { self.white_queenside_castling } else { self.black_queenside_castling };

                    // Kingside (e1-g1 / e8-g8)
                    if kingside
                        && col == 4 && row == rank
                        && self.squares[Self::idx(5, rank)].is_none()
                        && self.squares[Self::idx(6, rank)].is_none()
                        && !self.is_attacked(5, rank, color.opposite())
                        && !self.is_attacked(6, rank, color.opposite())
                    {
                        candidate_moves.push(ChessMove {
                            from: GridCoord::new(4, rank),
                            to: GridCoord::new(6, rank),
                            is_capture: false,
                            captured_square: None,
                            is_castling: true,
                            rook_from: Some(GridCoord::new(7, rank)),
                            rook_to: Some(GridCoord::new(5, rank)),
                            is_promotion: false,
                            promotion_type: PieceType::Queen,
                        });
                    }

                    // Queenside (e1-c1 / e8-c8)
                    if queenside
                        && col == 4 && row == rank
                        && self.squares[Self::idx(3, rank)].is_none()
                        && self.squares[Self::idx(2, rank)].is_none()
                        && self.squares[Self::idx(1, rank)].is_none()
                        && !self.is_attacked(3, rank, color.opposite())
                        && !self.is_attacked(2, rank, color.opposite())
                    {
                        candidate_moves.push(ChessMove {
                            from: GridCoord::new(4, rank),
                            to: GridCoord::new(2, rank),
                            is_capture: false,
                            captured_square: None,
                            is_castling: true,
                            rook_from: Some(GridCoord::new(0, rank)),
                            rook_to: Some(GridCoord::new(3, rank)),
                            is_promotion: false,
                            promotion_type: PieceType::Queen,
                        });
                    }
                }
            }
            PieceType::None => {}
        }

        // Filter out moves that leave own king in check
        let mut legal = Vec::new();
        for m in candidate_moves {
            let mut test_board = self.clone();
            test_board.apply_move_internal(&m);
            if !test_board.is_in_check(color) {
                legal.push(m);
            }
        }
        legal
    }

    fn apply_move_internal(&mut self, m: &ChessMove) {
        let piece = self.squares[Self::idx(m.from.col, m.from.row)];
        self.squares[Self::idx(m.from.col, m.from.row)] = None;

        if let Some(cap) = m.captured_square {
            self.squares[Self::idx(cap.col, cap.row)] = None;
        }

        if m.is_promotion {
            if let Some((c, _, p_idx)) = piece {
                self.squares[Self::idx(m.to.col, m.to.row)] = Some((c, m.promotion_type, p_idx));
            }
        } else {
            self.squares[Self::idx(m.to.col, m.to.row)] = piece;
        }

        if m.is_castling {
            if let (Some(rf), Some(rt)) = (m.rook_from, m.rook_to) {
                let rook = self.squares[Self::idx(rf.col, rf.row)];
                self.squares[Self::idx(rf.col, rf.row)] = None;
                self.squares[Self::idx(rt.col, rt.row)] = rook;
            }
        }
    }

    pub fn make_move(&mut self, m: &ChessMove) {
        let piece = self.squares[Self::idx(m.from.col, m.from.row)];

        // Update castling rights
        if let Some((color, pt, _)) = piece {
            if pt == PieceType::King {
                if color == Color::White {
                    self.white_kingside_castling = false;
                    self.white_queenside_castling = false;
                } else {
                    self.black_kingside_castling = false;
                    self.black_queenside_castling = false;
                }
            } else if pt == PieceType::Rook {
                if color == Color::White {
                    if m.from.col == 0 && m.from.row == 0 { self.white_queenside_castling = false; }
                    if m.from.col == 7 && m.from.row == 0 { self.white_kingside_castling = false; }
                } else {
                    if m.from.col == 0 && m.from.row == 7 { self.black_queenside_castling = false; }
                    if m.from.col == 7 && m.from.row == 7 { self.black_kingside_castling = false; }
                }
            }
        }

        // Update En Passant target
        if let Some((_, PieceType::Pawn, _)) = piece {
            if (m.to.row - m.from.row).abs() == 2 {
                self.ep_target = Some(GridCoord::new(m.from.col, (m.from.row + m.to.row) / 2));
            } else {
                self.ep_target = None;
            }
        } else {
            self.ep_target = None;
        }

        self.apply_move_internal(m);
        self.turn = self.turn.opposite();

        // Update game state (Check, Checkmate, Stalemate)
        let in_check = self.is_in_check(self.turn);
        let mut has_legal = false;
        'outer: for r in 0..8 {
            for c in 0..8 {
                if let Some((sq_c, _, _)) = self.squares[Self::idx(c, r)] {
                    if sq_c == self.turn && !self.legal_moves_for_square(c, r).is_empty() {
                        has_legal = true;
                        break 'outer;
                    }
                }
            }
        }

        if in_check && !has_legal {
            self.state = GameState::Checkmate;
        } else if !in_check && !has_legal {
            self.state = GameState::Stalemate;
        } else if in_check {
            self.state = GameState::Check;
        } else {
            self.state = GameState::Active;
        }
    }

    pub fn to_move_request(&self, m: &ChessMove) -> MoveRequest {
        MoveRequest {
            from: m.from,
            to: m.to,
            is_capture: m.is_capture,
            captured_square: m.captured_square,
            is_promotion: m.is_promotion,
            promotion_type: m.promotion_type,
            is_castling: m.is_castling,
            rook_from: m.rook_from,
            rook_to: m.rook_to,
        }
    }
}

impl Clone for ChessBoard {
    fn clone(&self) -> Self {
        Self {
            squares: self.squares,
            turn: self.turn,
            ep_target: self.ep_target,
            white_kingside_castling: self.white_kingside_castling,
            white_queenside_castling: self.white_queenside_castling,
            black_kingside_castling: self.black_kingside_castling,
            black_queenside_castling: self.black_queenside_castling,
            state: self.state,
        }
    }
}
