#import <Foundation/Foundation.h>
#import "../P64LichessService.h"

static void Require(BOOL condition, NSString *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message.UTF8String); exit(1); }
}
static void Wait(BOOL (^done)(void), NSTimeInterval timeout) {
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:timeout];
    while (!done() && deadline.timeIntervalSinceNow > 0)
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    Require(done(), @"timed out waiting for callback");
}
static void CheckStatus(NSURL *base, NSString *token, NSInteger status) {
    P64LichessService *service = [[P64LichessService alloc] initWithToken:token baseURL:base];
    __block NSInteger count = 0;
    [service account:^(NSDictionary *response, NSError *error) {
        Require(NSThread.isMainThread, @"account callback must run on main");
        Require(!response && error.code == status, @"incorrect account HTTP status");
        count++;
    }];
    Wait(^BOOL{ return count == 1; }, 5);
    if (status == 429) {
        __block BOOL returned = NO, completed = NO;
        __block id<P64LichessStream> handle = nil;
        handle = [service streamEvents:^(NSDictionary *event) { Require(NO, @"unexpected rate-limited event"); }
                              completion:^(NSError *error) {
            Require(returned && handle != nil, @"stream completion ran before handle returned");
            Require(error.code == 429, @"stream did not share request cooldown");
            completed = YES;
        }];
        returned = YES;
        Require(!completed, @"rate-limit completion must be asynchronous");
        Wait(^BOOL{ return completed; }, 5);
    }
    [service invalidate];
}
int main(int argc, const char *argv[]) {
    @autoreleasepool {
        Require(argc == 2, @"expected local server base URL argument");
        NSURL *base = [NSURL URLWithString:[NSString stringWithUTF8String:argv[1]]];
        P64LichessService *service = [[P64LichessService alloc] initWithToken:@"normal" baseURL:base];
        __block NSMutableArray *events = [NSMutableArray array];
        __block NSInteger completed = 0;
        [service streamGame:@"ndjson" event:^(NSDictionary *event) {
            Require(NSThread.isMainThread, @"stream event must run on main");
            [events addObject:event];
        } completion:^(NSError *error) {
            Require(NSThread.isMainThread && !error, @"stream completion failed");
            completed++;
        }];
        Wait(^BOOL{ return completed == 1; }, 5);
        Require(events.count == 2 && [events[0][@"text"] isEqual:@"café"] && [events[1][@"final"] boolValue], @"NDJSON framing or final line failed");
        __block NSInteger canceled = 0, afterCancel = 0;
        id<P64LichessStream> stream = [service streamGame:@"slow" event:^(NSDictionary *event) { afterCancel++; } completion:^(NSError *error) { Require(!error, @"cancel error"); canceled++; }];
        [stream cancel]; [stream cancel];
        Wait(^BOOL{ return canceled == 1; }, 5);
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
        Require(canceled == 1 && afterCancel == 0, @"cancel delivered stale callbacks");
        [service invalidate];
        CheckStatus(base, @"bad", 401); CheckStatus(base, @"error", 500); CheckStatus(base, @"limited", 429);
        P64LichessService *serial = [[P64LichessService alloc] initWithToken:@"slow" baseURL:base];
        __block NSInteger responses = 0;
        for (NSInteger i = 0; i < 3; i++) [serial account:^(NSDictionary *response, NSError *error) { Require(!error, @"serial account failed"); responses++; }];
        Wait(^BOOL{ return responses == 3; }, 10);
        [serial invalidate];
        NSURL *statsURL = [NSURL URLWithString:@"/test/stats" relativeToURL:base];
        __block BOOL statsDone = NO;
        [[[NSURLSession sharedSession] dataTaskWithURL:statsURL completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
            NSDictionary *stats = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
            Require(!error && [stats[@"maxConcurrent"] integerValue] == 1, @"short requests overlapped");
            dispatch_async(dispatch_get_main_queue(), ^{ statsDone = YES; });
        }] resume];
        Wait(^BOOL{ return statsDone; }, 5);
        puts("P64LichessService tests passed");
    }
    return 0;
}
