#import "MacWindow.h"
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

@interface EngineWindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) EnginePlatform::MacWindow* engineWindow;
@end

@implementation EngineWindowDelegate

- (BOOL)windowShouldClose:(NSWindow *)sender {
    if (self.engineWindow) {
        self.engineWindow->SetOpen(false);
    }
    return YES;
}

- (void)windowWillClose:(NSNotification *)notification {
    if (self.engineWindow) {
        self.engineWindow->SetOpen(false);
    }
}

- (void)windowDidResize:(NSNotification *)notification {
    if (self.engineWindow) {
        NSWindow* win = notification.object;
        NSView* view = [win contentView];
        if (view) {
            CGSize sz = view.bounds.size;
            self.engineWindow->OnViewResize((int)sz.width, (int)sz.height);
        }
    }
}

@end

@interface EngineView : NSView <CALayerDelegate>
@property (nonatomic, assign) EnginePlatform::MacWindow* engineWindow;
@end

@implementation EngineView

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    return YES;
}

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    if (self.engineWindow) {
        self.engineWindow->OnViewResize((int)newSize.width, (int)newSize.height);
    }
}

- (void)mouseDown:(NSEvent *)event {
    [super mouseDown:event];
}

- (void)mouseDragged:(NSEvent *)event {
    [super mouseDragged:event];
}

@end

namespace EnginePlatform {

MacWindow::MacWindow(int width, int height, const std::string& title)
    : m_width(width), m_height(height), m_title(title), m_isOpen(true) {
}

MacWindow::~MacWindow() {
    @autoreleasepool {
        if (m_windowDelegate) {
            EngineWindowDelegate* delegate = (__bridge_transfer EngineWindowDelegate*)m_windowDelegate;
            delegate.engineWindow = nullptr;
            m_windowDelegate = nullptr;
        }
        if (m_nsWindow) {
            NSWindow* window = (__bridge_transfer NSWindow*)m_nsWindow;
            [window setDelegate:nil];
            [window close];
            m_nsWindow = nullptr;
        }
        if (m_nsView) {
            NSView* view = (__bridge_transfer NSView*)m_nsView;
            (void)view;
            m_nsView = nullptr;
        }
        if (m_metalLayer) {
            CAMetalLayer* layer = (__bridge_transfer CAMetalLayer*)m_metalLayer;
            (void)layer;
            m_metalLayer = nullptr;
        }
    }
}

bool MacWindow::Initialize() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSRect contentRect = NSMakeRect(0, 0, m_width, m_height);
        NSWindowStyleMask styleMask = NSWindowStyleMaskTitled |
                                      NSWindowStyleMaskClosable |
                                      NSWindowStyleMaskResizable |
                                      NSWindowStyleMaskMiniaturizable;

        NSWindow* window = [[NSWindow alloc] initWithContentRect:contentRect
                                                       styleMask:styleMask
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];

        [window setTitle:[NSString stringWithUTF8String:m_title.c_str()]];
        [window center];
        [window setAcceptsMouseMovedEvents:YES];

        EngineWindowDelegate* delegate = [[EngineWindowDelegate alloc] init];
        delegate.engineWindow = this;
        [window setDelegate:delegate];

        EngineView* view = [[EngineView alloc] initWithFrame:contentRect];
        view.engineWindow = this;
        [view setWantsLayer:YES];

        CAMetalLayer* metalLayer = [CAMetalLayer layer];
        CGFloat scale = [window backingScaleFactor];
        metalLayer.contentsScale = scale;
        metalLayer.drawableSize = CGSizeMake(m_width * scale, m_height * scale);
        metalLayer.framebufferOnly = NO; // Allow compute & blit access
        [view setLayer:metalLayer];

        [window setContentView:view];
        [window makeFirstResponder:view];

        m_nsWindow = (__bridge_retained void*)window;
        m_nsView = (__bridge_retained void*)view;
        m_metalLayer = (__bridge_retained void*)metalLayer;
        m_windowDelegate = (__bridge_retained void*)delegate;
        m_isOpen = true;

        return true;
    }
}

void MacWindow::Show() {
    @autoreleasepool {
        if (m_nsWindow) {
            NSWindow* window = (__bridge NSWindow*)m_nsWindow;
            [window makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
        }
    }
}

void MacWindow::ToggleFullscreen() {
    @autoreleasepool {
        if (m_nsWindow) {
            NSWindow* window = (__bridge NSWindow*)m_nsWindow;
            [window toggleFullScreen:nil];
            m_isFullscreen = !m_isFullscreen;
        }
    }
}

void MacWindow::OnViewResize(int width, int height) {
    if (width > 0 && height > 0) {
        m_width = width;
        m_height = height;
        if (m_nsWindow && m_metalLayer) {
            @autoreleasepool {
                NSWindow* window = (__bridge NSWindow*)m_nsWindow;
                CAMetalLayer* layer = (__bridge CAMetalLayer*)m_metalLayer;
                CGFloat scale = [window backingScaleFactor];
                layer.contentsScale = scale;
                layer.drawableSize = CGSizeMake(width * scale, height * scale);
            }
        }
    }
}

void MacWindow::SetDisplayMode(int width, int height, const std::string& mode) {
    @autoreleasepool {
        m_width = width;
        m_height = height;
        if (m_nsWindow) {
            NSWindow* window = (__bridge NSWindow*)m_nsWindow;
            NSRect frame = [window frame];
            frame.size = NSMakeSize(width, height);
            [window setFrame:frame display:YES animate:NO];

            if (m_metalLayer) {
                CAMetalLayer* layer = (__bridge CAMetalLayer*)m_metalLayer;
                CGFloat scale = [window backingScaleFactor];
                layer.contentsScale = scale;
                layer.drawableSize = CGSizeMake(width * scale, height * scale);
            }
        }
    }
}

void MacWindow::SetCursorEnabled(bool enabled) {
    m_cursorEnabled = enabled;
    if (enabled) {
        CGAssociateMouseAndMouseCursorPosition(true);
        [NSCursor unhide];
    } else {
        CGAssociateMouseAndMouseCursorPosition(false);
        [NSCursor hide];
    }
}

bool MacWindow::IsKeyDown(int key) const {
    if (key >= 0 && key < 256) {
        return m_keys[key];
    }
    return false;
}

void MacWindow::GetMouseDelta(int& dx, int& dy) {
    dx = m_mouseDeltaX;
    dy = m_mouseDeltaY;
}

void MacWindow::ResetMouseDelta() {
    m_mouseDeltaX = 0;
    m_mouseDeltaY = 0;
}

bool MacWindow::ProcessEvents() {
    @autoreleasepool {
        NSEvent* event = nil;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:[NSDate distantPast]
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES])) {
            NSEventType type = [event type];

            if (type == NSEventTypeKeyDown) {
                unsigned short code = [event keyCode];
                // Map common macOS keycodes to standard virtual keys
                // 13 = W, 0 = A, 1 = S, 2 = D, 49 = Space, 56 = Shift
                if (code == 13) m_keys['W'] = true;
                if (code == 0)  m_keys['A'] = true;
                if (code == 1)  m_keys['S'] = true;
                if (code == 2)  m_keys['D'] = true;
                if (code == 12) m_keys['Q'] = true;
                if (code == 14) m_keys['E'] = true;
                if (code == 49) m_keys[0x20] = true; // Space
                if (code == 56 || code == 60) m_keys[0x10] = true; // Shift
                if (code == 53) m_keys[0x1B] = true; // Escape
            } else if (type == NSEventTypeKeyUp) {
                unsigned short code = [event keyCode];
                if (code == 13) m_keys['W'] = false;
                if (code == 0)  m_keys['A'] = false;
                if (code == 1)  m_keys['S'] = false;
                if (code == 2)  m_keys['D'] = false;
                if (code == 12) m_keys['Q'] = false;
                if (code == 14) m_keys['E'] = false;
                if (code == 49) m_keys[0x20] = false; // Space
                if (code == 56 || code == 60) m_keys[0x10] = false; // Shift
                if (code == 53) m_keys[0x1B] = false; // Escape
            } else if (type == NSEventTypeMouseMoved || type == NSEventTypeLeftMouseDragged || type == NSEventTypeRightMouseDragged) {
                m_mouseDeltaX += (int)[event deltaX];
                m_mouseDeltaY += (int)[event deltaY];
            }

            [NSApp sendEvent:event];
        }

        // Update drawable size if backing view resized
        if (m_nsWindow && m_metalLayer && m_nsView) {
            NSWindow* window = (__bridge NSWindow*)m_nsWindow;
            CAMetalLayer* layer = (__bridge CAMetalLayer*)m_metalLayer;
            NSView* view = (__bridge NSView*)m_nsView;
            CGFloat scale = [window backingScaleFactor];
            CGSize viewSize = view.bounds.size;
            int newW = (int)viewSize.width;
            int newH = (int)viewSize.height;
            if (newW > 0 && newH > 0 && (newW != m_width || newH != m_height)) {
                m_width = newW;
                m_height = newH;
            }
            CGSize targetSize = CGSizeMake(m_width * scale, m_height * scale);
            if (layer.drawableSize.width != targetSize.width || layer.drawableSize.height != targetSize.height) {
                layer.contentsScale = scale;
                layer.drawableSize = targetSize;
            }
        }

        return m_isOpen;
    }
}

} // namespace EnginePlatform
