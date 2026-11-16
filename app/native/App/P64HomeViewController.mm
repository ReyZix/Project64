#import "P64HomeViewController.h"
#import "P64GameSession.h"
#import "P64PieceView.h"

static UIColor *C(unsigned rgb) { return [UIColor colorWithRed:((rgb>>16)&255)/255.0 green:((rgb>>8)&255)/255.0 blue:(rgb&255)/255.0 alpha:1]; }
static UIColor *BG(void) { return C(0x0d1117); }
static UIColor *FG(void) { return C(0xe6edf3); }
static UIColor *Muted(void) { return [FG() colorWithAlphaComponent:.62]; }
static UILabel *Label(NSString *text, CGFloat size, UIFontWeight weight) { UILabel *l=[UILabel new]; l.text=text; l.textColor=FG(); l.font=[UIFont systemFontOfSize:size weight:weight]; l.numberOfLines=0; return l; }
static UIButton *Button(NSString *title, id target, SEL action) { UIButton *b=[UIButton buttonWithType:UIButtonTypeSystem]; [b setTitle:title forState:UIControlStateNormal]; [b setTitleColor:FG() forState:UIControlStateNormal]; b.backgroundColor=C(0x26313d); b.layer.cornerRadius=9; b.titleLabel.font=[UIFont systemFontOfSize:15 weight:UIFontWeightSemibold]; [b.heightAnchor constraintGreaterThanOrEqualToConstant:44].active=YES; if (target && action) [b addTarget:target action:action forControlEvents:UIControlEventTouchUpInside]; return b; }
static UIStackView *Stack(UILayoutConstraintAxis axis, CGFloat spacing) { UIStackView *s=[UIStackView new]; s.axis=axis; s.spacing=spacing; return s; }
static UITextField *Field(NSString *placeholder) { UITextField *f=[UITextField new]; f.placeholder=placeholder; f.textColor=FG(); f.attributedPlaceholder=[[NSAttributedString alloc] initWithString:placeholder attributes:@{NSForegroundColorAttributeName:Muted()}]; f.backgroundColor=C(0x161b22); f.borderStyle=UITextBorderStyleRoundedRect; f.autocorrectionType=UITextAutocorrectionTypeNo; f.autocapitalizationType=UITextAutocapitalizationTypeNone; return f; }

@interface P64BoardView : UIView
@property(nonatomic, weak) P64GameSession *session;
- (void)refresh;
@end
@implementation P64BoardView {
    NSMutableArray<UIButton *> *_squares;
    NSMutableArray<P64PieceView *> *_pieceViews;
    NSMutableArray<UIView *> *_legalDots;
}
- (instancetype)initWithSession:(P64GameSession *)session {
    if ((self=[super initWithFrame:CGRectZero])) { _session=session; _squares=[NSMutableArray array]; _pieceViews=[NSMutableArray array]; _legalDots=[NSMutableArray array]; self.isAccessibilityElement=NO;
        for (int i=0;i<64;i++) { UIButton *b=[UIButton buttonWithType:UIButtonTypeCustom]; b.tag=i; [b addTarget:self action:@selector(tap:) forControlEvents:UIControlEventTouchUpInside]; P64PieceView *piece=[P64PieceView new]; [b addSubview:piece]; UIView *dot=[UIView new]; dot.backgroundColor=C(0x367a40); dot.userInteractionEnabled=NO; dot.isAccessibilityElement=NO; dot.hidden=YES; [b addSubview:dot]; [self addSubview:b]; [_squares addObject:b]; [_pieceViews addObject:piece]; [_legalDots addObject:dot]; }
    } return self;
}
- (void)layoutSubviews { [super layoutSubviews]; CGFloat s=MIN(self.bounds.size.width,self.bounds.size.height)/8; for(int i=0;i<64;i++) { UIButton *b=_squares[i]; b.frame=CGRectMake((i%8)*s,(i/8)*s,s,s); _pieceViews[i].frame=b.bounds; CGFloat diameter=s*.16; UIView *dot=_legalDots[i]; dot.frame=CGRectMake((s-diameter)/2,(s-diameter)/2,diameter,diameter); dot.layer.cornerRadius=diameter/2; } }
- (void)tap:(UIButton *)sender { NSInteger i=sender.tag; int file=self.session.flipped ? 7-(int)(i%8) : (int)(i%8); int rank=self.session.flipped ? (int)(i/8)+1 : 8-(int)(i/8); [self.session tapSquare:[NSString stringWithFormat:@"%c%d",'a'+file,rank]]; }
- (void)refresh {
    NSString *placement=[[self.session.fen componentsSeparatedByString:@" "] firstObject] ?: @""; NSArray *rows=[placement componentsSeparatedByString:@"/"]; NSMutableDictionary *pieces=[NSMutableDictionary dictionary];
    for (int r=0;r<MIN(8,(int)rows.count);r++) { NSString *row=rows[r]; int f=0; for(NSUInteger j=0;j<row.length && f<8;j++) { unichar ch=[row characterAtIndex:j]; if(ch>='1'&&ch<='8') f+=ch-'0'; else { pieces[[NSString stringWithFormat:@"%c%d",'a'+f,8-r]]=[NSString stringWithCharacters:&ch length:1]; f++; } } }
    NSDictionary *names=@{@"k":@"king",@"q":@"queen",@"r":@"rook",@"b":@"bishop",@"n":@"knight",@"p":@"pawn"};
    for(int i=0;i<64;i++) { int file=self.session.flipped?7-i%8:i%8, rank=self.session.flipped?i/8+1:8-i/8; NSString *sq=[NSString stringWithFormat:@"%c%d",'a'+file,rank]; UIButton *b=_squares[i]; NSString *p=pieces[sq]; BOOL light=((file+rank)%2==0); UIColor *base=light?C(0xe2dcd2):C(0x38404b);
        if ([sq isEqualToString:self.session.lastMoveFrom] || [sq isEqualToString:self.session.lastMoveTo]) base=light?C(0xc8ce93):C(0x667a51);
        if ([sq isEqualToString:self.session.selectedSquare]) base=C(0x7fa75f);
        if ([sq isEqualToString:self.session.dangerSquare]) base=C(0xc65b55);
        b.backgroundColor=base; BOOL legal=[self.session.legalTargets containsObject:sq]; _pieceViews[i].piece=p; _legalDots[i].hidden=!(legal && !p); BOOL white=p && [p isEqualToString:p.uppercaseString];
        NSString *desc=p?[NSString stringWithFormat:@"%@ %@",white?@"white":@"black",names[p.lowercaseString] ?: @"piece"]:@"empty"; b.accessibilityIdentifier=[@"square-" stringByAppendingString:sq]; b.accessibilityLabel=[NSString stringWithFormat:@"%@, %@%@",sq,desc,legal?@", legal destination":@""]; b.accessibilityHint=@"Double tap to select or move"; b.enabled=!self.session.inputLocked;
    }
}
@end

@interface P64GameViewController : UIViewController <UITextFieldDelegate>
- (instancetype)initWithSession:(P64GameSession *)session;
@end

@implementation P64HomeViewController { P64GameSession *_session; }
- (instancetype)initWithSession:(P64GameSession *)session { if((self=[super init])) _session=session; return self; }
- (void)viewDidLoad { [super viewDidLoad]; self.view.backgroundColor=BG(); self.title=@"Project 64"; self.navigationItem.backBarButtonItem=[[UIBarButtonItem alloc] initWithTitle:@"Home" style:UIBarButtonItemStylePlain target:nil action:nil]; self.navigationItem.backBarButtonItem.accessibilityIdentifier=@"home";
    UILabel *title=Label(@"PROJECT 64",38,UIFontWeightBold); title.textAlignment=NSTextAlignmentCenter; UILabel *subtitle=Label(@"Autonomous chessboard",17,UIFontWeightRegular); subtitle.textColor=Muted(); subtitle.textAlignment=NSTextAlignmentCenter; UIButton *play=Button(@"Play Game",self,@selector(play)); play.backgroundColor=C(0x347a48); play.accessibilityIdentifier=@"play-game";
    UIStackView *s=Stack(UILayoutConstraintAxisVertical,16); s.translatesAutoresizingMaskIntoConstraints=NO; [s addArrangedSubview:title]; [s addArrangedSubview:subtitle]; [s setCustomSpacing:38 afterView:subtitle]; [s addArrangedSubview:play]; [self.view addSubview:s]; [NSLayoutConstraint activateConstraints:@[[s.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],[s.centerYAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.centerYAnchor],[s.widthAnchor constraintLessThanOrEqualToConstant:400],[s.widthAnchor constraintLessThanOrEqualToAnchor:self.view.widthAnchor multiplier:.8]]]; NSLayoutConstraint *preferred=[s.widthAnchor constraintEqualToAnchor:self.view.widthAnchor multiplier:.8]; preferred.priority=UILayoutPriorityDefaultHigh; preferred.active=YES;
}
- (void)play { [self.navigationController pushViewController:[[P64GameViewController alloc] initWithSession:_session] animated:YES]; }
@end

@implementation P64GameViewController {
    P64GameSession *_session; UIScrollView *_scroll; UIStackView *_content; P64BoardView *_board;
    UILabel *_topPlayer, *_bottomPlayer, *_status, *_hardware, *_onlineStatus, *_error;
    UIButton *_connect, *_retry, *_align, *_signOut, *_seek, *_challenge, *_cancel, *_resign, *_lobby;
    UIView *_online; UITextField *_token, *_username; UISegmentedControl *_time; UIStackView *_incoming;
    NSString *_promptKey; __weak UIAlertController *_choiceAlert; UILabel *_seekHint; void (^_changeHandler)(void);
}
- (instancetype)initWithSession:(P64GameSession *)session { if((self=[super init])) _session=session; return self; }
- (void)viewDidLoad { [super viewDidLoad]; self.title=@"Game"; self.view.backgroundColor=BG(); self.navigationItem.rightBarButtonItem=[[UIBarButtonItem alloc] initWithTitle:@"Settings" style:UIBarButtonItemStylePlain target:self action:@selector(settings)]; self.navigationItem.rightBarButtonItem.accessibilityIdentifier=@"settings"; self.navigationItem.backBarButtonItem=[[UIBarButtonItem alloc] initWithTitle:@"Home" style:UIBarButtonItemStylePlain target:nil action:nil];
    _scroll=[UIScrollView new]; _scroll.translatesAutoresizingMaskIntoConstraints=NO; _scroll.keyboardDismissMode=UIScrollViewKeyboardDismissModeInteractive; [self.view addSubview:_scroll]; [NSLayoutConstraint activateConstraints:@[[_scroll.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],[_scroll.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],[_scroll.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],[_scroll.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor]]];
    _content=Stack(UILayoutConstraintAxisVertical,12); _content.translatesAutoresizingMaskIntoConstraints=NO; [_scroll addSubview:_content]; [NSLayoutConstraint activateConstraints:@[[_content.topAnchor constraintEqualToAnchor:_scroll.contentLayoutGuide.topAnchor constant:16],[_content.bottomAnchor constraintEqualToAnchor:_scroll.contentLayoutGuide.bottomAnchor constant:-24],[_content.centerXAnchor constraintEqualToAnchor:_scroll.frameLayoutGuide.centerXAnchor],[_content.widthAnchor constraintLessThanOrEqualToAnchor:_scroll.frameLayoutGuide.widthAnchor constant:-32],[_content.widthAnchor constraintLessThanOrEqualToConstant:600]]];
    NSLayoutConstraint *preferredWidth=[_content.widthAnchor constraintEqualToAnchor:_scroll.frameLayoutGuide.widthAnchor constant:-32]; preferredWidth.priority=UILayoutPriorityDefaultHigh; preferredWidth.active=YES;
    _topPlayer=Label(@"",16,UIFontWeightSemibold); _bottomPlayer=Label(@"",16,UIFontWeightSemibold); _status=Label(@"",15,UIFontWeightRegular); _status.textColor=Muted(); _error=Label(@"",15,UIFontWeightMedium); _error.textColor=C(0xf4736b); _hardware=Label(@"",14,UIFontWeightMedium); _hardware.textColor=Muted();
    [_content addArrangedSubview:_topPlayer]; _board=[[P64BoardView alloc] initWithSession:_session]; [_content addArrangedSubview:_board]; [_board.heightAnchor constraintEqualToAnchor:_board.widthAnchor].active=YES; [_content addArrangedSubview:_bottomPlayer]; [_content addArrangedSubview:_status]; [_content addArrangedSubview:_error]; [_content addArrangedSubview:_hardware];
    UIStackView *links=Stack(UILayoutConstraintAxisHorizontal,8); links.distribution=UIStackViewDistributionFillEqually; _connect=Button(@"Connect",self,@selector(connect)); _connect.accessibilityIdentifier=@"connect"; [links addArrangedSubview:_connect]; [links addArrangedSubview:Button(@"Home gantry",self,@selector(homeGantry))]; [_content addArrangedSubview:links];
    UIStackView *actions=Stack(UILayoutConstraintAxisHorizontal,8); actions.distribution=UIStackViewDistributionFillEqually; UIButton *stop=Button(@"Emergency Stop",self,@selector(emergency)); stop.backgroundColor=C(0x9d302e); stop.accessibilityIdentifier=@"emergency-stop"; [actions addArrangedSubview:stop]; [actions addArrangedSubview:Button(@"New Game",self,@selector(newGame))]; [_content addArrangedSubview:actions];
    _retry=Button(@"Retry",self,@selector(retry)); [_content addArrangedSubview:_retry]; _align=Button(@"Board aligned — continue",self,@selector(align)); [_content addArrangedSubview:_align];
    [self makeOnlinePanel];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(keyboard:) name:UIKeyboardWillChangeFrameNotification object:nil];
}
- (void)makeOnlinePanel { UIStackView *panel=Stack(UILayoutConstraintAxisVertical,8); _online=panel; [_content addArrangedSubview:panel]; [panel addArrangedSubview:Label(@"Online play",20,UIFontWeightBold)]; _onlineStatus=Label(@"",14,UIFontWeightRegular); _onlineStatus.textColor=Muted(); [panel addArrangedSubview:_onlineStatus];
    UILabel *tokenHelp=Label(@"Create a Lichess personal token with Play games using the Board API and Send, accept and reject challenges permissions.",13,UIFontWeightRegular); tokenHelp.tag=101; tokenHelp.textColor=Muted(); [panel addArrangedSubview:tokenHelp]; _token=Field(@"Lichess API token"); _token.accessibilityIdentifier=@"online-token"; _token.secureTextEntry=YES; _token.returnKeyType=UIReturnKeyDone; _token.delegate=self; [panel addArrangedSubview:_token]; UIButton *login=Button(@"Sign in",self,@selector(login)); login.tag=100; [panel addArrangedSubview:login]; _signOut=Button(@"Sign out",self,@selector(logout)); [panel addArrangedSubview:_signOut];
    _time=[[UISegmentedControl alloc] initWithItems:@[@"10 + 0",@"10 + 5",@"15 + 10",@"30 + 0"]]; _time.accessibilityIdentifier=@"time-control"; [_time addTarget:self action:@selector(updateSeekAvailability) forControlEvents:UIControlEventValueChanged]; _time.selectedSegmentIndex=0; _time.selectedSegmentTintColor=C(0x347a48); [_time setTitleTextAttributes:@{NSForegroundColorAttributeName:FG()} forState:UIControlStateNormal]; [panel addArrangedSubview:_time]; _seek=Button(@"Find an opponent",self,@selector(seek)); _seek.accessibilityIdentifier=@"seek"; [panel addArrangedSubview:_seek]; _seekHint=Label(@"Random opponents require at least 10 minutes.",13,UIFontWeightRegular); _seekHint.textColor=Muted(); [panel addArrangedSubview:_seekHint]; _username=Field(@"Lichess username"); _username.delegate=self; [panel addArrangedSubview:_username]; _challenge=Button(@"Challenge player",self,@selector(challenge)); [panel addArrangedSubview:_challenge]; _incoming=Stack(UILayoutConstraintAxisVertical,8); [panel addArrangedSubview:_incoming]; _cancel=Button(@"Cancel search or challenge",self,@selector(cancel)); [panel addArrangedSubview:_cancel]; _resign=Button(@"Resign or abort",self,@selector(resign)); [panel addArrangedSubview:_resign]; _lobby=Button(@"Back to lobby",self,@selector(lobby)); [panel addArrangedSubview:_lobby];
}
- (void)viewWillAppear:(BOOL)animated { [super viewWillAppear:animated]; __weak __typeof__(self) weakSelf=self; _changeHandler=^{ [weakSelf refresh]; }; _session.onChange=_changeHandler; [self refresh]; }
- (void)viewDidAppear:(BOOL)animated { [super viewDidAppear:animated]; [self refresh]; }
- (void)viewDidDisappear:(BOOL)animated { [super viewDidDisappear:animated]; if (self.isMovingFromParentViewController && _session.onChange==_changeHandler) _session.onChange=nil; }
- (void)dealloc { [[NSNotificationCenter defaultCenter] removeObserver:self]; if (_session.onChange==_changeHandler) _session.onChange=nil; }
- (NSString *)clock:(NSTimeInterval)t { NSInteger n=MAX(0,(NSInteger)ceil(t)); return [NSString stringWithFormat:@"%02ld:%02ld",(long)(n/60),(long)(n%60)]; }
- (void)refresh { if(!self.isViewLoaded) return; [_board refresh]; BOOL flip=_session.flipped; NSString *white=[NSString stringWithFormat:@"%@  ·  %@%@",_session.whiteName,[self clock:_session.whiteClock],[_session.turn isEqualToString:@"white"]?@"  ●":@""]; NSString *black=[NSString stringWithFormat:@"%@  ·  %@%@",_session.blackName,[self clock:_session.blackClock],[_session.turn isEqualToString:@"black"]?@"  ●":@""]; _topPlayer.text=flip?white:black; _bottomPlayer.text=flip?black:white; _status.text=[NSString stringWithFormat:@"%@%@",_session.statusText ?: @"",_session.thinking?@"  ·  Thinking…":@""]; _error.text=_session.errorText; _error.hidden=!_session.errorText.length;
    NSArray *connections=@[@"Disconnected",@"Scanning",@"Connecting",@"Connected"]; NSArray *gantries=@[@"Idle",@"Moving",@"Homing",@"Error"]; NSInteger c=MIN(MAX(_session.connection,0),3), g=MIN(MAX(_session.gantry,0),3); _hardware.text=[NSString stringWithFormat:@"Board: %@  ·  Gantry: %@  ·  %@",connections[c],gantries[g],_session.engineLabel ?: @""]; [_connect setTitle:c==P64ConnectionDisconnected?@"Connect":@"Disconnect" forState:UIControlStateNormal]; _retry.hidden=!_session.errorText.length; _align.hidden=!_session.alignmentRequired;
    if (_session.mode!=P64GameModeOnline) _token.text=@""; _online.hidden=_session.mode!=P64GameModeOnline; NSString *phase=_session.onlinePhase; BOOL signedOut=[phase isEqualToString:@"SIGNED_OUT"] || [phase isEqualToString:@"CHECKING"]; BOOL lobby=[phase isEqualToString:@"LOBBY"]; BOOL searching=[phase isEqualToString:@"SEEKING"]||[phase isEqualToString:@"CHALLENGING"]; BOOL playing=_session.onlineGameActive; _onlineStatus.text=[NSString stringWithFormat:@"%@%@%@",_session.username.length?_session.username:@"",_session.username.length?@" · ":@"",_session.onlineNotice.length?_session.onlineNotice:phase]; _token.hidden=!signedOut; [_online viewWithTag:101].hidden=!signedOut; UIControl *signIn=(UIControl *)[_online viewWithTag:100]; signIn.hidden=!signedOut; signIn.enabled=![phase isEqualToString:@"CHECKING"]; _token.enabled=signIn.enabled; _signOut.hidden=signedOut; _time.hidden=!lobby; _seek.hidden=!lobby; [self updateSeekAvailability]; _seekHint.hidden=!lobby || [self minutes]>=10; _username.hidden=!lobby; _challenge.hidden=!lobby; _cancel.hidden=!searching; _resign.hidden=!playing || _session.onlineFinished; _lobby.hidden=!_session.onlineFinished;
    for(UIView *v in _incoming.arrangedSubviews) { [_incoming removeArrangedSubview:v]; [v removeFromSuperview]; } __weak P64GameSession *weakSession=_session; for(NSDictionary *item in _session.incomingChallenges) { NSString *cid=[item[@"id"] description]; if(!cid.length) continue; UILabel *l=Label([NSString stringWithFormat:@"Challenge from %@ · %@",item[@"username"] ?: @"Player",item[@"timeControl"] ?: @""],14,UIFontWeightMedium); [_incoming addArrangedSubview:l]; UIStackView *row=Stack(UILayoutConstraintAxisHorizontal,8); row.distribution=UIStackViewDistributionFillEqually; UIButton *accept=Button(@"Accept",nil,nil), *decline=Button(@"Decline",nil,nil); [accept addAction:[UIAction actionWithHandler:^(__kindof UIAction *a){ [weakSession answerChallenge:cid accept:YES]; }] forControlEvents:UIControlEventTouchUpInside]; [decline addAction:[UIAction actionWithHandler:^(__kindof UIAction *a){ [weakSession answerChallenge:cid accept:NO]; }] forControlEvents:UIControlEventTouchUpInside]; [row addArrangedSubview:accept]; [row addArrangedSubview:decline]; [_incoming addArrangedSubview:row]; }
    [self presentChoicesIfNeeded];
}
- (void)presentChoicesIfNeeded {
    NSArray *choices=_session.promotionChoices.count ? _session.promotionChoices : _session.inferredMoveChoices;
    BOOL promotion=_session.promotionChoices.count>0;
    NSString *key=choices.count ? [NSString stringWithFormat:@"%@:%@",promotion?@"promotion":@"move",[choices componentsJoinedByString:@","]] : nil;
    if (_choiceAlert && ![_promptKey isEqualToString:key]) {
        UIAlertController *stale=_choiceAlert; _choiceAlert=nil; _promptKey=nil;
        __weak __typeof__(self) weakSelf=self;
        [stale dismissViewControllerAnimated:YES completion:^{ [weakSelf presentChoicesIfNeeded]; }];
        return;
    }
    if (!key || _choiceAlert || self.presentedViewController || !self.view.window) return;
    _promptKey=key;
    UIAlertController *alert=[UIAlertController alertControllerWithTitle:promotion?@"Choose promotion":@"Choose the move played" message:promotion?@"Select a piece for your pawn.":@"More than one move matches the physical board." preferredStyle:UIAlertControllerStyleAlert];
    _choiceAlert=alert;
    __weak __typeof__(self) weakSelf=self;
    for (NSString *choice in choices) {
        NSDictionary *titles=@{@"q":@"Queen",@"r":@"Rook",@"b":@"Bishop",@"n":@"Knight"};
        NSString *title=promotion ? (titles[choice.lowercaseString] ?: choice) : choice;
        [alert addAction:[UIAlertAction actionWithTitle:title style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){
            __typeof__(self) self=weakSelf; if (!self || ![self->_promptKey isEqualToString:key]) return;
            self->_choiceAlert=nil; self->_promptKey=nil;
            NSArray *current=promotion ? self->_session.promotionChoices : self->_session.inferredMoveChoices;
            if ([current containsObject:choice]) { if(promotion) [self->_session choosePromotion:choice]; else [self->_session chooseInferredMove:choice]; }
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [weakSelf presentChoicesIfNeeded]; });
        }]];
    }
    [alert addAction:[UIAlertAction actionWithTitle:@"Emergency Stop" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *a){
        __typeof__(self) self=weakSelf; if (!self) return;
        self->_choiceAlert=nil; self->_promptKey=nil;
        [self->_session sendSystemCommand:P64SystemEmergencyStop];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [weakSelf presentChoicesIfNeeded]; });
    }]];
    [self presentViewController:alert animated:YES completion:nil];
}
- (void)connect { if(_session.connection==P64ConnectionDisconnected) [_session connect]; else [_session disconnect]; }
- (void)homeGantry { [_session sendSystemCommand:P64SystemHome]; }
- (void)emergency { [_session sendSystemCommand:P64SystemEmergencyStop]; }
- (void)newGame { [_session newGame]; }
- (void)retry { [_session retry]; }
- (void)align { [_session confirmBoardAlignment]; }
- (void)login { NSString *token=[_token.text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet]; if(token.length) { [_session loginWithToken:token]; _token.text=@""; [_token resignFirstResponder]; } }
- (void)logout { _token.text=@""; [_session logout]; }
- (void)updateSeekAvailability { BOOL available=[self minutes]>=10; _seek.enabled=available; _seek.alpha=available?1:.5; _seekHint.hidden=_seek.hidden || available; }
- (NSInteger)minutes { static NSInteger v[]={10,10,15,30}; return v[MAX(0,MIN(3,_time.selectedSegmentIndex))]; }
- (NSInteger)increment { static NSInteger v[]={0,5,10,0}; return v[MAX(0,MIN(3,_time.selectedSegmentIndex))]; }
- (void)seek { [_session seekMinutes:[self minutes] increment:[self increment]]; }
- (void)challenge { NSString *name=[_username.text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet]; if(name.length) { [_session challengeUsername:name minutes:[self minutes] increment:[self increment]]; [_username resignFirstResponder]; } }
- (void)cancel { [_session cancelMatchmaking]; }
- (void)resign { UIAlertController *a=[UIAlertController alertControllerWithTitle:@"Resign or abort?" message:@"This ends your current online game." preferredStyle:UIAlertControllerStyleAlert]; [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]]; [a addAction:[UIAlertAction actionWithTitle:@"End game" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *action){ [self->_session resignOrAbort]; }]]; [self presentViewController:a animated:YES completion:nil]; }
- (void)lobby { [_session leaveOnlineGame]; }
- (void)settings { UIAlertController *a=[UIAlertController alertControllerWithTitle:@"Settings" message:@"Choose game and board settings." preferredStyle:UIAlertControllerStyleActionSheet]; [a addAction:[UIAlertAction actionWithTitle:@"Game mode" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x){ [self modeSettings]; }]]; [a addAction:[UIAlertAction actionWithTitle:@"Board connection" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x){ [self transportSettings]; }]]; [a addAction:[UIAlertAction actionWithTitle:@"Connection target" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x){ [self targetSettings]; }]]; [a addAction:[UIAlertAction actionWithTitle:@"Engine skill" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x){ [self skillSettings]; }]]; [a addAction:[UIAlertAction actionWithTitle:@"Reboot board" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *x){ [self->_session sendSystemCommand:P64SystemReboot]; }]]; [a addAction:[UIAlertAction actionWithTitle:@"Done" style:UIAlertActionStyleCancel handler:nil]]; a.popoverPresentationController.barButtonItem=self.navigationItem.rightBarButtonItem; [self presentViewController:a animated:YES completion:nil]; }
- (void)modeSettings { UIAlertController *a=[UIAlertController alertControllerWithTitle:@"Game mode" message:nil preferredStyle:UIAlertControllerStyleActionSheet]; NSArray *names=@[@"Pass and play",@"Stockfish",@"Online"]; for(NSInteger i=0;i<names.count;i++) [a addAction:[UIAlertAction actionWithTitle:[NSString stringWithFormat:@"%@%@",names[i],_session.mode==i?@" ✓":@""] style:UIAlertActionStyleDefault handler:^(UIAlertAction *x){ [self->_session setGameMode:(P64GameMode)i]; }]]; [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]]; a.popoverPresentationController.barButtonItem=self.navigationItem.rightBarButtonItem; [self presentViewController:a animated:YES completion:nil]; }
- (void)transportSettings { UIAlertController *a=[UIAlertController alertControllerWithTitle:@"Board connection" message:nil preferredStyle:UIAlertControllerStyleActionSheet]; NSArray *names=@[@"Mock",@"Bluetooth",@"Wi-Fi"]; for(NSInteger i=0;i<names.count;i++) [a addAction:[UIAlertAction actionWithTitle:[NSString stringWithFormat:@"%@%@",names[i],_session.transport==i?@" ✓":@""] style:UIAlertActionStyleDefault handler:^(UIAlertAction *x){ [self->_session setHardwareTransport:(P64Transport)i]; }]]; [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]]; a.popoverPresentationController.barButtonItem=self.navigationItem.rightBarButtonItem; [self presentViewController:a animated:YES completion:nil]; }
- (void)targetSettings { UIAlertController *a=[UIAlertController alertControllerWithTitle:@"Connection target" message:@"Enter the Bluetooth board name or Wi-Fi host." preferredStyle:UIAlertControllerStyleAlert]; [a addTextFieldWithConfigurationHandler:^(UITextField *f){ f.text=self->_session.target; f.autocapitalizationType=UITextAutocapitalizationTypeNone; f.autocorrectionType=UITextAutocorrectionTypeNo; }]; [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]]; [a addAction:[UIAlertAction actionWithTitle:@"Save" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x){ self->_session.target=a.textFields.firstObject.text ?: @""; }]]; [self presentViewController:a animated:YES completion:nil]; }
- (void)skillSettings { UIAlertController *a=[UIAlertController alertControllerWithTitle:@"Engine skill" message:@"Choose a level from 0 to 20." preferredStyle:UIAlertControllerStyleAlert]; [a addTextFieldWithConfigurationHandler:^(UITextField *f){ f.keyboardType=UIKeyboardTypeNumberPad; f.text=[NSString stringWithFormat:@"%ld",(long)self->_session.skill]; }]; [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]]; [a addAction:[UIAlertAction actionWithTitle:@"Save" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x){ self->_session.skill=MAX(0,MIN(20,a.textFields.firstObject.text.integerValue)); }]]; [self presentViewController:a animated:YES completion:nil]; }
- (BOOL)textFieldShouldReturn:(UITextField *)textField { [textField resignFirstResponder]; return YES; }
- (void)keyboard:(NSNotification *)note { CGRect frame=[self.view convertRect:[note.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue] fromView:nil]; CGFloat overlap=MAX(0,CGRectGetMaxY(self.view.bounds)-CGRectGetMinY(frame)); UIEdgeInsets inset=_scroll.contentInset; inset.bottom=overlap; _scroll.contentInset=inset; _scroll.scrollIndicatorInsets=inset; UIView *first=_token.isFirstResponder?_token:(_username.isFirstResponder?_username:nil); if(first) [_scroll scrollRectToVisible:[first convertRect:first.bounds toView:_scroll] animated:YES]; }
@end
