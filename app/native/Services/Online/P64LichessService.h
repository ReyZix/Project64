#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@protocol P64LichessStream <NSObject>
- (void)cancel;
@end
typedef void (^P64LichessCompletion)(NSDictionary * _Nullable response, NSError * _Nullable error);
typedef void (^P64LichessEvent)(NSDictionary *event);
typedef void (^P64LichessStreamCompletion)(NSError * _Nullable error);
@interface P64LichessService : NSObject
- (instancetype)initWithToken:(NSString *)token baseURL:(NSURL *)baseURL;
- (void)invalidate;
- (void)account:(P64LichessCompletion)completion;
- (void)makeMove:(NSString *)uci gameID:(NSString *)gameID completion:(P64LichessCompletion)completion;
- (void)resignGame:(NSString *)gameID completion:(P64LichessCompletion)completion;
- (void)abortGame:(NSString *)gameID completion:(P64LichessCompletion)completion;
- (void)acceptChallenge:(NSString *)challengeID completion:(P64LichessCompletion)completion;
- (void)declineChallenge:(NSString *)challengeID completion:(P64LichessCompletion)completion;
- (void)cancelChallenge:(NSString *)challengeID completion:(P64LichessCompletion)completion;
- (id<P64LichessStream>)streamEvents:(P64LichessEvent)event completion:(P64LichessStreamCompletion)completion;
- (id<P64LichessStream>)streamGame:(NSString *)gameID event:(P64LichessEvent)event completion:(P64LichessStreamCompletion)completion;
- (id<P64LichessStream>)seekMinutes:(NSInteger)minutes increment:(NSInteger)increment rated:(BOOL)rated completion:(P64LichessStreamCompletion)completion;
- (id<P64LichessStream>)challengeUsername:(NSString *)username minutes:(NSInteger)minutes increment:(NSInteger)increment rated:(BOOL)rated onCreated:(void (^)(NSString *challengeID))created completion:(P64LichessStreamCompletion)completion;
@end
NS_ASSUME_NONNULL_END
