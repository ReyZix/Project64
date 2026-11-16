#import "P64TokenStore.h"
#import <Security/Security.h>

@implementation P64TokenStore
+ (NSMutableDictionary *)query {
    return [@{(__bridge id)kSecClass:(__bridge id)kSecClassGenericPassword,
              (__bridge id)kSecAttrService:@"org.project64.lichess",
              (__bridge id)kSecAttrAccount:@"board-api-token"} mutableCopy];
}
+ (BOOL)check:(OSStatus)status error:(NSError **)error {
    if (status == errSecSuccess) return YES;
    if (error) *error = [NSError errorWithDomain:NSOSStatusErrorDomain code:status userInfo:nil];
    return NO;
}
+ (NSString *)loadToken:(NSError **)error {
    if (error) *error = nil;
    NSMutableDictionary *query = [self query];
    query[(__bridge id)kSecReturnData] = @YES;
    query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
    CFTypeRef result = NULL;
    OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
    if (status == errSecItemNotFound) return nil;
    if (![self check:status error:error]) return nil;
    NSData *data = CFBridgingRelease(result);
    NSString *token = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    if (!token && error) *error = [NSError errorWithDomain:NSCocoaErrorDomain code:NSFileReadInapplicableStringEncodingError userInfo:nil];
    return token;
}
+ (BOOL)saveToken:(NSString *)token error:(NSError **)error {
    if (error) *error = nil;
    NSMutableDictionary *query = [self query];
    NSData *data = [token dataUsingEncoding:NSUTF8StringEncoding];
    NSDictionary *attributes = @{(__bridge id)kSecValueData:data, (__bridge id)kSecAttrAccessible:(__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly};
    OSStatus status = SecItemUpdate((__bridge CFDictionaryRef)query, (__bridge CFDictionaryRef)attributes);
    if (status == errSecItemNotFound) { [query addEntriesFromDictionary:attributes]; status = SecItemAdd((__bridge CFDictionaryRef)query, NULL); }
    return [self check:status error:error];
}
+ (BOOL)deleteToken:(NSError **)error {
    if (error) *error = nil;
    OSStatus status = SecItemDelete((__bridge CFDictionaryRef)[self query]);
    return status == errSecItemNotFound || [self check:status error:error];
}
@end
