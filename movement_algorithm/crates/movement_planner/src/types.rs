/**
 * Project 64 - Autonomous Chessboard Movement & Pathfinding Engine
 * Types and Configuration Data Structures
 */

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum PieceType {
    None = 0,
    Pawn,
    Knight,
    Bishop,
    Rook,
    Queen,
    King,
}

impl PieceType {
    pub fn char_code(self) -> char {
        match self {
            PieceType::None => ' ',
            PieceType::Pawn => 'P',
            PieceType::Knight => 'N',
            PieceType::Bishop => 'B',
            PieceType::Rook => 'R',
            PieceType::Queen => 'Q',
            PieceType::King => 'K',
        }
    }

    pub fn from_char(c: char) -> Option<Self> {
        match c.to_ascii_uppercase() {
            'P' => Some(PieceType::Pawn),
            'N' => Some(PieceType::Knight),
            'B' => Some(PieceType::Bishop),
            'R' => Some(PieceType::Rook),
            'Q' => Some(PieceType::Queen),
            'K' => Some(PieceType::King),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Color {
    White = 0,
    Black = 1,
}

pub type PieceColor = Color;

impl Color {
    pub fn opposite(self) -> Self {
        match self {
            Color::White => Color::Black,
            Color::Black => Color::White,
        }
    }

    pub fn prefix(self) -> char {
        match self {
            Color::White => 'W',
            Color::Black => 'B',
        }
    }
}

/// Physical board dimensions and kinematic parameters
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct DimensionsConfig {
    pub square_size_mm: f32,       // Width/height of one chess square (mm, default 50.0 or 60.0)
    pub piece_radius_mm: f32,      // Physical base radius of a standard chess piece (mm)
    pub safety_margin_mm: f32,     // Extra clearance margin between moving pieces (mm)
    pub max_edge_nudge_ratio: f32, // Max offset from square center (in square units, < 0.5 - r/S)
    pub gantry_travel_speed_mm_s: f32, // Carriage speed without a piece
    pub gantry_carry_speed_mm_s: f32,  // Carriage speed carrying a piece
}

impl Default for DimensionsConfig {
    fn default() -> Self {
        Self {
            square_size_mm: 50.0,
            piece_radius_mm: 14.0,
            safety_margin_mm: 3.0,
            max_edge_nudge_ratio: 0.34,
            gantry_travel_speed_mm_s: 80.0,
            gantry_carry_speed_mm_s: 45.0,
        }
    }
}

/// Piece physical diameter by type in mm
pub fn piece_diameter_mm(piece_type: PieceType) -> f32 {
    match piece_type {
        PieceType::Pawn => 28.0,
        PieceType::Knight | PieceType::Bishop | PieceType::Rook => 34.0,
        PieceType::Queen | PieceType::King => 36.0,
        PieceType::None => 28.0,
    }
}

pub fn piece_radius_mm(piece_type: PieceType) -> f32 {
    piece_diameter_mm(piece_type) / 2.0
}

/// Point in continuous square units (multiply by square_size_mm for physical mm)
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Point2D {
    pub x: f32,
    pub y: f32,
}

impl Point2D {
    pub const fn new(x: f32, y: f32) -> Self {
        Self { x, y }
    }

    pub fn dist(self, other: Point2D) -> f32 {
        let dx = self.x - other.x;
        let dy = self.y - other.y;
        (dx * dx + dy * dy).sqrt()
    }
}

/// Discrete grid coordinate
/// col: -2..9 (-2..-1 is Left Graveyard, 0..7 is Playable Board, 8..9 is Right Graveyard)
/// row: 0..7 (-1 is Front Transit Lane, 8 is Back Transit Lane)
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct GridCoord {
    pub col: i32,
    pub row: i32,
}

impl GridCoord {
    pub const fn new(col: i32, row: i32) -> Self {
        Self { col, row }
    }

    pub fn is_on_board(self) -> bool {
        self.col >= 0 && self.col < 8 && self.row >= 0 && self.row < 8
    }

    pub fn is_graveyard(self) -> bool {
        self.col < 0 || self.col > 7
    }

    pub fn to_algebraic(self) -> String {
        file_rank_label(self.col, self.row)
    }
}

pub fn file_rank_label(col: i32, row: i32) -> String {
    if col >= 0 && col < 8 && row >= 0 && row < 8 {
        let file = (b'a' + col as u8) as char;
        let rank = (b'1' + row as u8) as char;
        format!("{}{}", file, rank)
    } else if col < 0 {
        format!("GY-L[{},{}]", col, row)
    } else if col > 7 {
        format!("GY-R[{},{}]", col, row)
    } else {
        format!("Lane[{},{}]", col, row)
    }
}

pub fn parse_algebraic_square(s: &str) -> Option<GridCoord> {
    let bytes = s.trim().as_bytes();
    if bytes.len() != 2 {
        return None;
    }
    let col = match bytes[0] {
        b'a'..=b'h' => (bytes[0] - b'a') as i32,
        b'A'..=b'H' => (bytes[0] - b'A') as i32,
        _ => return None,
    };
    let row = match bytes[1] {
        b'1'..=b'8' => (bytes[1] - b'1') as i32,
        _ => return None,
    };
    Some(GridCoord::new(col, row))
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CommandAction {
    TravelMagnetOff = 0, // Move gantry carriage without piece
    MagnetOn = 1,        // Engage 12V electromagnet
    MovePieceStraight = 2, // Move piece in straight line (orthogonal or 45-degree diagonal only)
    MagnetOff = 3,       // Release 12V electromagnet
}

#[derive(Debug, Clone)]
pub struct GantryCommand {
    pub action: CommandAction,
    pub from: Point2D,
    pub to: Point2D,
    pub description: String,
}

#[derive(Debug, Clone)]
pub struct PieceState {
    pub id: i32,
    pub piece_type: PieceType,
    pub color: Color,
    pub square: GridCoord,
    pub current_pos: Point2D, // Current continuous sub-square position
    pub is_reserve: bool,
    pub has_moved: bool,
}

#[derive(Debug, Clone)]
pub struct MoveRequest {
    pub from: GridCoord,
    pub to: GridCoord,
    pub is_capture: bool,
    pub captured_square: Option<GridCoord>,
    pub is_promotion: bool,
    pub promotion_type: PieceType,
    pub is_castling: bool,
    pub rook_from: Option<GridCoord>,
    pub rook_to: Option<GridCoord>,
}

impl MoveRequest {
    pub fn simple(from: GridCoord, to: GridCoord) -> Self {
        Self {
            from,
            to,
            is_capture: false,
            captured_square: None,
            is_promotion: false,
            promotion_type: PieceType::Queen,
            is_castling: false,
            rook_from: None,
            rook_to: None,
        }
    }
}
