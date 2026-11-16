#import "P64PieceView.h"

#include <algorithm>
#include <cctype>

namespace {
void Rect(UIBezierPath *path, CGFloat x, CGFloat y, CGFloat width, CGFloat height) {
  [path appendPath:[UIBezierPath bezierPathWithRect:CGRectMake(x, y, width, height)]];
}

void Circle(UIBezierPath *path, CGFloat x, CGFloat y, CGFloat radius) {
  [path appendPath:[UIBezierPath bezierPathWithOvalInRect:CGRectMake(x - radius, y - radius, radius * 2, radius * 2)]];
}

void Base(UIBezierPath *path) { Rect(path, 11, 34, 23, 4.5); }
void RoyalBase(UIBezierPath *path) { Rect(path, 13, 30.5, 19, 3); Base(path); }
void Move(UIBezierPath *path, CGFloat x, CGFloat y) { [path moveToPoint:CGPointMake(x, y)]; }
void Line(UIBezierPath *path, CGFloat x, CGFloat y) { [path addLineToPoint:CGPointMake(x, y)]; }
void Curve(UIBezierPath *path, CGFloat x, CGFloat y, CGFloat x1, CGFloat y1, CGFloat x2, CGFloat y2) {
  [path addCurveToPoint:CGPointMake(x, y) controlPoint1:CGPointMake(x1, y1) controlPoint2:CGPointMake(x2, y2)];
}

void MakePiece(char type, UIBezierPath *body, UIBezierPath *detail) {
  switch (type) {
    case 'p':
      Circle(body, 22.5, 14, 5.5);
      Move(body, 18, 34); Line(body, 20.3, 20.5); Line(body, 24.7, 20.5); Line(body, 27, 34); [body closePath];
      Base(body);
      break;
    case 'r':
      Move(body, 12.5, 10); Line(body, 16.5, 10); Line(body, 16.5, 13); Line(body, 20.5, 13);
      Line(body, 20.5, 10); Line(body, 24.5, 10); Line(body, 24.5, 13); Line(body, 28.5, 13);
      Line(body, 28.5, 10); Line(body, 32.5, 10); Line(body, 32.5, 17.5); Line(body, 12.5, 17.5); [body closePath];
      Rect(body, 15.5, 17.5, 14, 16.5); Base(body);
      Move(detail, 15.5, 17.5); Line(detail, 29.5, 17.5);
      break;
    case 'n':
      Move(body, 14, 34); Curve(body, 19, 21, 14, 28, 17, 24.5); Line(body, 13.5, 23.5); Line(body, 10.5, 20.5);
      Curve(body, 19, 9.5, 12, 15.5, 15, 11.5); Line(body, 18, 6); Line(body, 22, 8.5);
      Curve(body, 32.5, 21.5, 28, 8.5, 32.5, 13.5); Curve(body, 32, 34, 32.5, 26.5, 32, 31); [body closePath];
      Base(body); Circle(detail, 19, 14.5, 1.3);
      break;
    case 'b':
      Circle(body, 22.5, 8.5, 2.5); Move(body, 22.5, 11.5);
      Curve(body, 17, 27.5, 16.5, 16, 15, 22); Line(body, 28, 27.5);
      Curve(body, 22.5, 11.5, 30, 22, 28.5, 16); [body closePath];
      Rect(body, 16, 27.5, 13, 3.5); Base(body);
      Move(detail, 22.5, 16); Line(detail, 22.5, 22); Move(detail, 19.5, 19); Line(detail, 25.5, 19);
      break;
    case 'q':
      Circle(body, 10, 15, 2.2); Circle(body, 22.5, 10.5, 2.2); Circle(body, 35, 15, 2.2);
      Move(body, 10.5, 17); Line(body, 14, 30.5); Line(body, 31, 30.5); Line(body, 34.5, 17);
      Line(body, 28, 24.5); Line(body, 22.5, 13); Line(body, 17, 24.5); [body closePath]; RoyalBase(body);
      break;
    case 'k':
      Move(body, 21, 4.5); Line(body, 24, 4.5); Line(body, 24, 7.5); Line(body, 27, 7.5);
      Line(body, 27, 10.5); Line(body, 24, 10.5); Line(body, 24, 14); Line(body, 21, 14);
      Line(body, 21, 10.5); Line(body, 18, 10.5); Line(body, 18, 7.5); Line(body, 21, 7.5); [body closePath];
      Move(body, 14, 30.5); Curve(body, 22.5, 15.5, 11.5, 22.5, 15, 16);
      Curve(body, 31, 30.5, 30, 16, 33.5, 22.5); [body closePath]; RoyalBase(body);
      Move(detail, 22.5, 19); Line(detail, 22.5, 27);
      break;
    default: break;
  }
}
}

@implementation P64PieceView

- (void)configure {
  self.opaque = NO;
  self.backgroundColor = UIColor.clearColor;
  self.userInteractionEnabled = NO;
  self.isAccessibilityElement = NO;
  self.contentMode = UIViewContentModeRedraw;
}

- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) [self configure];
  return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
  if ((self = [super initWithCoder:coder])) [self configure];
  return self;
}

- (void)setPiece:(NSString *)piece {
  if (_piece == piece || [_piece isEqualToString:piece]) return;
  _piece = [piece copy];
  [self setNeedsDisplay];
}

- (void)drawRect:(CGRect)rect {
  (void)rect;
  if (self.piece.length != 1) return;
  unichar character = [self.piece characterAtIndex:0];
  if (character > 127) return;
  char type = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  if (type != 'p' && type != 'r' && type != 'n' && type != 'b' && type != 'q' && type != 'k') return;
  BOOL white = character >= 'A' && character <= 'Z';
  UIColor *light = [UIColor colorWithRed:250.0/255.0 green:250.0/255.0 blue:247.0/255.0 alpha:1];
  UIColor *dark = [UIColor colorWithRed:20.0/255.0 green:23.0/255.0 blue:28.0/255.0 alpha:1];
  UIColor *fill = white ? light : dark;
  UIColor *ink = white ? dark : light;
  CGFloat side = std::min(CGRectGetWidth(self.bounds), CGRectGetHeight(self.bounds));
  if (side <= 0) return;
  CGContextRef context = UIGraphicsGetCurrentContext();
  CGContextSaveGState(context);
  CGContextTranslateCTM(context, CGRectGetMidX(self.bounds) - side / 2, CGRectGetMidY(self.bounds) - side / 2);
  CGContextScaleCTM(context, side / 45, side / 45);
  UIBezierPath *body = [UIBezierPath bezierPath];
  UIBezierPath *detail = [UIBezierPath bezierPath];
  MakePiece(type, body, detail);
  body.lineWidth = 1.3;
  body.lineJoinStyle = kCGLineJoinRound;
  [fill setFill]; [ink setStroke]; [body fill]; [body stroke];
  detail.lineWidth = 1.5;
  detail.lineCapStyle = kCGLineCapRound;
  [ink setFill]; [detail fill]; [detail stroke];
  CGContextRestoreGState(context);
}
@end
