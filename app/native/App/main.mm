#import <UIKit/UIKit.h>
#import "../Interfaces/P64HardwareBridge.h"
#include "ChessGame.hpp"
#include "StockfishEngine.hpp"
#include <memory>

// A small native integration The full chess engine 
@interface P64FoundationController : UIViewController
@end

@implementation P64FoundationController {
    std::unique_ptr<project64::ChessGame> _game;
    std::shared_ptr<project64::StockfishEngine> _engine;
    UILabel *_position;
    UILabel *_message;
    UITextField *_move;
    UIButton *_play;
    UIButton *_computer;
    NSUInteger _revision;
    BOOL _thinking;
}

- (UIButton *)button:(NSString *)title action:(SEL)action {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    [button setTitle:title forState:UIControlStateNormal];
    [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    return button;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.systemBackgroundColor;
    _game = std::make_unique<project64::ChessGame>();
    UILabel *title = [UILabel new];
    title.text = @"Project64 · Native foundation";
    title.font = [UIFont preferredFontForTextStyle:UIFontTextStyleTitle2];
    UILabel *detail = [UILabel new];
    detail.text = @"Objective-C++ / C++\nChess rules and local Stockfish integration";
    detail.numberOfLines = 0;
    _position = [UILabel new];
    _position.numberOfLines = 0;
    _position.font = [UIFont monospacedSystemFontOfSize:14 weight:UIFontWeightRegular];
    _move = [UITextField new];
    _move.placeholder = @"UCI move, e.g. e2e4 or a7a8q";
    _move.borderStyle = UITextBorderStyleRoundedRect;
    _move.autocapitalizationType = UITextAutocapitalizationTypeNone;
    _move.autocorrectionType = UITextAutocorrectionTypeNo;
    _play = [self button:@"Play move" action:@selector(playMove)];
    _computer = [self button:@"Ask Stockfish to move" action:@selector(computerMove)];
    _message = [UILabel new];
    _message.numberOfLines = 0;
    _message.text = @"Loading native Stockfish…";
    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[
        title, detail, _position, _move, _play, _computer,
        [self button:@"New game / cancel search" action:@selector(newGame)], _message
    ]];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 18;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:24],
        [stack.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-24],
        [stack.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:24]
    ]];
    [self refresh];
    std::string directory(NSBundle.mainBundle.resourcePath.UTF8String);
    __weak P64FoundationController *weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        try {
            auto engine = std::make_shared<project64::StockfishEngine>(directory);
            dispatch_async(dispatch_get_main_queue(), ^{
                P64FoundationController *controller = weakSelf;
                if (!controller) return;
                controller->_engine = engine;
                controller->_message.text = @"Native engine available. White moves first.";
                [controller refresh];
            });
        } catch (const std::exception &error) {
            NSString *message = [NSString stringWithUTF8String:error.what()];
            dispatch_async(dispatch_get_main_queue(), ^{
                P64FoundationController *controller = weakSelf;
                if (controller) controller->_message.text = message;
            });
        }
    });
}

- (void)refresh {
    const auto fen = _game->fen();
    _position.text = [NSString stringWithFormat:@"FEN\n%s\n\nLegal moves: %lu", fen.c_str(), (unsigned long)_game->legalMoves().size()];
    _computer.enabled = !_thinking && _engine && !_game->status().over;
    _play.enabled = !_thinking && !_game->status().over;
}

- (void)playMove {
    NSString *text = [[_move.text ?: @"" stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet] lowercaseString];
    if (_game->apply(text.UTF8String)) {
        ++_revision;
        _message.text = [NSString stringWithFormat:@"Played %@", text];
        _move.text = @"";
        [self.view endEditing:YES];
    } else {
        _message.text = @"Illegal move. The position has not changed.";
    }
    [self refresh];
}

- (void)computerMove {
    if (_thinking || !_engine) return;
    _thinking = YES;
    const NSUInteger revision = _revision;
    const auto initialFen = project64::ChessGame().fen();
    const auto moves = _game->history();
    auto engine = _engine;
    _message.text = @"Stockfish is thinking…";
    [self refresh];
    __weak P64FoundationController *weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        // Use the complete history so Stockfish also sees repetition context.
        auto result = engine->search(initialFen, moves, 12, 1000, 8);
        dispatch_async(dispatch_get_main_queue(), ^{
            P64FoundationController *controller = weakSelf;
            if (!controller || controller->_revision != revision) return;
            controller->_thinking = NO;
            if (result.cancelled) controller->_message.text = @"Search cancelled.";
            else if (!result.error.empty()) controller->_message.text = [NSString stringWithUTF8String:result.error.c_str()];
            else if (controller->_game->apply(result.bestMove)) {
                ++controller->_revision;
                controller->_message.text = [NSString stringWithFormat:@"Stockfish played %s", result.bestMove.c_str()];
            } else controller->_message.text = @"Engine returned no legal move.";
            [controller refresh];
        });
    });
}

- (void)newGame {
    ++_revision;
    if (_engine) _engine->newGame();
    _game = std::make_unique<project64::ChessGame>();
    _thinking = NO;
    _message.text = @"New game. White moves first.";
    [self refresh];
}
@end

@interface P64AppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation P64AppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [P64FoundationController new];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(P64AppDelegate.class));
    }
}
