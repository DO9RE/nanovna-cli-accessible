/**
 * @file hamspirit_gui_macos.mm
 * @brief Native macOS GUI for Ham Spirit game using AppKit/Cocoa
 *
 * Provides a native NSWindow with Core Graphics rendering for menu overlays,
 * text overlays, text input with cursor, and banner text — analogous to the
 * Windows GDI+ window implementation.
 *
 * No third-party dependencies required — uses only macOS system frameworks:
 *   - AppKit (NSWindow, NSView, NSFont, NSColor, NSImage)
 *   - Core Graphics (CGContext drawing)
 *
 * Threading model:
 *   All overlay update functions are called from the game thread (main thread).
 *   A periodic NSTimer drives redraws at ~30fps for cursor blink animation
 *   and to keep the window responsive during gameplay.
 */

#ifdef WITH_HAM_SPIRIT
#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstring>
#include "hamspirit_game.h"

using namespace HamSpirit;

// ============================================================================
// Shared overlay state — written by game thread, read by drawRect:
// ============================================================================

namespace {

struct MacGuiOverlayState {
    std::mutex mtx;

    bool menuVisible{false};
    std::string menuTitle;
    std::vector<std::string> menuItems;
    int menuSelected{0};

    bool textVisible{false};
    std::string textContent;

    bool inputVisible{false};
    std::string inputLabel;
    std::string inputValue;

    std::string bannerText;

    // Wallpaper images (loaded from img/ directory)
    NSImage* imgTitle{nil};   // HamSpirit-1.PNG (intro)
    NSImage* imgMenu{nil};    // HamSpirit-2.PNG (menu/game over)
    NSImage* imgRacing{nil};  // HamSpirit-3.PNG (racing/pause)
    int currentImage{0};      // 0=none, 1=title, 2=menu, 3=racing
};

// Thread-safe key event queue — written by NSView key handlers, read by game thread
struct KeyEvent {
    int keyCode;   // VK-style code (uppercase letter or VK constant)
    bool pressed;  // true = key down, false = key up
};

struct MacGuiKeyQueue {
    std::mutex mtx;
    std::vector<KeyEvent> events;
};

MacGuiOverlayState sGui;
MacGuiKeyQueue sKeyQueue;

// Window and view references (main thread only)
NSWindow*   sWindow   = nil;
NSTimer*    sTimer    = nil;
std::atomic<bool> sActive{false};

// Convert NSEvent keyCode / characters to VK-style codes used by the game
static int nsKeyCodeToVK(unsigned short keyCode, NSString* chars) {
    // Map macOS virtual key codes to Windows VK-style codes
    switch (keyCode) {
        case 123: return 0x25;  // Left arrow → VK_LEFT
        case 124: return 0x27;  // Right arrow → VK_RIGHT
        case 125: return 0x28;  // Down arrow → VK_DOWN
        case 126: return 0x26;  // Up arrow → VK_UP
        case 53:  return 0x1B;  // Escape → VK_ESCAPE
        case 36:  return 0x0D;  // Return → VK_RETURN
        case 76:  return 0x0D;  // Numpad enter → VK_RETURN
        case 48:  return 0x09;  // Tab → VK_TAB
        case 51:  return 0x08;  // Delete (backspace) → VK_BACK
        case 117: return 0x2E;  // Forward delete → VK_DELETE
        case 115: return 0x24;  // Home → VK_HOME
        case 119: return 0x23;  // End → VK_END
        case 49:  return 0x20;  // Space → VK_SPACE
        case 122: return 0x70;  // F1 → VK_F1
        case 120: return 0x71;  // F2
        case 99:  return 0x72;  // F3
        case 118: return 0x73;  // F4
        case 96:  return 0x74;  // F5
        case 97:  return 0x75;  // F6
        case 98:  return 0x76;  // F7
        case 100: return 0x77;  // F8
        case 101: return 0x78;  // F9
        case 109: return 0x79;  // F10
        case 103: return 0x7A;  // F11
        case 111: return 0x7B;  // F12
        default: break;
    }
    // Fall back to character-based mapping (uppercase letters, digits)
    if (chars && [chars length] > 0) {
        unichar ch = [chars characterAtIndex:0];
        if (ch >= 'a' && ch <= 'z') return ch - 'a' + 'A';  // Uppercase
        if (ch >= 'A' && ch <= 'Z') return ch;
        if (ch >= '0' && ch <= '9') return ch;
    }
    return -1;  // Unknown
}

static void pushKeyEvent(int vk, bool pressed) {
    if (vk < 0) return;
    std::lock_guard<std::mutex> lock(sKeyQueue.mtx);
    sKeyQueue.events.push_back({vk, pressed});
}

} // anonymous namespace

// ============================================================================
// Custom NSView — renders all game overlays via Core Graphics
// ============================================================================

@interface HamSpiritView : NSView
@end

@implementation HamSpiritView

- (BOOL)acceptsFirstResponder {
    return YES; // Required to receive key events
}

- (BOOL)isFlipped {
    return YES; // Top-left origin, matching Windows coordinate system
}

- (BOOL)isOpaque {
    return YES;
}

// ---- Key event forwarding to game thread ----
- (void)keyDown:(NSEvent*)event {
    int vk = nsKeyCodeToVK([event keyCode], [event charactersIgnoringModifiers]);
    pushKeyEvent(vk, true);
    // Don't call super — prevents system beep for unhandled keys
}

- (void)keyUp:(NSEvent*)event {
    int vk = nsKeyCodeToVK([event keyCode], [event charactersIgnoringModifiers]);
    pushKeyEvent(vk, false);
}

- (void)flagsChanged:(NSEvent*)event {
    [super flagsChanged:event];
}

// ---- Helper: Create NSFont with fallback chain ----
- (NSFont*)monoFontOfSize:(CGFloat)size {
    // Try common monospace fonts
    NSFont* font = [NSFont fontWithName:@"Menlo" size:size];
    if (!font) font = [NSFont fontWithName:@"SF Mono" size:size];
    if (!font) font = [NSFont fontWithName:@"Courier New" size:size];
    if (!font) font = [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightRegular];
    if (!font) font = [NSFont systemFontOfSize:size];
    return font;
}

// ---- Helper: Draw text at position ----
- (void)drawText:(NSString*)text
              at:(NSPoint)point
            font:(NSFont*)font
           color:(NSColor*)color
        centered:(BOOL)centered
           width:(CGFloat)areaWidth {
    if (!text || text.length == 0 || !font) return;

    NSDictionary* attrs = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: color
    };
    NSSize textSize = [text sizeWithAttributes:attrs];
    CGFloat x = point.x;
    if (centered) {
        x = point.x + (areaWidth - textSize.width) / 2.0;
    }
    [text drawAtPoint:NSMakePoint(x, point.y) withAttributes:attrs];
}

// ---- Helper: Fill rectangle ----
- (void)fillRect:(NSRect)rect color:(NSColor*)color {
    [color setFill];
    NSRectFill(rect);
}

// ---- Helper: Stroke rectangle ----
- (void)strokeRect:(NSRect)rect color:(NSColor*)color {
    [color setStroke];
    NSBezierPath* path = [NSBezierPath bezierPathWithRect:rect];
    [path setLineWidth:1.0];
    [path stroke];
}

// ---- Helper: Draw horizontal line ----
- (void)drawLineFromX:(CGFloat)x1 y:(CGFloat)y toX:(CGFloat)x2 color:(NSColor*)color {
    [color setStroke];
    NSBezierPath* line = [NSBezierPath bezierPath];
    [line moveToPoint:NSMakePoint(x1, y)];
    [line lineToPoint:NSMakePoint(x2, y)];
    [line setLineWidth:1.0];
    [line stroke];
}

// ---- Main drawing method ----
- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];

    NSRect bounds = [self bounds];
    CGFloat w = bounds.size.width;
    CGFloat h = bounds.size.height;

    // Theme colors derived from centralized palette (hamspirit_game.h)
    NSColor* bgColor     = [NSColor colorWithRed:CLR_HS_BG.r/255.0 green:CLR_HS_BG.g/255.0 blue:CLR_HS_BG.b/255.0 alpha:1.0];
    NSColor* cyanColor   = [NSColor colorWithRed:CLR_HS_CYAN.r/255.0 green:CLR_HS_CYAN.g/255.0 blue:CLR_HS_CYAN.b/255.0 alpha:1.0];
    NSColor* yellowColor = [NSColor colorWithRed:CLR_HS_YELLOW.r/255.0 green:CLR_HS_YELLOW.g/255.0 blue:CLR_HS_YELLOW.b/255.0 alpha:1.0];
    NSColor* whiteColor  = [NSColor colorWithRed:CLR_HS_WHITE.r/255.0 green:CLR_HS_WHITE.g/255.0 blue:CLR_HS_WHITE.b/255.0 alpha:1.0];
    NSColor* grayColor   = [NSColor colorWithRed:CLR_HS_GRAY.r/255.0 green:CLR_HS_GRAY.g/255.0 blue:CLR_HS_GRAY.b/255.0 alpha:1.0];
    NSColor* darkGray    = [NSColor colorWithRed:CLR_HS_DARK_GRAY.r/255.0 green:CLR_HS_DARK_GRAY.g/255.0 blue:CLR_HS_DARK_GRAY.b/255.0 alpha:1.0];
    NSColor* greenColor  = [NSColor colorWithRed:CLR_HS_GREEN.r/255.0 green:CLR_HS_GREEN.g/255.0 blue:CLR_HS_GREEN.b/255.0 alpha:1.0];
    NSColor* fieldBg     = [NSColor colorWithRed:CLR_HS_FIELD_BG.r/255.0 green:CLR_HS_FIELD_BG.g/255.0 blue:CLR_HS_FIELD_BG.b/255.0 alpha:1.0];
    NSColor* panelBorder = [NSColor colorWithRed:CLR_HS_PANEL_BRD.r/255.0 green:CLR_HS_PANEL_BRD.g/255.0 blue:CLR_HS_PANEL_BRD.b/255.0 alpha:1.0];
    NSColor* highlightBg = [NSColor colorWithRed:CLR_HS_HIGHLIGHT.r/255.0 green:CLR_HS_HIGHLIGHT.g/255.0 blue:CLR_HS_HIGHLIGHT.b/255.0 alpha:1.0];
    NSColor* bannerBg    = [NSColor colorWithRed:CLR_HS_BANNER_BG.r/255.0 green:CLR_HS_BANNER_BG.g/255.0 blue:CLR_HS_BANNER_BG.b/255.0 alpha:1.0];

    // Fonts
    NSFont* fontNormal = [self monoFontOfSize:16];
    NSFont* fontLarge  = [self monoFontOfSize:24];
    NSFont* fontSmall  = [self monoFontOfSize:12];

    // Read overlay state under lock
    bool menuVis, textVis, inputVis;
    std::string mTitle, tContent, iLabel, iValue, banner;
    std::vector<std::string> mItems;
    int mSel;
    NSImage* wallpaper = nil;
    {
        std::lock_guard<std::mutex> lock(sGui.mtx);
        menuVis  = sGui.menuVisible;
        mTitle   = sGui.menuTitle;
        mItems   = sGui.menuItems;
        mSel     = sGui.menuSelected;
        textVis  = sGui.textVisible;
        tContent = sGui.textContent;
        inputVis = sGui.inputVisible;
        iLabel   = sGui.inputLabel;
        iValue   = sGui.inputValue;
        banner   = sGui.bannerText;
        switch (sGui.currentImage) {
            case 1: wallpaper = sGui.imgTitle;  break;
            case 2: wallpaper = sGui.imgMenu;   break;
            case 3: wallpaper = sGui.imgRacing; break;
            default: break;
        }
    }

    // ---- Background ----
    [self fillRect:bounds color:bgColor];

    // ---- Wallpaper image (scaled to fill) ----
    if (wallpaper) {
        [wallpaper drawInRect:bounds
                     fromRect:NSZeroRect
                    operation:NSCompositingOperationSourceOver
                     fraction:0.4]; // Semi-transparent so overlays are readable
    }

    // ---- Header ----
    [self drawLineFromX:0 y:3 toX:w color:cyanColor];
    NSString* headerText = @"=== HAM SPIRIT ===";
    [self drawText:headerText at:NSMakePoint(0, 10) font:fontLarge color:cyanColor centered:YES width:w];
    [self drawLineFromX:0 y:45 toX:w color:cyanColor];

    CGFloat contentY = 60;

    // ---- Text input overlay (highest priority) ----
    if (inputVis) {
        CGFloat panelW = fmin(w - 80, 600);
        CGFloat panelH = 130;
        CGFloat panelX = (w - panelW) / 2;
        CGFloat panelY = (h - panelH) / 2;

        // Panel background + border
        NSRect panelRect = NSMakeRect(panelX, panelY, panelW, panelH);
        [self fillRect:panelRect color:bgColor];
        [self strokeRect:panelRect color:cyanColor];

        // Label
        NSString* label = [NSString stringWithUTF8String:iLabel.c_str()];
        [self drawText:label at:NSMakePoint(panelX, panelY + 15) font:fontNormal color:cyanColor centered:YES width:panelW];

        // Input field background
        CGFloat fX = panelX + 24, fW = panelW - 48, fY = panelY + 60, fH = 44;
        NSRect fieldRect = NSMakeRect(fX, fY, fW, fH);
        [self fillRect:fieldRect color:fieldBg];
        [self strokeRect:fieldRect color:panelBorder];

        // Value text with blinking cursor (unified timing via isCursorVisible)
        NSTimeInterval ticks = [[NSDate date] timeIntervalSince1970];
        bool cursorOn = HamSpirit::isCursorVisible(static_cast<float>(ticks));
        std::string display = iValue + (cursorOn ? "_" : " ");
        NSString* valStr = [NSString stringWithUTF8String:display.c_str()];
        [self drawText:valStr at:NSMakePoint(fX + 10, fY + 12) font:fontNormal color:whiteColor centered:NO width:0];

    // ---- Text overlay ----
    } else if (textVis) {
        // Word-wrap and center
        CGFloat maxW = fmin(w - 80, 700);
        NSFont* wrapFont = fontNormal;
        NSDictionary* attrs = @{NSFontAttributeName: wrapFont};

        // Measure character height
        NSSize mSize = [@"M" sizeWithAttributes:attrs];
        CGFloat lineH = mSize.height + 6;
        int charsPerLine = (int)(maxW / fmax(mSize.width, 1));

        std::string remaining = tContent;
        CGFloat row = contentY;
        while (!remaining.empty() && row < h - 60) {
            std::string line;
            if ((int)remaining.size() <= charsPerLine) {
                line = remaining;
                remaining.clear();
            } else {
                size_t brk = remaining.rfind(' ', charsPerLine);
                if (brk == std::string::npos || brk == 0) brk = charsPerLine;
                line = remaining.substr(0, brk);
                size_t next = brk + (brk < remaining.size() && remaining[brk] == ' ' ? 1 : 0);
                remaining = remaining.substr(next);
            }
            NSString* lineStr = [NSString stringWithUTF8String:line.c_str()];
            [self drawText:lineStr at:NSMakePoint(0, row) font:wrapFont color:whiteColor centered:YES width:w];
            row += lineH;
        }

    // ---- Menu overlay ----
    } else if (menuVis && !mItems.empty()) {
        // Title
        NSString* title = [NSString stringWithUTF8String:mTitle.c_str()];
        [self drawText:title at:NSMakePoint(0, contentY) font:fontLarge color:cyanColor centered:YES width:w];

        // Separator line
        [self drawLineFromX:w/2 - 160 y:contentY + 38 toX:w/2 + 160 color:cyanColor];

        // Menu items
        NSDictionary* normalAttrs = @{NSFontAttributeName: fontNormal};
        NSSize charSize = [@"M" sizeWithAttributes:normalAttrs];
        CGFloat lineH = charSize.height + 12;
        CGFloat itemY = contentY + 52;

        for (int i = 0; i < (int)mItems.size(); i++) {
            if (itemY >= h - 60) break;

            NSString* itemStr = [NSString stringWithUTF8String:mItems[i].c_str()];

            if (i == mSel) {
                // Highlight bar
                NSSize textSize = [itemStr sizeWithAttributes:normalAttrs];
                CGFloat barW = textSize.width + 60;
                NSRect hlRect = NSMakeRect(w/2 - barW/2, itemY - 2, barW, lineH);
                [self fillRect:hlRect color:highlightBg];
                [self strokeRect:hlRect color:yellowColor];

                NSString* label = [NSString stringWithFormat:@"> %@ <", itemStr];
                [self drawText:label at:NSMakePoint(0, itemY + 2) font:fontNormal color:yellowColor centered:YES width:w];
            } else {
                [self drawText:itemStr at:NSMakePoint(0, itemY + 2) font:fontNormal color:grayColor centered:YES width:w];
            }
            itemY += lineH;
        }
    }

    // ---- Banner at bottom ----
    if (!banner.empty()) {
        NSRect bannerRect = NSMakeRect(0, h - 38, w, 38);
        [self fillRect:bannerRect color:bannerBg];
        NSString* bannerStr = [NSString stringWithUTF8String:banner.c_str()];
        [self drawText:bannerStr at:NSMakePoint(12, h - 33) font:fontSmall color:greenColor centered:NO width:0];
    }

    // ---- Footer ----
    NSString* footer = @"Arrow keys: Navigate | Enter/A: Select | Escape/B: Back";
    [self drawText:footer at:NSMakePoint(12, h - 16) font:fontSmall color:darkGray centered:NO width:0];
}

@end

// ============================================================================
// Window delegate — handle close without quitting
// ============================================================================

@interface HamSpiritWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation HamSpiritWindowDelegate

- (BOOL)windowShouldClose:(id)sender {
    // Don't close — game lifecycle manages the window
    // Just hide it; stopHamSpiritWindow will clean up
    [sWindow orderOut:nil];
    return NO;
}

@end

// Keep delegate alive (prevent ARC deallocation)
static HamSpiritWindowDelegate* sDelegate = nil;

// ============================================================================
// Public API — called from hamspirit_game.cpp (game thread = main thread)
// ============================================================================

// Load wallpaper images using centralized paths from hamspirit_game.h
static void loadWallpaperImages() {
    auto paths = HamSpirit::getWallpaperPaths();
    NSImage** targets[] = {&sGui.imgTitle, &sGui.imgMenu, &sGui.imgRacing};

    for (int i = 0; i < std::min(static_cast<int>(paths.size()), 3); i++) {
        NSString* nsPath = [NSString stringWithUTF8String:paths[i].c_str()];
        NSImage* img = [[NSImage alloc] initWithContentsOfFile:nsPath];
        if (img && img.valid) {
            *targets[i] = img;
        }
    }
}

bool startHamSpiritWindow() {
    if (sActive.load()) return sWindow != nil;

    @autoreleasepool {
        // Ensure NSApplication is initialized (required for any AppKit usage)
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        // Load wallpaper images
        loadWallpaperImages();
        sGui.currentImage = 1; // Start with title image

        // Create window
        NSRect frame = NSMakeRect(0, 0, 960, 540);
        NSUInteger styleMask = NSWindowStyleMaskTitled
                             | NSWindowStyleMaskClosable
                             | NSWindowStyleMaskMiniaturizable
                             | NSWindowStyleMaskResizable;
        sWindow = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:styleMask
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
        if (!sWindow) return false;

        [sWindow setTitle:@"Ham Spirit"];
        [sWindow center];
        [sWindow setBackgroundColor:[NSColor colorWithRed:CLR_HS_BG.r/255.0 green:CLR_HS_BG.g/255.0 blue:CLR_HS_BG.b/255.0 alpha:1.0]];

        // Set delegate to intercept close button
        sDelegate = [[HamSpiritWindowDelegate alloc] init];
        [sWindow setDelegate:sDelegate];

        // Create custom content view
        HamSpiritView* view = [[HamSpiritView alloc] initWithFrame:frame];
        [sWindow setContentView:view];

        // Show and activate
        [sWindow makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        // Make the view first responder so it receives key events
        [sWindow makeFirstResponder:view];

        // Maximize (zoom)
        [sWindow zoom:nil];

        // Start periodic redraw timer (~30fps) for cursor blink and window responsiveness.
        // The timer fires on the main run loop. Since the game thread IS the main thread,
        // we pump events periodically to process timer and window events.
        sTimer = [NSTimer scheduledTimerWithTimeInterval:1.0/30.0
                                                repeats:YES
                                                  block:^(NSTimer* __unused t) {
            if (sWindow && [[sWindow contentView] isKindOfClass:[HamSpiritView class]]) {
                [[sWindow contentView] setNeedsDisplay:YES];
            }
        }];

        sActive.store(true);
    }

    return true;
}

void stopHamSpiritWindow() {
    if (!sActive.load()) return;

    @autoreleasepool {
        sActive.store(false);

        if (sTimer) {
            [sTimer invalidate];
            sTimer = nil;
        }

        if (sWindow) {
            [sWindow close];
            sWindow = nil;
        }

        sDelegate = nil;

        // Release wallpaper images
        {
            std::lock_guard<std::mutex> lock(sGui.mtx);
            sGui.imgTitle  = nil;
            sGui.imgMenu   = nil;
            sGui.imgRacing = nil;
        }
    }
}

// ---- Pump macOS events (called from overlay functions to keep window responsive) ----
static void pumpEvents() {
    @autoreleasepool {
        NSEvent* event;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:nil
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES])) {
            [NSApp sendEvent:event];
        }
    }
}

// ---- Public event pump — called from waitForInput() to keep the window alive ----
// Each call processes all pending AppKit events without blocking.
void pumpHamSpiritEvents() {
    pumpEvents();
}

// ---- Poll key event from the GUI window ----
// Returns true if an event was dequeued, filling outKeyCode and outPressed.
bool pollHamSpiritKeyEvent(int& outKeyCode, bool& outPressed) {
    std::lock_guard<std::mutex> lock(sKeyQueue.mtx);
    if (sKeyQueue.events.empty()) return false;
    outKeyCode = sKeyQueue.events.front().keyCode;
    outPressed = sKeyQueue.events.front().pressed;
    sKeyQueue.events.erase(sKeyQueue.events.begin());
    return true;
}

// ---- Request immediate redraw ----
static void requestRedraw() {
    if (sWindow) {
        [[sWindow contentView] setNeedsDisplay:YES];
        // Pump events so the redraw happens immediately
        pumpEvents();
    }
}

// ---- Set wallpaper image index (0=none, 1=title, 2=menu, 3=racing) ----
void setHamSpiritWallpaper(int imageIndex) {
    std::lock_guard<std::mutex> lock(sGui.mtx);
    sGui.currentImage = imageIndex;
}

// ============================================================================
// Overlay functions — same interface as Windows GDI+ and terminal versions
// ============================================================================

void pushBannerText(const std::string& text) {
    {
        std::lock_guard<std::mutex> lock(sGui.mtx);
        sGui.bannerText = text;
    }
    requestRedraw();
}

void updateMenuOverlay(const std::string& title,
                       const std::vector<std::string>& items,
                       int selectedIndex) {
    {
        std::lock_guard<std::mutex> lock(sGui.mtx);
        sGui.menuVisible = true;
        sGui.menuTitle = title;
        sGui.menuItems = items;
        sGui.menuSelected = selectedIndex;
    }
    requestRedraw();
}

void hideMenuOverlay() {
    {
        std::lock_guard<std::mutex> lock(sGui.mtx);
        sGui.menuVisible = false;
    }
    requestRedraw();
}

void showTextOverlay(const std::string& text) {
    {
        std::lock_guard<std::mutex> lock(sGui.mtx);
        sGui.textVisible = true;
        sGui.textContent = text;
    }
    requestRedraw();
}

void hideTextOverlay() {
    {
        std::lock_guard<std::mutex> lock(sGui.mtx);
        sGui.textVisible = false;
    }
    requestRedraw();
}

void showTextInputOverlay(const std::string& label, const std::string& value) {
    {
        std::lock_guard<std::mutex> lock(sGui.mtx);
        sGui.inputVisible = true;
        sGui.inputLabel = label;
        sGui.inputValue = value;
    }
    requestRedraw();
}

void hideTextInputOverlay() {
    {
        std::lock_guard<std::mutex> lock(sGui.mtx);
        sGui.inputVisible = false;
    }
    requestRedraw();
}

#endif // __APPLE__
#endif // WITH_HAM_SPIRIT
