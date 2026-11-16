#import <Foundation/Foundation.h>
#import "../App/P64GameSession.h"

@interface P64GameSession (TestEvents)
- (void)handleAccountEvent:(NSDictionary *)event;
- (void)handleGameEvent:(NSDictionary *)event;
@end

static void Check(BOOL ok, NSString *message) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message.UTF8String); exit(1); }
}
static void Wait(BOOL (^condition)(void), NSString *message, NSTimeInterval timeout = 5) {
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:timeout];
    while (!condition() && deadline.timeIntervalSinceNow > 0)
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    Check(condition(), message);
}
static void Pump(NSTimeInterval duration) {
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:duration];
    while (deadline.timeIntervalSinceNow > 0)
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
}

@interface TestHardwareBridge : NSObject <P64HardwareBridge>
@property(nonatomic) P64ConnectionStatus connectionStatus;
@property(nonatomic, copy) void (^boardUpdate)(NSData *);
@property(nonatomic, copy) void (^gantryUpdate)(P64GantryStatus);
@property(nonatomic, copy) void (^connectionUpdate)(P64ConnectionStatus);
@property(nonatomic, strong) NSMutableArray<NSString *> *sentMoves;
@property(nonatomic) BOOL failNext;
@property(nonatomic) BOOL holdWrites;
@property(nonatomic) BOOL suppressMotion;
@property(nonatomic, copy) void (^held)(NSError *);
@property(nonatomic) NSUInteger generation;
- (void)finishHeldWrite;
- (void)emitBoard:(NSString *)uci;
- (void)spontaneousDisconnect;
@end
@implementation TestHardwareBridge
- (instancetype)init { if ((self = [super init])) _sentMoves = [NSMutableArray array]; return self; }
- (void)connectToTarget:(NSString *)target completion:(void (^)(NSError *))completion {
    (void)target; self.connectionStatus = P64ConnectionConnected;
    if (self.connectionUpdate) self.connectionUpdate(self.connectionStatus);
    completion(nil);
}
- (void)disconnect { ++self.generation; self.connectionStatus = P64ConnectionDisconnected; if (self.connectionUpdate) self.connectionUpdate(self.connectionStatus); }
- (void)sendMove:(NSString *)uci completion:(void (^)(NSError *))completion {
    [self.sentMoves addObject:uci];
    if (self.failNext) { self.failNext = NO; completion([NSError errorWithDomain:@"TestHardware" code:1 userInfo:@{NSLocalizedDescriptionKey:@"Write failed"}]); return; }
    if (self.suppressMotion) {
        self.suppressMotion = NO;
        completion(nil);
        NSUInteger generation = self.generation;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            if (generation != self.generation) return;
            if (self.gantryUpdate) self.gantryUpdate(P64GantryMoving);
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 80 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                if (generation == self.generation && self.gantryUpdate) self.gantryUpdate(P64GantryIdle);
            });
        });
        return;
    }
    if (self.gantryUpdate) self.gantryUpdate(P64GantryMoving);
    if (self.holdWrites) { self.held = completion; return; }
    completion(nil);
    NSUInteger generation = self.generation;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        if (generation == self.generation && self.gantryUpdate) self.gantryUpdate(P64GantryIdle);
    });
}
- (void)finishHeldWrite { void (^done)(NSError *) = self.held; self.held = nil; if (done) done(nil); if (self.gantryUpdate) self.gantryUpdate(P64GantryIdle); }
- (void)spontaneousDisconnect { ++self.generation; self.connectionStatus = P64ConnectionDisconnected; if (self.connectionUpdate) self.connectionUpdate(self.connectionStatus); }
- (void)sendCommand:(P64SystemCommand)command completion:(void (^)(NSError *))completion { (void)command; completion(nil); }
- (void)emitBoard:(NSString *)uci { if (self.boardUpdate) self.boardUpdate([uci dataUsingEncoding:NSASCIIStringEncoding]); }
@end

static NSDictionary *State(NSString *moves) { return @{@"type":@"gameState", @"moves":moves, @"status":@"started", @"wtime":@600000, @"btime":@600000}; }
int main(int argc, const char *argv[]) {
    @autoreleasepool {
        Check(argc == 3, @"usage: NativeFlowTests baseURL engineDirectory");
        NSURL *base = [NSURL URLWithString:[NSString stringWithUTF8String:argv[1]]];
        NSString *directory = [NSString stringWithUTF8String:argv[2]];
        TestHardwareBridge *bridge = [TestHardwareBridge new];
        P64GameSession *session = [[P64GameSession alloc] initWithBridge:bridge engineDirectory:directory lichessBaseURL:base];
        [session setGameMode:P64GameModePassAndPlay];
        [session start];
        Check(session.connection == P64ConnectionConnected, @"hardware connect");
        NSString *initial = session.fen;
        [bridge emitBoard:@"e2e5"];
        Check([session.fen isEqual:initial] && bridge.sentMoves.count == 0, @"illegal board move changed position or echoed");
        [bridge emitBoard:@"e2e4"];
        Check(![session.fen isEqual:initial] && bridge.sentMoves.count == 0, @"legal physical move should commit without echo");
        [session newGame];
        bridge.failNext = YES;
        [session playUCI:@"e2e4"];
        Check([session.fen isEqual:initial] && bridge.sentMoves.count == 1 && session.errorText.length, @"failed hardware write committed move");
        [session retry];
        Check(![session.fen isEqual:initial] && bridge.sentMoves.count == 2, @"retry did not commit once");
        Check(session.inputLocked, @"gantry motion should lock input");
        NSString *duringMotion = session.fen;
        [session playUCI:@"e7e5"];
        Check([session.fen isEqual:duringMotion] && bridge.sentMoves.count == 2, @"move accepted while gantry moving");
        Wait(^BOOL{ return !session.inputLocked; }, @"gantry did not become idle");
        [session newGame]; bridge.suppressMotion = YES;
        [session playUCI:@"e2e4"];
        Check(session.gantry == P64GantryIdle && session.inputLocked, @"input unlocked between acknowledgement and motion");
        NSString *beforeEcho = session.fen;
        [bridge emitBoard:@"e7e5"];
        Check([session.fen isEqual:beforeEcho], @"board echo accepted before motion");
        Wait(^BOOL{ return !session.inputLocked; }, @"delayed motion did not unlock");
        [session newGame]; bridge.holdWrites = YES;
        [session playUCI:@"e2e4"];
        Check(bridge.held != nil && [session.fen isEqual:initial], @"held move committed early");
        [session newGame]; [bridge finishHeldWrite]; bridge.holdWrites = NO;
        Check([session.fen isEqual:initial], @"stale acknowledgement committed after reset");
        [session playUCI:@"e2e4"]; Pump(0.1);
        [session newGame]; bridge.holdWrites = YES;
        [session playUCI:@"e2e4"]; [session disconnect]; [bridge finishHeldWrite]; bridge.holdWrites = NO;
        Check([session.fen isEqual:initial], @"stale acknowledgement committed after disconnect");
        [session connect];
        [session newGame]; bridge.holdWrites = YES;
        [session playUCI:@"e2e4"]; [bridge spontaneousDisconnect]; [bridge finishHeldWrite]; bridge.holdWrites = NO;
        Check([session.fen isEqual:initial], @"stale acknowledgement committed after spontaneous disconnect");
        [session connect];
        [session setGameMode:P64GameModeStockfish];
        [bridge emitBoard:@"e2e4"];
        Wait(^BOOL{ return [session.turn isEqual:@"white"] && session.lastMoveFrom.length > 0 && ![session.lastMoveFrom isEqual:@"e2"]; }, @"engine failed to deliver a legal reply", 12);
        Wait(^BOOL{ return session.gantry == P64GantryIdle; }, @"engine motion did not finish");
        [session newGame]; [bridge emitBoard:@"e2e4"];
        [session setValue:@0 forKey:@"blackClock"];
        NSString *flagged = session.fen; NSUInteger beforeFlag = bridge.sentMoves.count;
        Pump(1.2);
        Check([session.fen isEqual:flagged] && bridge.sentMoves.count == beforeFlag, @"engine move delivered after clock flag");
        [session newGame]; [bridge emitBoard:@"e2e4"];
        [session setGameMode:P64GameModePassAndPlay];
        NSString *afterSwitch = session.fen; Pump(1.5);
        Check([session.fen isEqual:afterSwitch], @"engine search changed position after mode switch");
        [session setGameMode:P64GameModeOnline];
        [session loginWithToken:@"test-token"];
        Wait(^BOOL{ return [session.onlinePhase isEqual:@"LOBBY"]; }, @"online login failed");
        [session handleAccountEvent:@{@"type":@"gameStart", @"game":@{@"gameId":@"g1", @"color":@"white"}}];
        Wait(^BOOL{ return [session.onlinePhase isEqual:@"PLAYING"] && !session.inputLocked; }, @"gameFull did not unlock game");
        initial = session.fen; NSUInteger before = bridge.sentMoves.count;
        [session playUCI:@"d2d4"];
        Wait(^BOOL{ return session.errorText.length > 0; }, @"rejected online move did not report error");
        Check([session.fen isEqual:initial] && bridge.sentMoves.count == before, @"rejected online move reached board");
        [session playUCI:@"e2e4"];
        Wait(^BOOL{ return ![session.fen isEqual:initial]; }, @"accepted online move was not delivered");
        Wait(^BOOL{ return session.gantry == P64GantryIdle; }, @"board did not stop");
        bridge.failNext = YES;
        NSString *afterLocal = session.fen;
        NSDictionary *state = State(@"e2e4 e7e5");
        [session handleGameEvent:state]; [session handleGameEvent:state];
        Check(session.errorText.length && bridge.sentMoves.count == before + 2, @"remote move failure or deduplication");
        Check([session.fen isEqual:afterLocal], @"failed remote move changed game");
        [session retry];
        Wait(^BOOL{ return [session.turn isEqual:@"white"]; }, @"remote retry did not commit");
        bridge.suppressMotion = YES;
        NSUInteger beforeCatchup = bridge.sentMoves.count;
        [session handleGameEvent:State(@"e2e4 e7e5 g1f3 b8c6")];
        Pump(0.05);
        Check(bridge.sentMoves.count == beforeCatchup + 1 && session.inputLocked, @"catch-up overlapped before motion");
        Wait(^BOOL{ return [session.lastMoveFrom isEqual:@"b8"] && [session.lastMoveTo isEqual:@"c6"]; }, @"ordered remote catch-up failed");
        Check(!session.thinking, @"engine ran during online play");
        NSArray *tail = [bridge.sentMoves subarrayWithRange:NSMakeRange(bridge.sentMoves.count - 2, 2)];
        Check([tail isEqual:@[@"g1f3", @"b8c6"]], @"catch-up writes were not ordered");
        Wait(^BOOL{ return session.gantry == P64GantryIdle; }, @"catch-up motion did not finish");
        bridge.failNext = YES;
        [session handleGameEvent:State(@"e2e4 e7e5 g1f3 b8c6 f1b5")];
        NSUInteger beforeStop = bridge.sentMoves.count;
        [session sendSystemCommand:P64SystemEmergencyStop];
        [session handleGameEvent:State(@"e2e4 e7e5 g1f3 b8c6 f1b5")];
        Check(bridge.sentMoves.count == beforeStop && session.inputLocked, @"emergency stop automatically replayed a pending online move");
        [session retry];
        Check(bridge.sentMoves.count == beforeStop + 1, @"explicit retry did not resume stopped delivery");
        Wait(^BOOL{ return session.gantry == P64GantryIdle; }, @"resumed motion did not finish");
        [session handleAccountEvent:@{@"type":@"gameStart", @"game":@{@"gameId":@"g3", @"color":@"white"}}];
        [session handleGameEvent:@{@"type":@"gameFull", @"initialFen":@"startpos", @"white":@{@"id":@"tester"}, @"state":State(@"e2e4")}];
        bridge.holdWrites = YES;
        [session handleGameEvent:State(@"e2e4 e7e5")];
        Check(bridge.held != nil, @"remote write was not held");
        [session handleGameEvent:State(@"e2e4 c7c5")];
        NSString *diverged = session.fen;
        [bridge finishHeldWrite]; bridge.holdWrites = NO;
        Check([session.fen isEqual:diverged] && [session.fen containsString:@"2p5"], @"stale remote acknowledgement overwrote authoritative position");
        [session handleAccountEvent:@{@"type":@"gameStart", @"game":@{@"gameId":@"g2", @"color":@"white"}}];
        [session handleGameEvent:@{@"type":@"gameFull", @"initialFen":@"7k/P7/8/8/8/8/8/7K w - - 0 1", @"variant":@{@"key":@"standard"}, @"white":@{@"id":@"tester"}, @"black":@{@"id":@"opponent"}, @"state":State(@"")}];
        Check(!session.inputLocked, @"custom position did not load");
        NSString *customFen = session.fen;
        [session handleGameEvent:@{@"type":@"gameFull", @"initialFen":@"7k/1P6/8/8/8/8/8/7K w - - 0 1", @"variant":@{@"key":@"standard"}, @"white":@{@"id":@"tester"}, @"state":State(@"")}];
        Check(![session.fen isEqual:customFen] && [session.fen hasPrefix:@"7k/1P6"], @"changed initial FEN was not rebuilt");
        [session handleGameEvent:@{@"type":@"gameFull", @"initialFen":@"7k/P7/8/8/8/8/8/7K w - - 0 1", @"variant":@{@"key":@"standard"}, @"white":@{@"id":@"tester"}, @"state":State(@"")}];
        NSString *beforeInvalid = session.fen;
        [session handleGameEvent:@{@"type":@"gameFull", @"initialFen":@"startpos", @"variant":@{@"key":@"atomic"}, @"state":State(@"")}];
        Check([session.fen isEqual:beforeInvalid] && session.errorText.length && session.inputLocked, @"unsupported variant changed position or did not lock");
        [session handleGameEvent:@{@"type":@"gameFull", @"initialFen":@"7k/P7/8/8/8/8/8/7K w - - 0 1", @"variant":@{@"key":@"standard"}, @"white":@{@"id":@"tester"}, @"state":State(@"")}];
        [session tapSquare:@"a7"]; [session tapSquare:@"a8"];
        Check(session.promotionChoices.count == 4, @"promotion did not require a choice");
        [session tapSquare:@"h1"];
        uint8_t occupancy[8] = {0x80, 0, 0, 0, 0, 0, 0, 0x81};
        if (bridge.boardUpdate) bridge.boardUpdate([NSData dataWithBytes:occupancy length:sizeof occupancy]);
        Check(session.inferredMoveChoices.count == 4, @"ambiguous physical promotion did not require a choice");
        [session stop];
        puts("NativeFlowTests passed");
    }
    return 0;
}
