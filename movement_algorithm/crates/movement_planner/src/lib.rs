/**
 * Project 64 - Autonomous Chessboard Movement & Pathfinding Engine
 * Target: ESP32 Core 1 / Desktop / Robotics Motion Layer
 */

pub mod chess_rules;
pub mod planner;
pub mod reset;
pub mod scenarios;
pub mod types;

pub use chess_rules::{ChessBoard, ChessMove, GameState};
pub use planner::{GraveyardTrip, MovementPlanner, Route};
pub use reset::{plan_reset_board_with_magnet, standard_board_layout};
pub use scenarios::{load_scenario, ScenarioSetup};
pub use types::*;
