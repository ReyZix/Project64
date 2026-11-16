#import "P64GameSession.h"
#import "../Services/Hardware/P64Hardware.h"
#import "../Services/Online/P64LichessService.h"
#import "../Services/Online/P64TokenStore.h"
#include "ChessGame.hpp"
#include "StockfishEngine.hpp"
#include <algorithm>
#include <atomic>
#include <memory>

static NSString *P64String(const std::string &value) { return [NSString stringWithUTF8String:value.c_str()] ?: @""; }
static NSString *P64Text(id value) { return [value isKindOfClass:NSString.class] ? value : @""; }
static NSDictionary *P64Dict(id value) { return [value isKindOfClass:NSDictionary.class] ? value : @{}; }
static char P64Piece(NSString *fen, NSString *square) {
    if (square.length != 2) return 0;
    const char *position = fen.UTF8String;
    int file = 0, rank = 7;
    for (const char *c = position; *c && *c != ' '; ++c) {
        if (*c == '/') { file = 0; --rank; }
        else if (*c >= '1' && *c <= '8') file += *c - '0';
        else { if (file == [square characterAtIndex:0] - 'a' && rank == [square characterAtIndex:1] - '1') return *c; ++file; }
    }
    return 0;
}

typedef NS_ENUM(NSInteger, P64MoveOrigin) { P64MoveScreen, P64MoveBoard, P64MoveEngine, P64MoveRemote };

@interface P64GameSession ()
@property(nonatomic, readwrite, copy) NSString *selectedSquare, *lastMoveFrom, *lastMoveTo, *dangerSquare;
@property(nonatomic, readwrite, copy) NSArray<NSString *> *legalTargets, *promotionChoices, *inferredMoveChoices;
@property(nonatomic, readwrite, copy) NSString *errorText, *onlinePhase, *username, *onlineNotice;
@property(nonatomic, readwrite, copy) NSArray<NSDictionary *> *incomingChallenges;
@property(nonatomic, readwrite) NSTimeInterval whiteClock, blackClock;
@property(nonatomic, readwrite) P64GameMode mode;
@property(nonatomic, readwrite) P64Transport transport;
@property(nonatomic, readwrite) P64ConnectionStatus connection;
@property(nonatomic, readwrite) P64GantryStatus gantry;
@property(nonatomic, readwrite) BOOL thinking, alignmentRequired;
@end

@implementation P64GameSession {
    std::unique_ptr<project64::ChessGame> _game;
    std::shared_ptr<project64::StockfishEngine> _engine;
    std::shared_ptr<std::atomic_bool> _searchCancelled;
    id<P64HardwareBridge> _bridge;
    NSString *_engineDirectory, *_initialFen;
    NSURL *_baseURL;
    BOOL _running, _persistCredentials, _submitting, _writing, _waitingForFull;
    NSUInteger _revision, _hardwareGeneration, _onlineGeneration, _gameGeneration;
    NSTimer *_clockTimer;
    NSTimeInterval _lastTick;
    NSString *_pendingMove;
    P64MoveOrigin _pendingOrigin;
    BOOL _deliveryPaused;
    NSString *_promotionPrefix;
    NSString *_submittedMove;
    P64MoveOrigin _submittedOrigin;
    P64LichessService *_lichess;
    id<P64LichessStream> _events, _gameStream, _matchmaking;
    NSString *_accountID, *_gameID, *_myColor, *_opponent, *_remoteStatus, *_winner, *_outgoingChallenge;
    NSArray<NSString *> *_remoteMoves;
    NSString *_remoteInitialFen;
    BOOL _haveAuthoritativeSnapshot;
    BOOL _motionAwaiting, _motionObserved;
    NSUInteger _motionID, _writeOperation, _submissionOperation;
    NSString *_remoteTurn;
    NSString *_acceptedAwaitingEcho;
    NSUInteger _awaitingEchoPly;
    BOOL _halted;
}

- (instancetype)init {
    return [self initWithBridge:[P64MockHardwareBridge new]
               engineDirectory:NSBundle.mainBundle.resourcePath ?: @""
                 lichessBaseURL:[NSURL URLWithString:@"https://lichess.org"]];
}

- (instancetype)initWithBridge:(id<P64HardwareBridge>)bridge engineDirectory:(NSString *)directory lichessBaseURL:(NSURL *)baseURL {
    if ((self = [super init])) {
        _game = std::make_unique<project64::ChessGame>();
        _initialFen = P64String(_game->fen());
        _engineDirectory = [directory copy];
        _engine = std::make_shared<project64::StockfishEngine>(directory.UTF8String);
        _bridge = bridge; _baseURL = baseURL;
        _persistCredentials = [baseURL.scheme isEqualToString:@"https"] && [baseURL.host isEqualToString:@"lichess.org"];
        _mode = P64GameModeStockfish; _transport = P64TransportMock; _target = @"Simulator Mock"; _skill = 8;
        _selectedSquare = _lastMoveFrom = _lastMoveTo = _dangerSquare = _errorText = @"";
        _legalTargets = _promotionChoices = _inferredMoveChoices = @[];
        _onlinePhase = @"SIGNED_OUT"; _username = _onlineNotice = @""; _incomingChallenges = @[];
        _whiteClock = _blackClock = 600;
        _myColor = @"white"; _opponent = @"Online opponent"; _accountID = _gameID = _remoteStatus = _winner = @"";
        _remoteMoves = @[]; _remoteInitialFen = _initialFen;
    }
    return self;
}

- (void)dealloc {
    [_clockTimer invalidate];
    if (_searchCancelled) _searchCancelled->store(true);
    _engine->cancel();
    [_events cancel]; [_gameStream cancel]; [_matchmaking cancel]; [_lichess invalidate];
    _bridge.boardUpdate = nil; _bridge.gantryUpdate = nil; _bridge.connectionUpdate = nil;
    [_bridge disconnect];
}

- (void)notify { if (self.onChange) self.onChange(); }
- (NSString *)fen { return P64String(_game->fen()); }
- (NSString *)turn {
    const auto fen = _game->fen();
    auto space = fen.find(' ');
    return space != std::string::npos && fen[space + 1] == 'b' ? @"black" : @"white";
}
- (BOOL)onlineGameActive { return self.mode == P64GameModeOnline && [self.onlinePhase isEqualToString:@"PLAYING"]; }
- (BOOL)onlineFinished { return self.mode == P64GameModeOnline && [self.onlinePhase isEqualToString:@"FINISHED"]; }
- (BOOL)flipped { return self.mode == P64GameModeOnline && [_myColor isEqualToString:@"black"]; }
- (NSString *)whiteName {
    if (self.mode == P64GameModeOnline) return [_myColor isEqualToString:@"white"] ? (self.username.length ? self.username : @"You") : _opponent;
    return self.mode == P64GameModePassAndPlay ? @"Player 1" : @"You";
}
- (NSString *)blackName {
    if (self.mode == P64GameModeOnline) return [_myColor isEqualToString:@"black"] ? (self.username.length ? self.username : @"You") : _opponent;
    return self.mode == P64GameModeStockfish ? [NSString stringWithFormat:@"Stockfish · Level %ld", (long)self.skill] : @"Player 2";
}
- (NSString *)engineLabel { return self.mode == P64GameModeStockfish ? (self.thinking ? @"Stockfish is thinking…" : @"Native Stockfish 17.1") : @""; }
- (BOOL)inputLocked {
    if (!_running || _halted || self.alignmentRequired || _motionAwaiting || self.connection != P64ConnectionConnected || self.gantry != P64GantryIdle || _pendingMove || _writing || _submitting || self.thinking || _game->status().over) return YES;
    if (self.mode == P64GameModeOnline) return !self.onlineGameActive || _waitingForFull || ![self.turn isEqualToString:_myColor];
    if (self.whiteClock <= 0 || self.blackClock <= 0) return YES;
    return self.mode == P64GameModeStockfish && [self.turn isEqualToString:@"black"];
}
- (NSString *)statusText {
    if (self.alignmentRequired) return @"Align the physical board with this position, then confirm.";
    if (self.errorText.length) return self.errorText;
    if (self.gantry == P64GantryError) return @"Gantry error. Check the board and re-home before continuing.";
    if (self.gantry == P64GantryMoving) return @"Board is moving…";
    if (self.gantry == P64GantryHoming) return @"Board is homing…";
    if (_motionAwaiting) return @"Waiting for the board to complete its move…";
    if (self.connection != P64ConnectionConnected) return @"Connect a board, or use Simulator Mock.";
    if (self.mode == P64GameModeOnline) {
        if (self.onlineFinished) {
            NSString *result = _winner.length ? ([_winner isEqualToString:_myColor] ? @"You won" : @"You lost") : @"Game finished";
            return [NSString stringWithFormat:@"%@ · %@", result, _remoteStatus];
        }
        if (!self.onlineGameActive) return self.onlineNotice.length ? self.onlineNotice : @"Sign in or find an opponent to play online.";
        if (_waitingForFull) return @"Synchronizing with Lichess…";
        if (_submitting) return @"Waiting for Lichess to accept your move…";
        if (_pendingMove) return @"Waiting to deliver the confirmed move to the board…";
        return [self.turn isEqualToString:_myColor] ? @"Your turn" : @"Waiting for your opponent…";
    }
    const auto status = _game->status();
    if (status.over) return status.result == "checkmate" ? [NSString stringWithFormat:@"Checkmate · %s wins", status.winner.c_str()] : [NSString stringWithFormat:@"Draw · %s", status.drawReason.empty() ? status.result.c_str() : status.drawReason.c_str()];
    if (self.whiteClock <= 0 || self.blackClock <= 0) return @"Time is up";
    if (self.thinking) return @"Stockfish is thinking…";
    if (_pendingMove) return @"Waiting for the board…";
    return [NSString stringWithFormat:@"%@ to move%@", [self.turn capitalizedString], status.inCheck ? @" · Check" : @""];
}

- (void)clearSelection {
    self.selectedSquare = @""; self.legalTargets = @[]; self.promotionChoices = @[]; self.inferredMoveChoices = @[]; _promotionPrefix = nil;
}
- (void)invalidatePosition {
    ++_revision;
    ++_writeOperation; ++_submissionOperation;
    if (_searchCancelled) _searchCancelled->store(true);
    _engine->cancel(); self.thinking = NO;
    _pendingMove = nil; _writing = NO; _deliveryPaused = NO; _submitting = NO; _submittedMove = nil;
    [self clearSelection];
}
- (void)start {
    if (_running) return;
    _running = YES; _lastTick = NSProcessInfo.processInfo.systemUptime;
    [self bindBridge];
    __weak P64GameSession *weakSelf = self;
    _clockTimer = [NSTimer scheduledTimerWithTimeInterval:0.2 repeats:YES block:^(NSTimer *timer) { [weakSelf tick]; }];
    [self connect];
}
- (void)stop {
    if (!_running) return;
    _running = NO; ++_hardwareGeneration;
    [self invalidatePosition]; [self closeOnline];
    [_clockTimer invalidate]; _clockTimer = nil;
    [_bridge disconnect]; [self notify];
}
- (void)updateClocks {
    NSTimeInterval now = NSProcessInfo.processInfo.systemUptime;
    NSTimeInterval elapsed = MAX(0, now - _lastTick); _lastTick = now;
    BOOL active = self.mode == P64GameModeOnline ? self.onlineGameActive && !_waitingForFull : !_game->history().empty() && !_game->status().over;
    if (active && !self.alignmentRequired) {
        NSString *clockTurn = self.mode == P64GameModeOnline ? (_remoteTurn ?: self.turn) : self.turn;
        if ([clockTurn isEqualToString:@"white"]) self.whiteClock = MAX(0, self.whiteClock - elapsed);
        else self.blackClock = MAX(0, self.blackClock - elapsed);
    }
}
- (void)tick { [self updateClocks]; [self notify]; }
- (void)bindBridge {
    const NSUInteger generation = ++_hardwareGeneration;
    __weak P64GameSession *weakSelf = self;
    _bridge.connectionUpdate = ^(P64ConnectionStatus status) {
        P64GameSession *s = weakSelf; if (!s || generation != s->_hardwareGeneration || !s->_running) return;
        s.connection = status;
        if (status != P64ConnectionConnected) {
            if (s->_searchCancelled) s->_searchCancelled->store(true);
            s->_engine->cancel(); s.thinking = NO;
            ++s->_writeOperation; ++s->_submissionOperation;
            if (s->_submitting && s->_submittedOrigin == P64MoveBoard && s.transport != P64TransportMock) s.alignmentRequired = YES;
            s->_submitting = NO; s->_submittedMove = nil;
            if (s->_motionAwaiting && s.transport != P64TransportMock) { s.alignmentRequired = YES; s.errorText = @"Connection lost during motion. Check the physical board."; }
        }
        if (status != P64ConnectionConnected && (s->_writing || s->_pendingMove)) {
            s->_deliveryPaused = YES;
            s.errorText = @"Board connection lost. Check the position before retrying.";
            if (s->_writing && s.transport != P64TransportMock) s.alignmentRequired = YES;
            s->_writing = NO;
        }
        if (status == P64ConnectionConnected) { [s drainDelivery]; [s maybeStartEngine]; }
        [s notify];
    };
    _bridge.gantryUpdate = ^(P64GantryStatus status) {
        P64GameSession *s = weakSelf; if (!s || generation != s->_hardwareGeneration || !s->_running) return;
        s.gantry = status;
        if (s->_motionAwaiting && (status == P64GantryMoving || status == P64GantryHoming)) s->_motionObserved = YES;
        if (s->_motionAwaiting && status == P64GantryIdle && s->_motionObserved) { s->_motionAwaiting = NO; ++s->_motionID; }
        if (status == P64GantryIdle) { [s drainDelivery]; [s reconcileRemote]; [s maybeStartEngine]; }
        [s notify];
    };
    _bridge.boardUpdate = ^(NSData *data) {
        P64GameSession *s = weakSelf; if (!s || generation != s->_hardwareGeneration || s.inputLocked) return;
        NSString *payload = P64DecodeBoardPayload(data);
        if (!payload) return;
        if (payload.length == 4 || payload.length == 5) { [s submitMove:payload origin:P64MoveBoard]; return; }
        uint64_t occupancy;
        if (!P64OccupancyFromPayload(data, &occupancy) || occupancy == s->_game->occupancy()) return;
        auto choices = s->_game->inferMoves(occupancy);
        if (choices.size() == 1) [s submitMove:P64String(choices.front()) origin:P64MoveBoard];
        else if (choices.size() > 1) {
            NSMutableArray *list = [NSMutableArray array];
            for (const auto &choice : choices) [list addObject:P64String(choice)];
            s.inferredMoveChoices = list;
            s.onlineNotice = @"Choose the move made on the physical board.";
            [s notify];
        }
    };
}
- (void)connect {
    const NSUInteger generation = _hardwareGeneration;
    __weak P64GameSession *weakSelf = self;
    [_bridge connectToTarget:self.target completion:^(NSError *error) {
        P64GameSession *s = weakSelf; if (!s || !s->_running || generation != s->_hardwareGeneration) return;
        s.connection = s->_bridge.connectionStatus;
        if (error) s.errorText = error.localizedDescription;
        else { s.errorText = @""; [s drainDelivery]; [s maybeStartEngine]; }
        [s notify];
    }];
}
- (void)disconnect {
    ++_hardwareGeneration;
    ++_writeOperation; ++_submissionOperation;
    if (_submitting && _submittedOrigin == P64MoveBoard && self.transport != P64TransportMock) self.alignmentRequired = YES;
    _submitting = NO; _submittedMove = nil;
    if (_searchCancelled) _searchCancelled->store(true);
    _engine->cancel(); self.thinking = NO;
    if (_pendingMove || _writing) { _deliveryPaused = YES; self.errorText = @"Disconnected. Check the board before retrying."; }
    if (_writing && self.transport != P64TransportMock) self.alignmentRequired = YES;
    _writing = NO; [self clearSelection];
    [_bridge disconnect]; self.connection = P64ConnectionDisconnected;
    [self bindBridge]; [self notify];
}
- (void)setHardwareTransport:(P64Transport)transport {
    if (transport == self.transport) return;
    [self disconnect];
    _bridge.boardUpdate = nil; _bridge.gantryUpdate = nil; _bridge.connectionUpdate = nil;
    _transport = transport;
    if (transport == P64TransportBLE) { _bridge = [P64BLEHardwareBridge new]; self.target = @"Project64"; }
    else if (transport == P64TransportWiFi) { _bridge = [P64WiFiHardwareBridge new]; self.target = @"192.168.4.1"; }
    else { _bridge = [P64MockHardwareBridge new]; self.target = @"Simulator Mock"; }
    self.gantry = P64GantryIdle; self.errorText = @"";
    _motionAwaiting = NO; _motionObserved = NO; ++_motionID;
    self.alignmentRequired = transport != P64TransportMock && (!_game->history().empty() || _pendingMove != nil);
    [self bindBridge]; if (transport == P64TransportMock && _running) [self connect]; [self notify];
}
- (void)setGameMode:(P64GameMode)mode {
    if (mode == self.mode) return;
    [self closeOnline]; _mode = mode; [self resetGame];
    if (mode == P64GameModeOnline && _persistCredentials) {
        NSError *error = nil; NSString *token = [P64TokenStore loadToken:&error];
        if (token.length) [self loginWithToken:token];
        else if (error) self.errorText = error.localizedDescription;
    }
    [self notify];
}
- (void)setSkill:(NSInteger)skill { _skill = MIN(20, MAX(0, skill)); [self notify]; }
- (void)resetGame {
    BOOL needsAlignment = self.transport != P64TransportMock && (!_game->history().empty() || self.gantry != P64GantryIdle || _writing);
    [self invalidatePosition]; _engine->newGame();
    _halted = NO;
    _game = std::make_unique<project64::ChessGame>(); _initialFen = P64String(_game->fen());
    self.lastMoveFrom = self.lastMoveTo = self.dangerSquare = self.errorText = @"";
    self.whiteClock = self.blackClock = 600; _lastTick = NSProcessInfo.processInfo.systemUptime;
    self.alignmentRequired = needsAlignment;
}
- (void)newGame {
    if (self.mode == P64GameModeOnline && self.onlineGameActive) { self.errorText = @"Finish or abort the online game before starting another."; [self notify]; return; }
    [self resetGame]; [self notify];
}
- (void)sendSystemCommand:(P64SystemCommand)command {
    if (command == P64SystemEmergencyStop || command == P64SystemReboot) {
        BOOL uncertain = self.gantry != P64GantryIdle || _writing || _motionAwaiting;
        [self invalidatePosition];
        _halted = YES;
        self.errorText = @"Board stopped. Check its position, then Retry to continue.";
        if (uncertain && self.transport != P64TransportMock) self.alignmentRequired = YES;
    }
    const NSUInteger generation = _hardwareGeneration;
    __weak P64GameSession *weakSelf = self;
    [_bridge sendCommand:command completion:^(NSError *error) {
        P64GameSession *s = weakSelf; if (!s || generation != s->_hardwareGeneration) return;
        if (error) s.errorText = error.localizedDescription;
        else if (command == P64SystemHome) s.errorText = @"";
        [s notify];
    }];
}
- (void)confirmBoardAlignment {
    if (self.gantry != P64GantryIdle || self.connection != P64ConnectionConnected) { self.errorText = @"Wait for a connected, idle board before confirming."; [self notify]; return; }
    self.alignmentRequired = NO;
    self.errorText = _halted ? @"Board stopped. Retry when you are ready to continue." : @"";
    _motionAwaiting = NO; _motionObserved = NO; ++_motionID;
    [self reconcileRemote]; [self maybeStartEngine];
    [self notify];
}

- (void)tapSquare:(NSString *)square {
    if (self.inputLocked || square.length != 2) return;
    self.dangerSquare = @"";
    if (self.selectedSquare.length && [self.legalTargets containsObject:square]) {
        NSString *prefix = [self.selectedSquare stringByAppendingString:square];
        NSMutableArray *promotions = [NSMutableArray array];
        for (const auto &move : _game->legalMoves()) if (move.size() == 5 && [P64String(move) hasPrefix:prefix]) [promotions addObject:P64String(move.substr(4))];
        if (promotions.count) { _promotionPrefix = prefix; self.promotionChoices = promotions; [self notify]; }
        else [self submitMove:prefix origin:P64MoveScreen];
        return;
    }
    char piece = P64Piece(self.fen, square);
    BOOL own = piece && ((piece >= 'A' && piece <= 'Z') == [self.turn isEqualToString:@"white"]);
    [self clearSelection];
    if (own) {
        self.selectedSquare = square;
        NSMutableOrderedSet *targets = [NSMutableOrderedSet orderedSet];
        for (const auto &move : _game->legalMoves()) if ([P64String(move) hasPrefix:square]) [targets addObject:P64String(move.substr(2, 2))];
        self.legalTargets = targets.array;
    }
    [self notify];
}
- (void)playUCI:(NSString *)uci { [self submitMove:uci origin:P64MoveScreen]; }
- (void)choosePromotion:(NSString *)piece {
    if (![self.promotionChoices containsObject:piece] || !_promotionPrefix || self.inputLocked) return;
    NSString *uci = [_promotionPrefix stringByAppendingString:piece]; [self submitMove:uci origin:P64MoveScreen];
}
- (void)chooseInferredMove:(NSString *)uci {
    if ([self.inferredMoveChoices containsObject:uci] && !self.inputLocked) [self submitMove:uci origin:P64MoveBoard];
}
- (void)submitMove:(NSString *)uci origin:(P64MoveOrigin)origin {
    [self updateClocks];
    if (self.inputLocked) return;
    auto candidate = *_game;
    if (!candidate.apply(uci.UTF8String)) { self.dangerSquare = uci.length >= 2 ? [uci substringToIndex:2] : @""; self.errorText = @"Illegal move"; [self notify]; return; }
    self.errorText = @""; [self clearSelection];
    if (self.mode != P64GameModeOnline) {
        if (origin == P64MoveBoard) { [self commitMove:uci]; [self maybeStartEngine]; }
        else [self queueMove:uci origin:origin];
        return;
    }
    _submitting = YES; _submittedMove = uci; _submittedOrigin = origin;
    const NSUInteger generation = _onlineGeneration, gameGeneration = _gameGeneration, revision = _revision, operation = ++_submissionOperation;
    __weak P64GameSession *weakSelf = self;
    [_lichess makeMove:uci gameID:_gameID completion:^(NSDictionary *response, NSError *error) {
        P64GameSession *s = weakSelf;
        if (!s || generation != s->_onlineGeneration || gameGeneration != s->_gameGeneration || revision != s->_revision || operation != s->_submissionOperation || ![s->_submittedMove isEqualToString:uci]) return;
        const auto ply = s->_game->history().size();
        BOOL confirmed = s->_remoteMoves.count > ply && [s->_remoteMoves[ply] isEqualToString:uci];
        s->_submitting = NO; s->_submittedMove = nil;
        if (error && !confirmed) {
            s.errorText = error.localizedDescription;
            if (origin == P64MoveBoard) s.alignmentRequired = YES;
            [s reconcileRemote]; [s notify]; return;
        }
        s->_acceptedAwaitingEcho = confirmed ? nil : uci;
        s->_awaitingEchoPly = ply;
        if (origin == P64MoveBoard) { [s commitMove:uci]; [s reconcileRemote]; }
        else [s queueMove:uci origin:P64MoveScreen];
    }];
    [self notify];
}
- (void)commitMove:(NSString *)uci {
    [self updateClocks];
    if (!_game->apply(uci.UTF8String)) { self.errorText = @"Move no longer matches the current position."; return; }
    ++_revision; self.thinking = NO; self.errorText = @""; [self clearSelection];
    self.lastMoveFrom = [uci substringToIndex:2]; self.lastMoveTo = [uci substringWithRange:NSMakeRange(2, 2)]; self.dangerSquare = @"";
    if (_game->status().inCheck) {
        for (int rank = 1; rank <= 8; ++rank) for (char file = 'a'; file <= 'h'; ++file) {
            NSString *square = [NSString stringWithFormat:@"%c%d", file, rank];
            if (P64Piece(self.fen, square) == ([self.turn isEqualToString:@"white"] ? 'K' : 'k')) self.dangerSquare = square;
        }
    }
    _lastTick = NSProcessInfo.processInfo.systemUptime; [self notify];
}
- (void)queueMove:(NSString *)uci origin:(P64MoveOrigin)origin {
    if (_pendingMove) return;
    _pendingMove = [uci copy]; _pendingOrigin = origin; _deliveryPaused = NO;
    [self drainDelivery]; [self notify];
}
- (void)drainDelivery {
    [self updateClocks];
    if (!_running || _halted || !_pendingMove || _writing || _motionAwaiting || _deliveryPaused || self.alignmentRequired || self.connection != P64ConnectionConnected || self.gantry != P64GantryIdle) return;
    if (self.mode != P64GameModeOnline && (self.whiteClock <= 0 || self.blackClock <= 0 || _game->status().over)) { _pendingMove = nil; [self notify]; return; }
    const NSUInteger revision = _revision, hardware = _hardwareGeneration, gameGeneration = _gameGeneration, operation = ++_writeOperation;
    NSString *uci = _pendingMove;
    _writing = YES;
    _motionAwaiting = YES; _motionObserved = NO;
    const NSUInteger motion = ++_motionID;
    __weak P64GameSession *weakSelf = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        P64GameSession *s = weakSelf;
        if (!s || motion != s->_motionID || !s->_motionAwaiting) return;
        s.errorText = @"Board completion was not confirmed. Check its position before continuing.";
        s.alignmentRequired = YES; s->_deliveryPaused = YES; [s notify];
    });
    [_bridge sendMove:uci completion:^(NSError *error) {
        P64GameSession *s = weakSelf;
        if (!s || revision != s->_revision || hardware != s->_hardwareGeneration || gameGeneration != s->_gameGeneration || operation != s->_writeOperation || ![s->_pendingMove isEqualToString:uci]) return;
        s->_writing = NO;
        [s updateClocks];
        if (error) {
            s->_deliveryPaused = YES; s.errorText = error.localizedDescription;
            if (s.transport == P64TransportMock && !s->_motionObserved) { s->_motionAwaiting = NO; ++s->_motionID; }
            else s.alignmentRequired = YES;
            [s notify]; return;
        }
        if (s.mode != P64GameModeOnline && (s.whiteClock <= 0 || s.blackClock <= 0)) {
            s->_pendingMove = nil;
            if (s.transport != P64TransportMock) s.alignmentRequired = YES;
            s.errorText = @"Time expired while the board command was pending."; [s notify]; return;
        }
        s->_pendingMove = nil;
        [s commitMove:uci]; [s reconcileRemote]; [s maybeStartEngine]; [s notify];
    }];
}
- (void)maybeStartEngine {
    if (!_running || _halted || self.mode != P64GameModeStockfish || ![self.turn isEqualToString:@"black"] || self.thinking || _pendingMove || _writing || self.errorText.length || self.alignmentRequired || _game->status().over || self.whiteClock <= 0 || self.blackClock <= 0 || self.connection != P64ConnectionConnected) return;
    self.thinking = YES;
    const NSUInteger revision = _revision;
    const auto initialFen = std::string(_initialFen.UTF8String);
    const auto moves = _game->history();
    const int skill = (int)self.skill;
    auto engine = _engine;
    auto cancelled = std::make_shared<std::atomic_bool>(false); _searchCancelled = cancelled;
    __weak P64GameSession *weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        if (cancelled->load()) return;
        auto result = engine->search(initialFen, moves, 12, 1000, skill, cancelled.get());
        dispatch_async(dispatch_get_main_queue(), ^{
            P64GameSession *s = weakSelf;
            if (!s || cancelled->load() || revision != s->_revision || s.mode != P64GameModeStockfish || !s->_running) return;
            s.thinking = NO;
            if (s.whiteClock <= 0 || s.blackClock <= 0 || s->_game->status().over) { [s notify]; return; }
            if (result.cancelled) { [s notify]; return; }
            if (!result.error.empty()) { s.errorText = P64String(result.error); [s notify]; return; }
            [s queueMove:P64String(result.bestMove) origin:P64MoveEngine];
        });
    });
    [self notify];
}
- (void)retry {
    self.errorText = @"";
    _halted = NO;
    _deliveryPaused = NO;
    if (_pendingMove) [self drainDelivery];
    else { [self reconcileRemote]; [self maybeStartEngine]; }
    if (self.mode == P64GameModeOnline && _lichess) {
        if (!_events && self.username.length) [self openEventStream];
        if (!_gameStream && self.onlineGameActive) [self openGameStream];
    }
    [self notify];
}

// MARK: Lichess account and streams
- (void)closeOnline {
    ++_onlineGeneration; ++_gameGeneration;
    [_events cancel]; _events = nil; [_gameStream cancel]; _gameStream = nil; [_matchmaking cancel]; _matchmaking = nil;
    [_lichess invalidate]; _lichess = nil;
    _gameID = _accountID = @""; _outgoingChallenge = nil; _remoteMoves = @[];
    _haveAuthoritativeSnapshot = NO; _waitingForFull = NO; self.onlinePhase = @"SIGNED_OUT";
    _acceptedAwaitingEcho = nil; _remoteTurn = @"white";
    _myColor = @"white"; _opponent = @"Online opponent";
    self.username = self.onlineNotice = @""; self.incomingChallenges = @[];
}
- (void)loginWithToken:(NSString *)token {
    if (self.mode != P64GameModeOnline) return;
    NSString *trimmed = [token stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if (!trimmed.length) { self.errorText = @"Enter a Lichess token."; [self notify]; return; }
    [self invalidatePosition]; [self closeOnline];
    const NSUInteger generation = _onlineGeneration;
    self.onlinePhase = @"CHECKING"; self.errorText = @"";
    _lichess = [[P64LichessService alloc] initWithToken:trimmed baseURL:_baseURL];
    __weak P64GameSession *weakSelf = self;
    [_lichess account:^(NSDictionary *response, NSError *error) {
        P64GameSession *s = weakSelf; if (!s || generation != s->_onlineGeneration || s.mode != P64GameModeOnline) return;
        NSString *account = P64Text(response[@"id"]), *name = P64Text(response[@"username"]);
        if (error || !account.length || !name.length) {
            s.errorText = error.localizedDescription ?: @"Lichess returned an invalid account.";
            s.onlinePhase = @"SIGNED_OUT"; [s->_lichess invalidate]; s->_lichess = nil; [s notify]; return;
        }
        if (s->_persistCredentials) {
            NSError *saveError = nil;
            if (![P64TokenStore saveToken:trimmed error:&saveError]) s.errorText = saveError.localizedDescription ?: @"Token could not be saved to Keychain.";
        }
        s->_accountID = account; s.username = name; s.onlinePhase = @"LOBBY";
        [s openEventStream]; [s notify];
    }];
    [self notify];
}
- (void)logout {
    [self invalidatePosition]; [self closeOnline];
    NSError *error = nil;
    if (_persistCredentials) [P64TokenStore deleteToken:&error];
    [self resetGame]; self.errorText = error.localizedDescription ?: @""; [self notify];
}
- (NSTimeInterval)reconnectDelay:(NSError *)error { return error.code == 429 ? 61 : 5; }
- (BOOL)authenticationError:(NSError *)error { return error.code == 401; }
- (void)openEventStream {
    if (!_lichess || self.mode != P64GameModeOnline || !self.username.length || !_running) return;
    const NSUInteger generation = _onlineGeneration;
    __weak P64GameSession *weakSelf = self;
    __block __weak id<P64LichessStream> identity;
    id<P64LichessStream> stream = [_lichess streamEvents:^(NSDictionary *event) {
        P64GameSession *s = weakSelf; if (s && generation == s->_onlineGeneration) [s handleAccountEvent:event];
    } completion:^(NSError *error) {
        P64GameSession *s = weakSelf;
        if (!s || generation != s->_onlineGeneration || s->_events != identity || !s->_running) return;
        s->_events = nil;
        if ([s authenticationError:error]) { NSString *message = error.localizedDescription; [s logout]; s.errorText = message; [s notify]; return; }
        s.onlineNotice = @"Online connection interrupted. Reconnecting…"; [s notify];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)([s reconnectDelay:error] * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            P64GameSession *current = weakSelf;
            if (current && generation == current->_onlineGeneration && !current->_events) [current openEventStream];
        });
    }];
    identity = stream; _events = stream;
}
- (void)handleAccountEvent:(NSDictionary *)event {
    NSString *type = P64Text(event[@"type"]);
    if ([type isEqualToString:@"gameStart"]) {
        NSDictionary *game = P64Dict(event[@"game"]);
        NSString *gameID = P64Text(game[@"gameId"]); if (!gameID.length) gameID = P64Text(game[@"id"]);
        NSString *status = P64Text(P64Dict(game[@"status"])[@"name"]);
        if (gameID.length && (!status.length || [status isEqualToString:@"created"] || [status isEqualToString:@"started"])) {
            if ([_gameID isEqualToString:gameID]) return;
            [self invalidatePosition]; ++_gameGeneration; [_gameStream cancel]; _gameStream = nil;
            _gameID = gameID; _myColor = [P64Text(game[@"color"]) isEqualToString:@"black"] ? @"black" : @"white";
            _opponent = P64Text(P64Dict(game[@"opponent"])[@"username"]); if (!_opponent.length) _opponent = @"Opponent";
            _remoteMoves = @[]; _haveAuthoritativeSnapshot = NO; _waitingForFull = YES; _remoteStatus = @"started"; _winner = @"";
            self.onlinePhase = @"PLAYING"; self.onlineNotice = @""; self.errorText = @"";
            [self cancelMatchmaking]; self.incomingChallenges = @[]; [self openGameStream]; [self notify];
        }
    } else if ([type isEqualToString:@"challenge"]) {
        NSDictionary *challenge = P64Dict(event[@"challenge"]), *challenger = P64Dict(challenge[@"challenger"]);
        NSString *identifier = P64Text(challenge[@"id"]), *variant = P64Text(P64Dict(challenge[@"variant"])[@"key"]);
        if (!identifier.length || [P64Text(challenger[@"id"]) isEqualToString:_accountID] || (variant.length && ![variant isEqualToString:@"standard"])) return;
        NSMutableArray *list = [self.incomingChallenges mutableCopy];
        for (NSDictionary *item in list) if ([item[@"id"] isEqualToString:identifier]) return;
        NSString *timeControl = P64Text(P64Dict(challenge[@"timeControl"])[@"show"]);
        [list addObject:@{@"id":identifier, @"username":P64Text(challenger[@"name"]), @"timeControl":timeControl.length ? timeControl : @"Clock game"}];
        self.incomingChallenges = list; [self notify];
    } else if ([type isEqualToString:@"challengeCanceled"] || [type isEqualToString:@"challengeDeclined"]) {
        NSString *identifier = P64Text(P64Dict(event[@"challenge"])[@"id"]);
        NSMutableArray *list = [NSMutableArray array];
        for (NSDictionary *item in self.incomingChallenges) if (![item[@"id"] isEqualToString:identifier]) [list addObject:item];
        self.incomingChallenges = list;
        if (identifier.length && [_outgoingChallenge isEqualToString:identifier]) { [self cancelMatchmaking]; self.onlineNotice = @"Challenge declined or cancelled."; }
        [self notify];
    } else if ([type isEqualToString:@"gameFinish"] && [P64Text(P64Dict(event[@"game"])[@"gameId"]) isEqualToString:_gameID]) {
        // The game stream carries the final move list and result; do not drop it here.
        [self notify];
    }
}
- (void)openGameStream {
    if (!_lichess || !_gameID.length || !self.onlineGameActive || !_running) return;
    const NSUInteger generation = _onlineGeneration, gameGeneration = _gameGeneration;
    NSString *gameID = _gameID;
    __weak P64GameSession *weakSelf = self;
    __block __weak id<P64LichessStream> identity;
    id<P64LichessStream> stream = [_lichess streamGame:gameID event:^(NSDictionary *event) {
        P64GameSession *s = weakSelf;
        if (s && generation == s->_onlineGeneration && gameGeneration == s->_gameGeneration) [s handleGameEvent:event];
    } completion:^(NSError *error) {
        P64GameSession *s = weakSelf;
        if (!s || generation != s->_onlineGeneration || gameGeneration != s->_gameGeneration || s->_gameStream != identity || !s.onlineGameActive || !s->_running) return;
        s->_gameStream = nil; s->_waitingForFull = YES;
        if ([s authenticationError:error]) { s.errorText = error.localizedDescription; [s notify]; return; }
        s.onlineNotice = @"Game connection interrupted. Reconnecting…"; [s notify];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)([s reconnectDelay:error] * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            P64GameSession *current = weakSelf;
            if (current && generation == current->_onlineGeneration && gameGeneration == current->_gameGeneration && !current->_gameStream) [current openGameStream];
        });
    }];
    identity = stream; _gameStream = stream;
}
- (void)handleGameEvent:(NSDictionary *)event {
    NSString *type = P64Text(event[@"type"]);
    NSDictionary *state = event;
    BOOL full = [type isEqualToString:@"gameFull"];
    if (full) {
        NSString *variant = P64Text(P64Dict(event[@"variant"])[@"key"]);
        if (variant.length && ![variant isEqualToString:@"standard"]) { self.errorText = @"This app supports standard chess only."; _waitingForFull = YES; [self notify]; return; }
        NSString *initial = P64Text(event[@"initialFen"]);
        _remoteInitialFen = !initial.length || [initial isEqualToString:@"startpos"] ? P64String(project64::ChessGame().fen()) : initial;
        NSDictionary *white = P64Dict(event[@"white"]), *black = P64Dict(event[@"black"]);
        if ([P64Text(black[@"id"]) isEqualToString:_accountID]) _myColor = @"black";
        else if ([P64Text(white[@"id"]) isEqualToString:_accountID]) _myColor = @"white";
        NSDictionary *them = [_myColor isEqualToString:@"white"] ? black : white;
        if (P64Text(them[@"name"]).length) _opponent = P64Text(them[@"name"]);
        state = P64Dict(event[@"state"]);
    } else if ([type isEqualToString:@"opponentGone"]) { self.onlineNotice = [event[@"gone"] isKindOfClass:NSNumber.class] && [event[@"gone"] boolValue] ? @"Opponent disconnected." : @""; [self notify]; return; }
    else if (![type isEqualToString:@"gameState"]) return;
    if (!_remoteInitialFen.length) return;
    try {
        project64::ChessGame authoritative(_remoteInitialFen.UTF8String);
        NSString *canonicalInitial = P64String(authoritative.fen());
        NSMutableArray *moves = [NSMutableArray array];
        NSArray *tokens = [P64Text(state[@"moves"]) componentsSeparatedByCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
        for (NSString *token in tokens) {
            if (!token.length) continue;
            NSString *normalized = token;
            if (token.length == 4) {
                NSString *from = [token substringToIndex:2], *to = [token substringFromIndex:2];
                char king = P64Piece(P64String(authoritative.fen()), from), rook = P64Piece(P64String(authoritative.fen()), to);
                if ((king == 'K' && rook == 'R') || (king == 'k' && rook == 'r')) normalized = [NSString stringWithFormat:@"%@%c%c", from, [to characterAtIndex:0] > [from characterAtIndex:0] ? 'g' : 'c', [from characterAtIndex:1]];
            }
            if (!authoritative.apply(normalized.UTF8String)) throw std::invalid_argument("Invalid online move list");
            [moves addObject:normalized];
        }
        BOOL firstSnapshot = !_haveAuthoritativeSnapshot;
        BOOL initialChanged = ![_initialFen isEqualToString:canonicalInitial];
        NSUInteger currentPly = _game->history().size();
        BOOL obsoletePending = _pendingMove && ((moves.count > currentPly && ![moves[currentPly] isEqualToString:_pendingMove]) || (_pendingOrigin == P64MoveRemote && moves.count <= currentPly));
        BOOL differentHistory = NO;
        for (NSUInteger i = 0; i < MIN(currentPly, moves.count); ++i) if (_game->history()[i] != [moves[i] UTF8String]) differentHistory = YES;
        BOOL replace = firstSnapshot || initialChanged || obsoletePending || differentHistory;
        BOOL physicallyDifferent = ![self.fen isEqualToString:P64String(authoritative.fen())];
        _remoteInitialFen = canonicalInitial;
        _remoteMoves = moves; _haveAuthoritativeSnapshot = YES; _waitingForFull = NO;
        const auto authoritativeFen = authoritative.fen();
        _remoteTurn = authoritativeFen[authoritativeFen.find(' ') + 1] == 'b' ? @"black" : @"white";
        if (_acceptedAwaitingEcho && moves.count > _awaitingEchoPly && [moves[_awaitingEchoPly] isEqualToString:_acceptedAwaitingEcho]) _acceptedAwaitingEcho = nil;
        if ([state[@"wtime"] isKindOfClass:NSNumber.class]) self.whiteClock = MAX(0, [state[@"wtime"] doubleValue] / 1000.0);
        if ([state[@"btime"] isKindOfClass:NSNumber.class]) self.blackClock = MAX(0, [state[@"btime"] doubleValue] / 1000.0);
        _lastTick = NSProcessInfo.processInfo.systemUptime;
        _remoteStatus = P64Text(state[@"status"]); if (!_remoteStatus.length) _remoteStatus = @"started";
        _winner = P64Text(state[@"winner"]);
        if (replace) {
            [self invalidatePosition];
            _acceptedAwaitingEcho = nil;
            _game = std::make_unique<project64::ChessGame>(std::move(authoritative)); _initialFen = _remoteInitialFen;
            self.lastMoveFrom = _game->lastMove() ? P64String(_game->lastMove()->from) : @"";
            self.lastMoveTo = _game->lastMove() ? P64String(_game->lastMove()->to) : @"";
            if (self.transport != P64TransportMock && physicallyDifferent) self.alignmentRequired = YES;
        } else [self reconcileRemote];
        if (![_remoteStatus isEqualToString:@"started"] && ![_remoteStatus isEqualToString:@"created"]) self.onlinePhase = @"FINISHED";
        [self notify];
    } catch (const std::exception &) { self.errorText = @"Lichess sent an invalid position or move list."; _waitingForFull = YES; [self notify]; }
}
- (void)reconcileRemote {
    if (_halted || self.mode != P64GameModeOnline || !_haveAuthoritativeSnapshot || _waitingForFull || _submitting || _pendingMove || _writing || self.alignmentRequired) return;
    const auto &local = _game->history();
    NSUInteger common = 0;
    while (common < local.size() && common < _remoteMoves.count && local[common] == [_remoteMoves[common] UTF8String]) ++common;
    if (common == local.size() && common == _remoteMoves.count) return;
    // A request can be acknowledged before its stream echo arrives.
    if (common == _remoteMoves.count && local.size() == common + 1 && _acceptedAwaitingEcho && _awaitingEchoPly == common && [P64String(local.back()) isEqualToString:_acceptedAwaitingEcho]) return;
    if (common == local.size() && _remoteMoves.count > common) {
        [self queueMove:_remoteMoves[common] origin:P64MoveRemote]; return;
    }
    try {
        auto rebuilt = std::make_unique<project64::ChessGame>(_remoteInitialFen.UTF8String);
        for (NSString *move in _remoteMoves) if (!rebuilt->apply(move.UTF8String)) return;
        [self invalidatePosition]; _game = std::move(rebuilt); _initialFen = _remoteInitialFen;
        self.lastMoveFrom = self.lastMoveTo = @"";
        self.alignmentRequired = self.transport != P64TransportMock;
        self.onlineNotice = @"Position resynchronized from Lichess."; [self notify];
    } catch (const std::exception &) { self.errorText = @"Could not resynchronize the online game."; }
}
- (void)seekMinutes:(NSInteger)minutes increment:(NSInteger)increment {
    if (!_lichess || ![self.onlinePhase isEqualToString:@"LOBBY"]) return;
    if (minutes < 10 || minutes > 180 || increment < 0 || increment > 180) { self.errorText = @"Random opponents require a clock of at least 10 minutes."; [self notify]; return; }
    self.onlinePhase = @"SEEKING"; self.onlineNotice = @"Finding an opponent…"; self.errorText = @"";
    [self beginMatchmakingForUsername:nil minutes:minutes increment:increment];
}
- (void)challengeUsername:(NSString *)username minutes:(NSInteger)minutes increment:(NSInteger)increment {
    NSString *name = [username stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if (!_lichess || ![self.onlinePhase isEqualToString:@"LOBBY"] || !name.length) return;
    if (minutes < 1 || minutes > 180 || increment < 0 || increment > 180) { self.errorText = @"Choose a valid time control."; [self notify]; return; }
    self.onlinePhase = @"CHALLENGING"; self.onlineNotice = [NSString stringWithFormat:@"Challenging %@…", name]; self.errorText = @"";
    [self beginMatchmakingForUsername:name minutes:minutes increment:increment];
}
- (void)beginMatchmakingForUsername:(NSString *)username minutes:(NSInteger)minutes increment:(NSInteger)increment {
    const NSUInteger generation = _onlineGeneration;
    __weak P64GameSession *weakSelf = self;
    __block __weak id<P64LichessStream> identity;
    P64LichessStreamCompletion completion = ^(NSError *error) {
        P64GameSession *s = weakSelf;
        if (!s || generation != s->_onlineGeneration || s->_matchmaking != identity) return;
        s->_matchmaking = nil;
        if ([s.onlinePhase isEqualToString:@"SEEKING"] || [s.onlinePhase isEqualToString:@"CHALLENGING"]) {
            s.onlinePhase = @"LOBBY"; s.onlineNotice = @"No match found. You can try again.";
            if (error) s.errorText = error.localizedDescription; [s notify];
        }
    };
    id<P64LichessStream> stream;
    if (username.length) stream = [_lichess challengeUsername:username minutes:minutes increment:increment rated:NO onCreated:^(NSString *identifier) {
        P64GameSession *s = weakSelf; if (s && generation == s->_onlineGeneration && [s.onlinePhase isEqualToString:@"CHALLENGING"]) s->_outgoingChallenge = identifier;
    } completion:completion];
    else stream = [_lichess seekMinutes:minutes increment:increment rated:NO completion:completion];
    identity = stream; _matchmaking = stream; [self notify];
}
- (void)cancelMatchmaking {
    id<P64LichessStream> stream = _matchmaking; _matchmaking = nil;
    [stream cancel];
    NSString *identifier = _outgoingChallenge; _outgoingChallenge = nil;
    if (identifier.length) [_lichess cancelChallenge:identifier completion:^(NSDictionary *response, NSError *error) {}];
    if ([self.onlinePhase isEqualToString:@"SEEKING"] || [self.onlinePhase isEqualToString:@"CHALLENGING"]) self.onlinePhase = @"LOBBY";
    self.onlineNotice = @""; [self notify];
}
- (void)answerChallenge:(NSString *)challengeID accept:(BOOL)accept {
    if (!_lichess || ![self.onlinePhase isEqualToString:@"LOBBY"]) return;
    const NSUInteger generation = _onlineGeneration;
    __weak P64GameSession *weakSelf = self;
    P64LichessCompletion completion = ^(NSDictionary *response, NSError *error) {
        P64GameSession *s = weakSelf; if (!s || generation != s->_onlineGeneration) return;
        if (error) s.errorText = error.localizedDescription;
        else {
            NSMutableArray *list = [NSMutableArray array];
            for (NSDictionary *item in s.incomingChallenges) if (![item[@"id"] isEqualToString:challengeID]) [list addObject:item];
            s.incomingChallenges = list;
        }
        [s notify];
    };
    if (accept) [_lichess acceptChallenge:challengeID completion:completion]; else [_lichess declineChallenge:challengeID completion:completion];
}
- (void)resignOrAbort {
    if (!_lichess || !self.onlineGameActive) return;
    const NSUInteger generation = _onlineGeneration, gameGeneration = _gameGeneration;
    __weak P64GameSession *weakSelf = self;
    P64LichessCompletion completion = ^(NSDictionary *response, NSError *error) {
        P64GameSession *s = weakSelf; if (!s || generation != s->_onlineGeneration || gameGeneration != s->_gameGeneration) return;
        if (error) s.errorText = error.localizedDescription; [s notify];
    };
    if (_remoteMoves.count < 2) [_lichess abortGame:_gameID completion:completion]; else [_lichess resignGame:_gameID completion:completion];
}
- (void)leaveOnlineGame {
    if (self.onlineGameActive) return;
    ++_gameGeneration; [_gameStream cancel]; _gameStream = nil;
    _gameID = @""; _remoteMoves = @[]; _haveAuthoritativeSnapshot = NO;
    self.onlinePhase = _lichess ? @"LOBBY" : @"SIGNED_OUT"; [self resetGame]; [self notify];
}
@end
