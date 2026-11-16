#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface P64TokenStore : NSObject
+ (nullable NSString *)loadToken:(NSError * _Nullable * _Nullable)error;
+ (BOOL)saveToken:(NSString *)token error:(NSError * _Nullable * _Nullable)error;
+ (BOOL)deleteToken:(NSError * _Nullable * _Nullable)error;
@end
NS_ASSUME_NONNULL_END
