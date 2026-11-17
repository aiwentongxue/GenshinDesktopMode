#import <AppKit/AppKit.h>
#include "../genshin/native_cursor.h"
#include <cassert>
#include <iostream>
static unsigned setCalls;
static void SpySet(id cursor,SEL selector) {assert(cursor && selector==@selector(set));++setCalls;}
int main(int argc,char **argv) {
    @autoreleasepool {
        assert(argc==2);
        NSData *png=[NSData dataWithContentsOfFile:@(argv[1])];assert(png);
        NSImage *source=[[NSImage alloc] initWithData:png];assert(source);
        CGPoint tip=DesktopNativeCursorHotspot(CGSizeMake(32,32),CGPointZero);
        assert(CGPointEqualToPoint(tip,CGPointMake(2,2)));
        NSCursor *cursor=MakeDesktopNativeCursor(png,CGSizeMake(32,32),tip);
        assert(cursor && NSEqualSizes(cursor.image.size,NSMakeSize(32,32)));
        assert(NSEqualPoints(cursor.hotSpot,NSMakePoint(2,2)));
        assert(CGPointEqualToPoint(DesktopNativeCursorHotspot(CGSizeMake(32,32),CGPointMake(4,6)),CGPointMake(4,6)));
        assert(CGPointEqualToPoint(DesktopNativeCursorHotspot(CGSizeMake(48,48),CGPointZero),CGPointZero));
        NSCursor *current=NSCursor.currentCursor;
        ApplyDesktopNativeCursor(current,SpySet,false);assert(setCalls==0);
        ApplyDesktopNativeCursor(current,SpySet,true);assert(setCalls==1);
        ApplyDesktopNativeCursor(current,SpySet,true);assert(setCalls==2);
        // No cursor is set and no system pointer event is posted by this test.
        std::cout<<"Native NSCursor: real game image, fixed point size and physical tip hotspot passed\n";
    }
}
