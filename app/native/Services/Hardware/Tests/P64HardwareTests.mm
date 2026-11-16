#import "../P64Hardware.h"
#include <cstdio>
#include <cstdlib>
#undef assert
#define assert(x) do { if (!(x)) { std::fprintf(stderr, "FAIL: %s at line %d\n", #x, __LINE__); std::abort(); } } while (0)


static bool waitUntil(BOOL (^predicate)(void), double seconds=4) {
    NSDate *end=[NSDate dateWithTimeIntervalSinceNow:seconds];
    while (!predicate() && [end timeIntervalSinceNow]>0)
        [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    return predicate();
}
int main(int argc, const char **argv) { @autoreleasepool {
    uint8_t bytes[] = {1,2,3,4,5,6,7,8};
    NSData *raw=[NSData dataWithBytes:bytes length:8]; uint64_t value=0;
    assert([P64DecodeBoardPayload(raw) isEqualToString:@"0102030405060708"]);
    assert(P64OccupancyFromPayload(raw,&value) && value==0x0807060504030201ULL);
    NSData *hex=[@"0102030405060708" dataUsingEncoding:NSASCIIStringEncoding];
    assert(P64OccupancyFromPayload(hex,&value) && value==0x0807060504030201ULL);
    assert([P64DecodeBoardPayload([@"e7e8q" dataUsingEncoding:NSASCIIStringEncoding]) isEqualToString:@"e7e8q"]);
    assert(!P64DecodeBoardPayload([@"e9e4" dataUsingEncoding:NSASCIIStringEncoding]));
    assert(!P64DecodeBoardPayload([@"010203040506070z" dataUsingEncoding:NSASCIIStringEncoding]));
    

    P64MockHardwareBridge *mock=[P64MockHardwareBridge new];
    __block int calls=0; __block NSError *result=nil; __block P64GantryStatus gantry=P64GantryError;
    mock.gantryUpdate=^(P64GantryStatus s){ gantry=s; };
    [mock connectToTarget:@"" completion:^(NSError *e){ calls++; result=e; }];
    [mock disconnect]; assert(calls==1 && result);
    waitUntil(^{return NO;},0.6); assert(calls==1 && mock.connectionStatus==P64ConnectionDisconnected);
    calls=0; [mock connectToTarget:@"" completion:^(NSError *e){calls++; result=e;}];
    assert(waitUntil(^{return calls==1;})); assert(!result && gantry==P64GantryIdle);
    [mock sendMove:@"e2e4" completion:^(NSError *e){ assert(!e); }]; assert(gantry==P64GantryMoving);
    [mock sendMove:@"e7e5" completion:^(NSError *e){ assert(e); }];
    [mock sendCommand:P64SystemEmergencyStop completion:^(NSError *e){assert(!e);}]; assert(gantry==P64GantryIdle);
    [mock simulateFault]; assert(gantry==P64GantryError);
    [mock sendCommand:P64SystemHome completion:^(NSError *e){assert(!e);}]; assert(gantry==P64GantryHoming);
    assert(waitUntil(^{return gantry==P64GantryIdle;},3));
    [mock disconnect];

    if(argc>1) {
        P64WiFiHardwareBridge *wifi=[P64WiFiHardwareBridge new];
        __block int connected=0, board=0, status=0, writes=0; __block NSError *failure=nil;
        wifi.boardUpdate=^(NSData *data){ if([P64DecodeBoardPayload(data) isEqualToString:@"e2e4"]) board++; };
        wifi.gantryUpdate=^(P64GantryStatus s){ if(s==P64GantryIdle) status++; };
        [wifi connectToTarget:[NSString stringWithUTF8String:argv[1]] completion:^(NSError *e){connected++; failure=e;}];
        assert(waitUntil(^{return connected && board && status;},8)); assert(!failure);
        [wifi sendMove:@"e2e4" completion:^(NSError *e){writes++; failure=e;}];
        assert(waitUntil(^{return writes==1;})); assert(!failure);
        [wifi sendCommand:P64SystemReboot completion:^(NSError *e){writes++; failure=e;}];
        assert(waitUntil(^{return writes==2 && wifi.connectionStatus==P64ConnectionDisconnected;},8)); assert(!failure);
        [wifi sendMove:@"e7e5" completion:^(NSError *e){writes++; failure=e;}];
        assert(waitUntil(^{return writes==3;},8)); assert(!failure); [wifi disconnect];
    }
    return 0;
} }
