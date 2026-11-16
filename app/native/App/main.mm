#import <UIKit/UIKit.h>
#import "P64GameSession.h"
#import "P64HomeViewController.h"
@interface P64HomeViewController (SmokeLaunch)
- (void)play;
@end

@interface P64AppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@property(nonatomic, strong) P64GameSession *session;
@end
@implementation P64AppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    self.session = [P64GameSession new];
    [self.session start];
    P64HomeViewController *home = [[P64HomeViewController alloc] initWithSession:self.session];
    UINavigationController *navigation = [[UINavigationController alloc] initWithRootViewController:home];
    navigation.navigationBar.tintColor = [UIColor colorWithRed:0.90 green:0.93 blue:0.95 alpha:1];
    UINavigationBarAppearance *appearance = [UINavigationBarAppearance new];
    [appearance configureWithOpaqueBackground];
    appearance.backgroundColor = [UIColor colorWithRed:13/255.0 green:17/255.0 blue:23/255.0 alpha:1];
    appearance.titleTextAttributes = @{NSForegroundColorAttributeName:UIColor.whiteColor};
    navigation.navigationBar.standardAppearance = appearance;
    navigation.navigationBar.scrollEdgeAppearance = appearance;
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = navigation;
    self.window.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    [self.window makeKeyAndVisible];
    // Launch-only UI smoke convenience; no alternate app runtime or mock credentials.
    if ([NSProcessInfo.processInfo.arguments containsObject:@"--p64-show-game"]) {
        dispatch_async(dispatch_get_main_queue(), ^{ [home play]; });
    }
    return YES;
}
- (void)applicationWillTerminate:(UIApplication *)application { [self.session stop]; }
@end
int main(int argc, char *argv[]) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(P64AppDelegate.class)); }
}
