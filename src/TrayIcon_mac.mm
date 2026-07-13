#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "volt-ui/TrayIcon.h"
#include <cstring>

@interface TransFlintTrayDelegate : NSObject
@property (nonatomic, assign) volt::TrayIcon* tray;
@end

@implementation TransFlintTrayDelegate
- (void)statusItemClicked:(id)sender {
    if (self.tray) {
        volt::TrayEvent ev; ev.type = volt::TrayEventType::LeftClick;
        self.tray->PushEvent(ev);
    }
}
- (void)menuItemSelected:(NSMenuItem*)sender {
    if (self.tray) {
        volt::TrayEvent ev; ev.type = volt::TrayEventType::MenuSelect;
        ev.menuId = std::to_string(sender.tag);
        self.tray->PushEvent(ev);
    }
}
@end

namespace volt {

struct TrayIcon::PlatformData {
    NSStatusItem* statusItem = nullptr;
    NSMenu* menu = nullptr;
    NSImage* icon = nullptr;
    TransFlintTrayDelegate* delegate = nullptr;
    std::string tooltip;
    bool visible = false;
};

TrayIcon::TrayIcon()  { m_data = std::make_unique<PlatformData>(); }
TrayIcon::~TrayIcon() { Hide(); }
TrayIcon::Ptr TrayIcon::Create() { return std::make_shared<TrayIcon>(); }

bool TrayIcon::PlatformInit(const std::string& iconPath, const std::string& tooltip) {
    auto& d = *m_data;
    d.tooltip = tooltip;
    @autoreleasepool {
        NSImage* img = nil;
        if (!iconPath.empty()) {
            img = [[NSImage alloc] initWithContentsOfFile:
                [NSString stringWithUTF8String:iconPath.c_str()]];
        }
        if (!img) {
            img = [[NSImage alloc] initWithSize:NSMakeSize(16, 16)];
            [img lockFocus];
            [[NSColor colorWithCalibratedRed:0.2 green:0.51 blue:0.78 alpha:1.0] setFill];
            NSRectFill(NSMakeRect(0, 0, 16, 16));
            [img unlockFocus];
        }
        d.icon = img;
    }
    return true;
}

bool TrayIcon::PlatformInitFromData(const uint8_t* rgba, int w, int h, const std::string& tooltip) {
    auto& d = *m_data;
    d.tooltip = tooltip;
    @autoreleasepool {
        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:nil pixelsWide:w pixelsHigh:h
            bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
            colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:w*4 bitsPerPixel:32];
        if (rep) {
            memcpy([rep bitmapData], rgba, w * h * 4);
            NSImage* img = [[NSImage alloc] initWithSize:NSMakeSize(w, h)];
            [img addRepresentation:rep];
            d.icon = img;
        }
    }
    return true;
}

void TrayIcon::PlatformShow() {
    auto& d = *m_data;
    @autoreleasepool {
        d.delegate = [[TransFlintTrayDelegate alloc] init];
        d.delegate.tray = this;
        d.statusItem = [[NSStatusBar systemStatusBar]
            statusItemWithLength:NSSquareStatusItemLength];
        [d.statusItem retain];

        if (d.icon) {
            NSImage* scaled = [d.icon copy];
            [scaled setSize:NSMakeSize(18, 18)];
            [[d.statusItem button] setImage:scaled];
        }
        [[d.statusItem button] setToolTip:
            [NSString stringWithUTF8String:d.tooltip.c_str()]];
        [[d.statusItem button] setTarget:d.delegate];
        [[d.statusItem button] setAction:@selector(statusItemClicked:)];
        d.visible = true;
    }
}

void TrayIcon::PlatformHide() {
    auto& d = *m_data;
    @autoreleasepool {
        if (d.statusItem) {
            [[NSStatusBar systemStatusBar] removeStatusItem:d.statusItem];
            d.statusItem = nullptr;
        }
        d.menu = nullptr;
        d.delegate = nullptr;
    }
    d.visible = false;
}

void TrayIcon::PlatformUpdateMenu() {
    auto& d = *m_data;
    if (!d.statusItem) return;
    @autoreleasepool {
        NSMenu* menu = [[NSMenu alloc] init];
        for (size_t i = 0; i < m_menuItems.size(); i++) {
            const auto& item = m_menuItems[i];
            if (item.separator) {
                [menu addItem:[NSMenuItem separatorItem]];
            } else {
                NSMenuItem* menuItem = [[NSMenuItem alloc]
                    initWithTitle:[NSString stringWithUTF8String:item.label.c_str()]
                    action:@selector(menuItemSelected:) keyEquivalent:@""];
                [menuItem setTarget:d.delegate];
                [menuItem setTag:i];
                [menuItem setEnabled:item.enabled];
                [menuItem setState:item.checked ? NSOnState : NSOffState];
                [menu addItem:menuItem];
            }
        }
        d.menu = menu;
        [d.statusItem setMenu:menu];
    }
}

void TrayIcon::PlatformSetTooltip(const std::string& tooltip) {
    auto& d = *m_data;
    d.tooltip = tooltip;
    if (d.statusItem)
        [[d.statusItem button] setToolTip:[NSString stringWithUTF8String:tooltip.c_str()]];
}

void TrayIcon::PlatformSetIcon(const std::string& iconPath) {
    auto& d = *m_data;
    @autoreleasepool {
        NSImage* img = [[NSImage alloc] initWithContentsOfFile:
            [NSString stringWithUTF8String:iconPath.c_str()]];
        if (img) {
            d.icon = img;
            if (d.statusItem) {
                NSImage* scaled = [img copy];
                [scaled setSize:NSMakeSize(18, 18)];
                [[d.statusItem button] setImage:scaled];
            }
        }
    }
}

bool TrayIcon::PlatformIsVisible() const { return m_data->visible; }

bool TrayIcon::Init(const std::string& p, const std::string& t) { return PlatformInit(p, t); }
bool TrayIcon::InitFromData(const uint8_t* rgba, int w, int h, const std::string& t) { return PlatformInitFromData(rgba, w, h, t); }
void TrayIcon::Show() { if (!m_visible) { m_visible = true; PlatformShow(); } }
void TrayIcon::UpdateMenu() { PlatformUpdateMenu(); }
void TrayIcon::Hide() { if (m_visible) { m_visible = false; PlatformHide(); } }
bool TrayIcon::IsVisible() const { return PlatformIsVisible(); }
void TrayIcon::SetTooltip(const std::string& s) { PlatformSetTooltip(s); }
void TrayIcon::SetIcon(const std::string& s) { PlatformSetIcon(s); }
void TrayIcon::SetMenu(const std::vector<TrayMenuItem>& items) { m_menuItems = items; }
void TrayIcon::PollEvents() {
    TrayEvent ev;
    while (PopEvent(ev)) { if (m_callback) m_callback(ev); }
}
void TrayIcon::PushEvent(TrayEvent ev) {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    m_eventQueue.push_back(ev);
}
bool TrayIcon::PopEvent(TrayEvent& ev) {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    if (m_eventQueue.empty()) return false;
    ev = m_eventQueue.front(); m_eventQueue.pop_front(); return true;
}
void TrayIcon::Process() { PollEvents(); }

} // namespace volt
#endif
