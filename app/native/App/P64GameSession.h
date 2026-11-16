#import <Foundation/Foundation.h>
#import "../Interfaces/P64HardwareBridge.h"

NS_ASSUME_NONNULL_BEGIN
typedef NS_ENUM(NSInteger, P64GameMode) { P64GameModePassAndPlay, P64GameModeStockfish, P64GameModeOnline };
typedef NS_ENUM(NSInteger, P64Transport) { P64TransportMock, P64TransportBLE, P64TransportWiFi };

// All methods/properties and notifications belong to the main queue.
// The session alone mutates the game; views render these snapshots.
@interface P64GameSession : NSObject
@property(nonatomic, copy, nullable) void (^onChange)(void);
@property(nonatomic, readonly, copy) NSString *fen;
@property(nonatomic, readonly, copy) NSString *selectedSquare;
@property(nonatomic, readonly, copy) NSArray<NSString *> *legalTargets;
@property(nonatomic, readonly, copy) NSString *lastMoveFrom;
@property(nonatomic, readonly, copy) NSString *lastMoveTo;
@property(nonatomic, readonly, copy) NSString *dangerSquare;
@property(nonatomic, readonly, copy) NSArray<NSString *> *promotionChoices;
@property(nonatomic, readonly, copy) NSArray<NSString *> *inferredMoveChoices;
@property(nonatomic, readonly) BOOL flipped;
@property(nonatomic, readonly) BOOL inputLocked;
@property(nonatomic, readonly) BOOL thinking;
@property(nonatomic, readonly, copy) NSString *statusText;
@property(nonatomic, readonly, copy) NSString *errorText;
@property(nonatomic, readonly) NSTimeInterval whiteClock;
@property(nonatomic, readonly) NSTimeInterval blackClock;
@property(nonatomic, readonly, copy) NSString *whiteName;
@property(nonatomic, readonly, copy) NSString *blackName;
@property(nonatomic, readonly, copy) NSString *turn; // "white" or "black"
@property(nonatomic, readonly) P64GameMode mode;
@property(nonatomic, readonly) P64Transport transport;
@property(nonatomic, copy) NSString *target;
@property(nonatomic) NSInteger skill;
@property(nonatomic, readonly) P64ConnectionStatus connection;
@property(nonatomic, readonly) P64GantryStatus gantry;
@property(nonatomic, readonly, copy) NSString *engineLabel;
@property(nonatomic, readonly, copy) NSString *onlinePhase; // SIGNED_OUT/CHECKING/LOBBY/SEEKING/CHALLENGING/PLAYING/FINISHED
@property(nonatomic, readonly, copy) NSString *username;
@property(nonatomic, readonly, copy) NSString *onlineNotice;
@property(nonatomic, readonly, copy) NSArray<NSDictionary *> *incomingChallenges; // id, username, timeControl
@property(nonatomic, readonly) BOOL onlineGameActive;
@property(nonatomic, readonly) BOOL onlineFinished;
@property(nonatomic, readonly) BOOL alignmentRequired;
- (void)start;
- (void)stop;
- (void)tapSquare:(NSString *)square;
- (void)playUCI:(NSString *)uci;
- (void)choosePromotion:(NSString *)piece;
- (void)chooseInferredMove:(NSString *)uci;
- (void)newGame;
- (void)setGameMode:(P64GameMode)mode;
- (void)setHardwareTransport:(P64Transport)transport;
- (void)connect;
- (void)disconnect;
- (void)sendSystemCommand:(P64SystemCommand)command;
- (void)retry;
- (void)loginWithToken:(NSString *)token;
- (void)logout;
- (void)seekMinutes:(NSInteger)minutes increment:(NSInteger)increment;
- (void)challengeUsername:(NSString *)username minutes:(NSInteger)minutes increment:(NSInteger)increment;
- (void)cancelMatchmaking;
- (void)answerChallenge:(NSString *)challengeID accept:(BOOL)accept;
- (void)resignOrAbort;
- (void)leaveOnlineGame;
- (void)confirmBoardAlignment;
// Dependency injection for native integration tests; no tokens in snapshots.
- (instancetype)initWithBridge:(id<P64HardwareBridge>)bridge
              engineDirectory:(NSString *)directory
                lichessBaseURL:(NSURL *)baseURL;
@end
NS_ASSUME_NONNULL_END
