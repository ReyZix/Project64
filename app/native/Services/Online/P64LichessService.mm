#import "P64LichessService.h"

static NSString *const P64LichessErrorDomain = @"P64LichessError";
static const NSUInteger P64MaxLine = 1024 * 1024;
static NSError *P64Error(NSInteger code, NSString *message) {
    return [NSError errorWithDomain:P64LichessErrorDomain code:code userInfo:@{NSLocalizedDescriptionKey: message}];
}
static NSString *P64Encode(NSString *value) {
    NSCharacterSet *allowed = [NSCharacterSet characterSetWithCharactersInString:@"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~"];
    return [value stringByAddingPercentEncodingWithAllowedCharacters:allowed] ?: @"";
}
static void P64Main(dispatch_block_t block) {
    if (NSThread.isMainThread) block(); else dispatch_async(dispatch_get_main_queue(), block);
}

@class P64LichessService;
@interface P64OnlineStream : NSObject <P64LichessStream, NSURLSessionDataDelegate>
@property(nonatomic, weak) P64LichessService *owner;
@property(nonatomic, strong) NSURLSession *session;
@property(nonatomic, strong) NSURLSessionDataTask *task;
@property(nonatomic, strong) NSMutableData *buffer;
@property(nonatomic, copy) P64LichessEvent event;
@property(nonatomic, copy) P64LichessStreamCompletion completion;
@property(nonatomic) BOOL finished;
@property(nonatomic) BOOL accepted;
@property(nonatomic) BOOL discarding;
- (void)finish:(NSError *)error;
@end

@interface P64LichessService () <NSURLSessionTaskDelegate>
@property(nonatomic, copy) NSString *token;
@property(nonatomic, strong) NSURL *baseURL;
@property(nonatomic, strong) NSURLSession *shortSession;
@property(nonatomic, strong) NSMutableArray *pending;
@property(nonatomic, strong) NSMutableSet<P64OnlineStream *> *streams;
@property(nonatomic, strong) NSURLSessionDataTask *shortTask;
@property(nonatomic, copy) P64LichessCompletion currentCompletion;
@property(nonatomic) NSTimeInterval limitedUntil;
@property(nonatomic) BOOL invalidated;
- (NSError *)statusError:(NSInteger)status data:(NSData *)data;
- (void)removeStream:(P64OnlineStream *)stream;
@end

@implementation P64OnlineStream
- (void)cancel { P64Main(^{ [self finish:nil]; }); }
- (void)finish:(NSError *)error {
    if (_finished) return;
    _finished = YES;
    [_task cancel];
    [_session invalidateAndCancel];
    [_owner removeStream:self];
    P64LichessStreamCompletion done = _completion;
    _completion = nil; _event = nil; _task = nil; _session = nil;
    if (done) done(error);
}
- (void)emitLine:(NSData *)line {
    if (!line.length || _finished) return;
    id obj = [NSJSONSerialization JSONObjectWithData:line options:0 error:nil];
    if ([obj isKindOfClass:NSDictionary.class] && _event) _event(obj);
}
- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task willPerformHTTPRedirection:(NSHTTPURLResponse *)response newRequest:(NSURLRequest *)request completionHandler:(void (^)(NSURLRequest *))handler {
    handler(nil);
}
- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task didReceiveResponse:(NSURLResponse *)response completionHandler:(void (^)(NSURLSessionResponseDisposition))handler {
    NSInteger status = [response isKindOfClass:NSHTTPURLResponse.class] ? ((NSHTTPURLResponse *)response).statusCode : 0;
    if (status < 200 || status >= 300) {
        handler(NSURLSessionResponseCancel);
        [self finish:[_owner statusError:status data:nil]];
        return;
    }
    _accepted = YES;
    handler(NSURLSessionResponseAllow);
}
- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task didReceiveData:(NSData *)data {
    if (_finished || !_accepted) return;
    const unsigned char *bytes = (const unsigned char *)data.bytes;
    NSUInteger start = 0;
    for (NSUInteger i = 0; i < data.length; ++i) {
        if (bytes[i] != '\n') continue;
        NSUInteger length = i - start;
        if (!_discarding && _buffer.length + length <= P64MaxLine) {
            [_buffer appendBytes:bytes + start length:length];
            [self emitLine:_buffer];
        }
        [_buffer setLength:0]; _discarding = NO; start = i + 1;
        if (_finished) return;
    }
    NSUInteger length = data.length - start;
    if (!_discarding && _buffer.length + length <= P64MaxLine) [_buffer appendBytes:bytes + start length:length];
    else { [_buffer setLength:0]; _discarding = YES; }
}
- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error {
    if (_finished) return;
    if (!error && _accepted && !_discarding) [self emitLine:_buffer];
    if (_finished) return;
    [self finish:error ? P64Error(0, @"Could not reach Lichess.") : nil];
}
@end

@implementation P64LichessService
- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task willPerformHTTPRedirection:(NSHTTPURLResponse *)response newRequest:(NSURLRequest *)request completionHandler:(void (^)(NSURLRequest *))handler {
    handler(nil);
}
- (instancetype)initWithToken:(NSString *)token baseURL:(NSURL *)baseURL {
    if ((self = [super init])) {
        _token = [token copy]; _baseURL = baseURL;
        _pending = [NSMutableArray array]; _streams = [NSMutableSet set];
        _shortSession = [NSURLSession sessionWithConfiguration:NSURLSessionConfiguration.ephemeralSessionConfiguration delegate:self delegateQueue:NSOperationQueue.mainQueue];
    }
    return self;
}
- (NSURLRequest *)request:(NSString *)method path:(NSString *)path body:(NSString *)body {
    NSString *base = _baseURL.absoluteString;
    while ([base hasSuffix:@"/"]) base = [base substringToIndex:base.length - 1];
    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:[base stringByAppendingString:path]]];
    request.HTTPMethod = method;
    [request setValue:[@"Bearer " stringByAppendingString:_token] forHTTPHeaderField:@"Authorization"];
    [request setValue:@"application/x-ndjson, application/json" forHTTPHeaderField:@"Accept"];
    if (body) { request.HTTPBody = [body dataUsingEncoding:NSUTF8StringEncoding]; [request setValue:@"application/x-www-form-urlencoded" forHTTPHeaderField:@"Content-Type"]; }
    return request;
}
- (NSError *)statusError:(NSInteger)status data:(NSData *)data {
    if (status == 401) return P64Error(401, @"Lichess rejected the token. Check it in Settings.");
    if (status == 429) { _limitedUntil = NSProcessInfo.processInfo.systemUptime + 60; return P64Error(429, @"Lichess asked us to slow down. Try again in a minute."); }
    NSString *detail = nil;
    if (data.length) {
        id json = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
        if ([json isKindOfClass:NSDictionary.class] && [json[@"error"] isKindOfClass:NSString.class]) detail = json[@"error"];
        if (!detail) detail = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
        detail = [detail stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    }
    return P64Error(status, detail.length ? detail : [NSString stringWithFormat:@"Lichess refused the request (%ld).", (long)status]);
}
- (NSError *)availabilityError {
    if (_invalidated) return P64Error(NSURLErrorCancelled, @"Lichess service was closed.");
    if (NSProcessInfo.processInfo.systemUptime < _limitedUntil) return P64Error(429, @"Lichess asked us to slow down. Try again in a minute.");
    return nil;
}
- (void)enqueue:(NSString *)method path:(NSString *)path completion:(P64LichessCompletion)completion {
    P64Main(^{
        if (self.invalidated) { completion(nil, [self availabilityError]); return; }
        [self.pending addObject:@{ @"method":method, @"path":path, @"completion":[completion copy] }];
        [self runNext];
    });
}
- (void)runNext {
    if (_shortTask || !_pending.count || _invalidated) return;
    NSDictionary *item = _pending.firstObject; [_pending removeObjectAtIndex:0];
    P64LichessCompletion completion = item[@"completion"];
    NSError *unavailable = [self availabilityError];
    if (unavailable) { completion(nil, unavailable); [self runNext]; return; }
    _currentCompletion = completion;
    __weak P64LichessService *weakSelf = self;
    _shortTask = [_shortSession dataTaskWithRequest:[self request:item[@"method"] path:item[@"path"] body:nil] completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
        P64Main(^{
            P64LichessService *strongSelf = weakSelf;
            if (!strongSelf || !strongSelf.currentCompletion) return;
            P64LichessCompletion done = strongSelf.currentCompletion;
            strongSelf.currentCompletion = nil; strongSelf.shortTask = nil;
            NSInteger status = [response isKindOfClass:NSHTTPURLResponse.class] ? ((NSHTTPURLResponse *)response).statusCode : 0;
            NSError *failure = error ? P64Error(0, @"Could not reach Lichess.") : ((status < 200 || status >= 300) ? [strongSelf statusError:status data:data] : nil);
            id json = !failure && data.length ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
            done([json isKindOfClass:NSDictionary.class] ? json : (failure ? nil : @{}), failure);
            [strongSelf runNext];
        });
    }];
    [_shortTask resume];
}
- (void)invalidate {
    P64Main(^{
        if (self.invalidated) return;
        self.invalidated = YES;
        [self.shortTask cancel]; self.shortTask = nil;
        P64LichessCompletion current = self.currentCompletion; self.currentCompletion = nil;
        NSArray *pending = [self.pending copy]; [self.pending removeAllObjects];
        for (P64OnlineStream *stream in [self.streams copy]) [stream finish:nil];
        [self.shortSession invalidateAndCancel];
        NSError *error = [self availabilityError];
        if (current) current(nil, error);
        for (NSDictionary *item in pending) { P64LichessCompletion done = item[@"completion"]; done(nil, error); }
    });
}
- (void)removeStream:(P64OnlineStream *)stream { [_streams removeObject:stream]; }
- (id<P64LichessStream>)stream:(NSString *)method path:(NSString *)path body:(NSString *)body event:(P64LichessEvent)event completion:(P64LichessStreamCompletion)completion {
    P64OnlineStream *stream = [P64OnlineStream new]; stream.owner = self; stream.buffer = [NSMutableData data]; stream.event = event; stream.completion = completion;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (stream.finished) return;
        NSError *error = [self availabilityError];
        if (error) { [stream finish:error]; return; }
        [self.streams addObject:stream];
        stream.session = [NSURLSession sessionWithConfiguration:NSURLSessionConfiguration.ephemeralSessionConfiguration delegate:stream delegateQueue:NSOperationQueue.mainQueue];
        stream.task = [stream.session dataTaskWithRequest:[self request:method path:path body:body]];
        [stream.task resume];
    });
    return stream;
}
- (void)account:(P64LichessCompletion)c { [self enqueue:@"GET" path:@"/api/account" completion:c]; }
- (void)makeMove:(NSString *)uci gameID:(NSString *)gameID completion:(P64LichessCompletion)c { [self enqueue:@"POST" path:[NSString stringWithFormat:@"/api/board/game/%@/move/%@", P64Encode(gameID), P64Encode(uci)] completion:c]; }
- (void)resignGame:(NSString *)gameID completion:(P64LichessCompletion)c { [self enqueue:@"POST" path:[NSString stringWithFormat:@"/api/board/game/%@/resign", P64Encode(gameID)] completion:c]; }
- (void)abortGame:(NSString *)gameID completion:(P64LichessCompletion)c { [self enqueue:@"POST" path:[NSString stringWithFormat:@"/api/board/game/%@/abort", P64Encode(gameID)] completion:c]; }
- (void)acceptChallenge:(NSString *)ident completion:(P64LichessCompletion)c { [self enqueue:@"POST" path:[NSString stringWithFormat:@"/api/challenge/%@/accept", P64Encode(ident)] completion:c]; }
- (void)declineChallenge:(NSString *)ident completion:(P64LichessCompletion)c { [self enqueue:@"POST" path:[NSString stringWithFormat:@"/api/challenge/%@/decline", P64Encode(ident)] completion:c]; }
- (void)cancelChallenge:(NSString *)ident completion:(P64LichessCompletion)c { [self enqueue:@"POST" path:[NSString stringWithFormat:@"/api/challenge/%@/cancel", P64Encode(ident)] completion:c]; }
- (id<P64LichessStream>)streamEvents:(P64LichessEvent)e completion:(P64LichessStreamCompletion)c { return [self stream:@"GET" path:@"/api/stream/event" body:nil event:e completion:c]; }
- (id<P64LichessStream>)streamGame:(NSString *)ident event:(P64LichessEvent)e completion:(P64LichessStreamCompletion)c { return [self stream:@"GET" path:[@"/api/board/game/stream/" stringByAppendingString:P64Encode(ident)] body:nil event:e completion:c]; }
- (id<P64LichessStream>)seekMinutes:(NSInteger)m increment:(NSInteger)i rated:(BOOL)r completion:(P64LichessStreamCompletion)c { return [self stream:@"POST" path:@"/api/board/seek" body:[NSString stringWithFormat:@"rated=%@&time=%ld&increment=%ld", r ? @"true" : @"false", (long)m, (long)i] event:^(NSDictionary *e) { (void)e; } completion:c]; }
- (id<P64LichessStream>)challengeUsername:(NSString *)name minutes:(NSInteger)m increment:(NSInteger)i rated:(BOOL)r onCreated:(void (^)(NSString *))created completion:(P64LichessStreamCompletion)c {
    __block BOOL sent = NO;
    return [self stream:@"POST" path:[@"/api/challenge/" stringByAppendingString:P64Encode(name)] body:[NSString stringWithFormat:@"rated=%@&clock.limit=%ld&clock.increment=%ld&keepAliveStream=true", r ? @"true" : @"false", (long)(m * 60), (long)i] event:^(NSDictionary *e) { id ident = e[@"id"]; if (![ident isKindOfClass:NSString.class] && [e[@"challenge"] isKindOfClass:NSDictionary.class]) ident = e[@"challenge"][@"id"]; if (!sent && [ident isKindOfClass:NSString.class]) { sent = YES; created(ident); } } completion:c];
}
@end
