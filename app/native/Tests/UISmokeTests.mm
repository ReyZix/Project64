#import <UIKit/UIKit.h>
#import <Security/Security.h>
#import "../App/P64GameSession.h"
#import "../App/P64HomeViewController.h"
#import "../Services/Hardware/P64Hardware.h"
#import "../Services/Online/P64TokenStore.h"

// An isolated test service avoids touching a user's saved Lichess credential.
@interface P64IsolatedTokenStore : P64TokenStore
@end
@implementation P64IsolatedTokenStore
+ (NSMutableDictionary *)query {
    return [@{(__bridge id)kSecClass:(__bridge id)kSecClassGenericPassword,
              (__bridge id)kSecAttrService:@"edu.project64.native.ui-tests",
              (__bridge id)kSecAttrAccount:@"temporary-fixture-token"} mutableCopy];
}
@end
static void Check(BOOL ok, const char *message) {
    if (!ok) { fprintf(stderr, "UI TEST FAILED: %s\n", message); fflush(stderr); exit(1); }
}
static UIView *Find(UIView *root, NSString *identifier) {
    if ([root.accessibilityIdentifier isEqualToString:identifier]) return root;
    for (UIView *child in root.subviews) { UIView *found = Find(child, identifier); if (found) return found; }
    return nil;
}
static void Press(UIView *root, NSString *identifier) {
    UIControl *control = (UIControl *)Find(root, identifier);
    Check([control isKindOfClass:UIControl.class] && control.enabled, identifier.UTF8String);
    [control sendActionsForControlEvents:UIControlEventTouchUpInside];
}
@interface UISmokeDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@property(nonatomic, strong) P64GameSession *session;
@property(nonatomic, strong) UINavigationController *navigation;
@end
@implementation UISmokeDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    self.session = [[P64GameSession alloc] initWithBridge:[P64MockHardwareBridge new]
        engineDirectory:NSBundle.mainBundle.resourcePath
        lichessBaseURL:[NSURL URLWithString:@"http://127.0.0.1:1"]];
    [self.session setGameMode:P64GameModePassAndPlay];
    [self.session start];
    self.navigation = [[UINavigationController alloc] initWithRootViewController:[[P64HomeViewController alloc] initWithSession:self.session]];
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = self.navigation;
    [self.window makeKeyAndVisible];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{ [self exerciseHome]; });
    return YES;
}
- (void)exerciseHome {
    NSError *error = nil;
    Check([P64IsolatedTokenStore deleteToken:&error], "isolated Keychain cleanup");
    Check([P64IsolatedTokenStore loadToken:&error] == nil && !error, "empty Keychain load");
    Check([P64IsolatedTokenStore saveToken:@"fixture-token" error:&error], "Keychain save");
    Check([[P64IsolatedTokenStore loadToken:&error] isEqualToString:@"fixture-token"], "Keychain load");
    Check([P64IsolatedTokenStore saveToken:@"updated-fixture" error:&error], "Keychain update");
    Check([[P64IsolatedTokenStore loadToken:&error] isEqualToString:@"updated-fixture"], "Keychain update readback");
    Check([P64IsolatedTokenStore deleteToken:&error], "Keychain delete");
    Press(self.window, @"play-game");
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{ [self exerciseBoard]; });
}
- (void)exerciseBoard {
    Check(self.navigation.viewControllers.count == 2, "home navigation");
    Press(self.window, @"square-e2");
    Check([self.session.selectedSquare isEqualToString:@"e2"] && [self.session.legalTargets containsObject:@"e4"], "board selection and legal destinations");
    Press(self.window, @"square-e4");
    Check([self.session.lastMoveTo isEqualToString:@"e4"] && self.session.inputLocked, "screen move and motion lock");
    Press(self.window, @"emergency-stop");
    Check(self.session.gantry == P64GantryIdle, "emergency stop remains available while locked");
    [self.session newGame];
    [self.session setGameMode:P64GameModeOnline];
    Check(Find(self.window, @"online-token") != nil, "online secure token field");
    UITextField *token = (UITextField *)Find(self.window, @"online-token");
    Check(token.secureTextEntry, "token field must hide credentials");
    Check(!self.session.thinking && self.session.inputLocked, "online lobby cannot play or run engine");
    [self.session setGameMode:P64GameModePassAndPlay];
    [self.navigation popViewControllerAnimated:NO];
    Check(self.navigation.viewControllers.count == 1, "return home");
    [self.session stop];
    puts("Native UIKit and isolated Keychain smoke tests passed"); fflush(stdout); exit(0);
}
@end
int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(UISmokeDelegate.class)); }
}
