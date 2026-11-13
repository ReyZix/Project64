use movement_planner::*;

#[test]
fn test_standard_board_pieces_count() {
    let pieces = standard_board_layout();
    assert_eq!(pieces.len(), 34); // 32 standard pieces + 2 reserve queens
}

#[test]
fn test_pawn_one_step_move() {
    let mut config = DimensionsConfig::default();
    config.square_size_mm = 60.0;
    let planner = MovementPlanner::new(config);
    let mut pieces = standard_board_layout();

    let req = MoveRequest::simple(GridCoord::new(4, 1), GridCoord::new(4, 2));
    let cmds = planner.plan_move(&mut pieces, &req);

    assert!(!cmds.is_empty());
    let e3_piece = pieces.iter().find(|p| p.square == GridCoord::new(4, 2)).unwrap();
    assert_eq!(e3_piece.piece_type, PieceType::Pawn);
    assert_eq!(e3_piece.color, Color::White);
}

#[test]
fn test_knight_jump_generates_nudge_commands() {
    let mut config = DimensionsConfig::default();
    config.square_size_mm = 60.0;
    let planner = MovementPlanner::new(config);
    let mut pieces = standard_board_layout();

    let req = MoveRequest::simple(GridCoord::new(6, 0), GridCoord::new(5, 2));
    let cmds = planner.plan_move(&mut pieces, &req);

    assert!(!cmds.is_empty());
    let has_nudge = cmds.iter().any(|c| c.description.contains("Nudge"));
    assert!(has_nudge, "Knight move should generate edge nudging commands");
}

#[test]
fn test_castling_generates_king_and_rook_commands() {
    let mut config = DimensionsConfig::default();
    config.square_size_mm = 60.0;
    let planner = MovementPlanner::new(config);
    let mut pieces = standard_board_layout();
    pieces.retain(|p| !(p.color == Color::White && (p.square.col == 5 || p.square.col == 6) && p.square.row == 0));

    let mut req = MoveRequest::simple(GridCoord::new(4, 0), GridCoord::new(6, 0));
    req.is_castling = true;
    req.rook_from = Some(GridCoord::new(7, 0));
    req.rook_to = Some(GridCoord::new(5, 0));

    let cmds = planner.plan_move(&mut pieces, &req);
    assert!(!cmds.is_empty());

    let king = pieces.iter().find(|p| p.square == GridCoord::new(6, 0)).unwrap();
    assert_eq!(king.piece_type, PieceType::King);

    let rook = pieces.iter().find(|p| p.square == GridCoord::new(5, 0)).unwrap();
    assert_eq!(rook.piece_type, PieceType::Rook);
}
