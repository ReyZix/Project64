#import "../../Interfaces/P64HardwareBridge.h"
#import <stdint.h>
NS_ASSUME_NONNULL_BEGIN
FOUNDATION_EXPORT NSString * _Nullable P64DecodeBoardPayload(NSData *payload);
FOUNDATION_EXPORT BOOL P64OccupancyFromPayload(NSData *payload, uint64_t * _Nullable value);
@interface P64HardwareBase : NSObject <P64HardwareBridge>
@property(nonatomic, readonly) P64ConnectionStatus connectionStatus;
@property(nonatomic, copy, nullable) void (^boardUpdate)(NSData *);
@property(nonatomic, copy, nullable) void (^gantryUpdate)(P64GantryStatus);
@property(nonatomic, copy, nullable) void (^connectionUpdate)(P64ConnectionStatus);
@end
@interface P64BLEHardwareBridge : P64HardwareBase
@end
@interface P64WiFiHardwareBridge : P64HardwareBase
@end
@interface P64MockHardwareBridge : P64HardwareBase
- (void)simulateBoardPayload:(NSData *)payload;
- (void)simulateFault;
@end
NS_ASSUME_NONNULL_END
