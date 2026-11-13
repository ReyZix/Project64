#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN
typedef NS_ENUM(NSInteger, P64ConnectionStatus) {
    P64ConnectionDisconnected, P64ConnectionScanning, P64ConnectionConnecting, P64ConnectionConnected
};
// Values are the ESP32 wire protocol. Unknown values must become Error.
typedef NS_ENUM(uint8_t, P64GantryStatus) {
    P64GantryIdle = 0, P64GantryMoving = 1, P64GantryHoming = 2, P64GantryError = 3
};
typedef NS_ENUM(uint8_t, P64SystemCommand) {
    P64SystemHome = 1, P64SystemEmergencyStop = 2, P64SystemReboot = 3
};

// Contract for the next phase; no transport implementation is included yet.
// Deliver callbacks on the main queue. Complete writes only when accepted by
// the transport; this does not mean physical movement has finished.
@protocol P64HardwareBridge <NSObject>
@property(nonatomic, readonly) P64ConnectionStatus connectionStatus;
@property(nonatomic, copy, nullable) void (^boardUpdate)(NSData *payload);
@property(nonatomic, copy, nullable) void (^gantryUpdate)(P64GantryStatus status);
@property(nonatomic, copy, nullable) void (^connectionUpdate)(P64ConnectionStatus status);
- (void)connectToTarget:(NSString *)target completion:(void (^)(NSError * _Nullable error))completion;
- (void)disconnect;
- (void)sendMove:(NSString *)uci completion:(void (^)(NSError * _Nullable error))completion;
- (void)sendCommand:(P64SystemCommand)command completion:(void (^)(NSError * _Nullable error))completion;
@end
NS_ASSUME_NONNULL_END
