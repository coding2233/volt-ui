#if defined(__linux__) && !defined(__ANDROID__)
#include "volt-ui/TrayIcon.h"
#include "dbus_mini.h"
#include <cstring>
#include <cmath>
#include <unistd.h>

namespace volt {
using namespace dbus_mini;

#if __has_include(<X11/Xlib.h>) && __has_include(<X11/Xatom.h>) && __has_include(<X11/Xutil.h>)
#define HAS_X11 1
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#undef None
#undef Bool
#undef Status
#endif

struct TrayIcon::PlatformData {
    int iconWidth = 16; int iconHeight = 16;
    std::vector<uint8_t> iconData;
    std::string tooltip;
    std::thread eventThread;
    std::atomic<bool> running{false};
    bool isSni = false;
    bool backendVisible = false;

    dbus_mini::Connection dbusConn;
    std::string sniServiceName;
    std::string sniObjectPath;

#ifdef HAS_X11
    ::Display* x11display = nullptr;
    ::Window x11window = 0;
    int x11screen = 0;
    bool x11docked = false;
#endif
};

TrayIcon::TrayIcon()  { m_data = std::make_unique<PlatformData>(); }
TrayIcon::~TrayIcon() { Hide(); }
TrayIcon::Ptr TrayIcon::Create() { return std::make_shared<TrayIcon>(); }

bool TrayIcon::PlatformInit(const std::string&, const std::string& tooltip) {
    auto& d = *m_data;
    d.tooltip = tooltip;
    d.iconWidth = 16; d.iconHeight = 16;
    d.iconData.resize(16*16*4, 0);
    for (int y=0; y<16; y++) for (int x=0; x<16; x++) {
        int idx=(y*16+x)*4;
        d.iconData[idx+0]=50; d.iconData[idx+1]=130; d.iconData[idx+2]=200; d.iconData[idx+3]=255;
    }
    return true;
}

bool TrayIcon::PlatformInitFromData(const uint8_t* rgba, int w, int h, const std::string& tooltip) {
    auto& d = *m_data;
    d.tooltip = tooltip;
    d.iconWidth = w; d.iconHeight = h;
    d.iconData.assign(rgba, rgba + w * h * 4);
    return true;
}

// ============ SNI via D-Bus ============
void TrayIcon::PlatformShow() {
    auto& d = *m_data;
    bool sniOk = false;

    // Try SNI first (Wayland), then X11
    if (d.dbusConn.connect()) {
        std::string bn="org.freedesktop.DBus", bp="/org/freedesktop/DBus", bi="org.freedesktop.DBus";
        Writer hw; hw << std::string("org.kde.StatusNotifierWatcher");
        if (d.dbusConn.sendMethodCall(bn,bp,bi,"NameHasOwner",{hw.buf.begin(),hw.buf.end()},"su")) {
            std::vector<uint8_t> reply;
            if (d.dbusConn.readMessage(reply)) {
                std::string err; std::vector<uint8_t> rb;
                if (Connection::parseReply(reply,err,rb)) {
                    Reader rr(rb.data(),rb.size());
                    if (rr.rBool()) {
                        char sname[128];
                        snprintf(sname,sizeof(sname),"org.transflint.Tray-%d-1",(int)getpid());
                        d.sniServiceName=sname; d.sniObjectPath="/StatusNotifierItem";

                        Writer nw; nw << std::string(sname) << uint32_t(0);
                        if (d.dbusConn.sendMethodCall(bn,bp,bi,"RequestName",{nw.buf.begin(),nw.buf.end()},"su")) {
                            if (d.dbusConn.readMessage(reply) && Connection::parseReply(reply,err,rb)) {
                                Reader nr(rb.data(),rb.size());
                                uint32_t r=nr.r32();
                                if (r==1||r==4) {
                                    Writer rw; rw << sname;
                                    d.dbusConn.sendMethodCall("org.kde.StatusNotifierWatcher","/StatusNotifierWatcher",
                                        "org.kde.StatusNotifierWatcher","RegisterStatusNotifierItem",{rw.buf.begin(),rw.buf.end()},"s");

                                    { Writer pw; pw << std::string("Active");
                                      d.dbusConn.sendSignal(d.sniObjectPath,"org.kde.StatusNotifierItem","NewStatus","s",{pw.buf.begin(),pw.buf.end()}); }
                                    if (!d.tooltip.empty()) {
                                        Writer tw; tw << d.tooltip;
                                        d.dbusConn.sendSignal(d.sniObjectPath,"org.kde.StatusNotifierItem","NewTitle","s",{tw.buf.begin(),tw.buf.end()}); }
                                    if (!d.iconData.empty()) {
                                        Writer iw; uint32_t w=d.iconWidth,h=d.iconHeight;
                                        iw.u32(1); iw.pad8(); iw.u32(w); iw.u32(h); iw.u32(w*h*4);
                                        for (int y=0; y<(int)h; y++) for (int x=0; x<(int)w; x++) {
                                            int idx=(y*(int)w+x)*4;
                                            iw.buf.push_back(d.iconData[idx+3]); iw.buf.push_back(d.iconData[idx+0]);
                                            iw.buf.push_back(d.iconData[idx+1]); iw.buf.push_back(d.iconData[idx+2]);
                                        }
                                        d.dbusConn.sendSignal(d.sniObjectPath,"org.kde.StatusNotifierItem","NewIcon","a(iiay)",{iw.buf.begin(),iw.buf.end()}); }

                                    d.isSni=true; d.backendVisible=true; sniOk=true;
                                    d.running=true;
                                    auto tray=this;
                                    d.eventThread = std::thread([tray,&d](){
                                        while(d.running){ std::vector<uint8_t> msg;
                                            if(d.dbusConn.readMessage(msg) && msg.size()>1 && msg[1]==1) {
                                                uint32_t fLen = Connection::parseHeaderFieldLen(msg.data()+12);
                                                std::string member; size_t pos=16, end=pos+fLen;
                                                while(pos<end && pos+2<=msg.size()){ uint8_t fc=msg[pos++]; uint8_t sig=msg[pos++];
                                                    if(fc==3){ Reader r(msg.data()+pos,msg.size()-pos); member=r.rstr(); break; }
                                                    if(sig=='s'||sig=='o'){ Reader r(msg.data()+pos,msg.size()-pos); r.rstr(); pos=r.pos; }
                                                    else if(sig=='u') pos+=4; }
                                                if(!member.empty()){ TrayEvent ev;
                                                    if(member=="Activate") ev.type=TrayEventType::LeftClick;
                                                    else if(member=="SecondaryActivate") ev.type=TrayEventType::DoubleClick;
                                                    else if(member=="ContextMenu") ev.type=TrayEventType::RightClick;
                                                    if(ev.type!=TrayEventType::None) tray->PushEvent(ev);
                                                    Message replyMsg(2,"","/","","",""); std::vector<uint8_t> w; replyMsg.build(w);
                                                    d.dbusConn.sendMessage(w);
                                                }
                                            }
                                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                        }
                                    });
                                }
                            }
                        }
                    }
                }
            }
        }
        if (!sniOk) d.dbusConn.disconnect();
    }

    if (sniOk) return;

    // ===== X11 XEmbed tray =====
#ifdef HAS_X11
    {
        auto errHandler = [](::Display*,::XErrorEvent*){ return 0; };
        XSetErrorHandler(errHandler);
        d.x11display = XOpenDisplay(nullptr);
        if (!d.x11display) return;
        d.x11screen = DefaultScreen(d.x11display);
        Atom traySel = XInternAtom(d.x11display, "_NET_SYSTEM_TRAY_S0", False);
        Window trayOwner = XGetSelectionOwner(d.x11display, traySel);
        if (trayOwner == None) { XCloseDisplay(d.x11display); d.x11display = nullptr; return; }
        d.x11screen = DefaultScreen(d.x11display);
        Atom traySel = XInternAtom(d.x11display, "_NET_SYSTEM_TRAY_S0", False);
        Window trayOwner = XGetSelectionOwner(d.x11display, traySel);
        if (trayOwner == None) {
            XCloseDisplay(d.x11display); d.x11display = nullptr; return;
        }

        d.x11window = XCreateSimpleWindow(d.x11display,
            RootWindow(d.x11display, d.x11screen), 0, 0,
            d.iconWidth, d.iconHeight, 0, 0, 0x000000);
        XSelectInput(d.x11display, d.x11window,
                     ExposureMask | ButtonPressMask | ButtonReleaseMask | StructureNotifyMask);
        XClassHint ch; ch.res_name=const_cast<char*>("transflint");
        ch.res_class=const_cast<char*>("TransFlint");
        XSetClassHint(d.x11display, d.x11window, &ch);

        XEvent ev; memset(&ev,0,sizeof(ev));
        ev.xclient.type=ClientMessage; ev.xclient.window=trayOwner;
        ev.xclient.message_type = XInternAtom(d.x11display, "_NET_SYSTEM_TRAY_OPCODE", False);
        ev.xclient.format=32;
        ev.xclient.data.l[0]=CurrentTime;
        ev.xclient.data.l[1]=SYSTEM_TRAY_REQUEST_DOCK;
        ev.xclient.data.l[2]=d.x11window;
        XSendEvent(d.x11display, trayOwner, False, NoEventMask, &ev);
        XSync(d.x11display, False);
        d.x11docked=true; d.backendVisible=true;

        // Paint first frame
        if (!d.iconData.empty()) {
            int w=d.iconWidth, h=d.iconHeight;
            GC gc = XCreateGC(d.x11display, d.x11window, 0, nullptr);
            XImage* img = XCreateImage(d.x11display,
                DefaultVisual(d.x11display, d.x11screen), 24, ZPixmap, 0, nullptr, w, h, 32, 0);
            if (img) {
                img->data = new char[w*h*4];
                for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
                    int si=(y*w+x)*4, di=(y*w+x)*4;
                    img->data[di+0]=d.iconData[si+2]; img->data[di+1]=d.iconData[si+1];
                    img->data[di+2]=d.iconData[si+0]; img->data[di+3]=0;
                }
                XPutImage(d.x11display, d.x11window, gc, img, 0, 0, 0, 0, w, h);
                delete[] img->data; img->data=nullptr; XDestroyImage(img);
            }
            XFreeGC(d.x11display, gc);
        }

        d.running=true;
        auto tray=this;
        d.eventThread = std::thread([tray,&d](){
            XEvent event;
            while(d.running && d.x11display) {
                while(d.x11display && XPending(d.x11display)>0) {
                    XNextEvent(d.x11display, &event);
                    if(event.type==Expose && event.xexpose.count==0) {
                        if(!d.iconData.empty()) {
                            int w=d.iconWidth,h=d.iconHeight;
                            GC gc = XCreateGC(d.x11display,d.x11window,0,nullptr);
                            XImage* img = XCreateImage(d.x11display,
                                DefaultVisual(d.x11display,d.x11screen),24,ZPixmap,0,nullptr,w,h,32,0);
                            if(img) {
                                img->data=new char[w*h*4];
                                for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
                                    int si=(y*w+x)*4,di=(y*w+x)*4;
                                    img->data[di+0]=d.iconData[si+2];
                                    img->data[di+1]=d.iconData[si+1];
                                    img->data[di+2]=d.iconData[si+0]; img->data[di+3]=0;
                                }
                                XPutImage(d.x11display,d.x11window,gc,img,0,0,0,0,w,h);
                                delete[] img->data; img->data=nullptr; XDestroyImage(img);
                            }
                            XFreeGC(d.x11display,gc);
                        }
                    } else if(event.type==ButtonPress) {
                        TrayEvent ev;
                        if(event.xbutton.button==Button1) ev.type=TrayEventType::LeftClick;
                        else if(event.xbutton.button==Button3) ev.type=TrayEventType::RightClick;
                        ev.x=event.xbutton.x_root; ev.y=event.xbutton.y_root;
                        tray->PushEvent(ev);
                    } else if(event.type==DestroyNotify) d.running=false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
    }
#endif
}

void TrayIcon::PlatformHide() {
    auto& d = *m_data;
    d.backendVisible = false;
    d.running = false;
    if (d.eventThread.joinable()) d.eventThread.join();
    if (d.isSni) d.dbusConn.disconnect();
#ifdef HAS_X11
    if (d.x11window) { XDestroyWindow(d.x11display, d.x11window); d.x11window=0; }
    if (d.x11display) { XCloseDisplay(d.x11display); d.x11display=nullptr; }
    d.x11docked=false;
#endif
    d.isSni=false;
}

void TrayIcon::PlatformUpdateMenu() {
    auto& d = *m_data;
    if (d.isSni) {
        Writer mw; mw << int32_t(0) << int32_t(0);
        d.dbusConn.sendSignal(d.sniObjectPath,"org.kde.StatusNotifierItem","ContextMenu","(ii)",
            {mw.buf.begin(),mw.buf.end()});
    }
}

void TrayIcon::PlatformSetTooltip(const std::string& tooltip) {
    auto& d = *m_data;
    d.tooltip = tooltip;
    if (d.isSni) {
        Writer tw; tw << tooltip;
        d.dbusConn.sendSignal(d.sniObjectPath,"org.kde.StatusNotifierItem","NewTitle","s",
            {tw.buf.begin(),tw.buf.end()});
    }
#ifdef HAS_X11
    if (d.x11display && d.x11window) XStoreName(d.x11display, d.x11window, tooltip.c_str());
#endif
}

void TrayIcon::PlatformSetIcon(const std::string&) {
    // Re-init with default icon
    auto& d = *m_data;
    d.iconWidth=16; d.iconHeight=16;
    d.iconData.resize(16*16*4,0);
    for(int y=0;y<16;y++) for(int x=0;x<16;x++) {
        int idx=(y*16+x)*4;
        d.iconData[idx+0]=50; d.iconData[idx+1]=130; d.iconData[idx+2]=200; d.iconData[idx+3]=255;
    }
    if (d.isSni) {
        Writer iw; uint32_t w=d.iconWidth,h=d.iconHeight;
        iw.u32(1); iw.pad8(); iw.u32(w); iw.u32(h); iw.u32(w*h*4);
        for(int y=0;y<(int)h;y++) for(int x=0;x<(int)w;x++) {
            int idx=(y*(int)w+x)*4;
            iw.buf.push_back(d.iconData[idx+3]); iw.buf.push_back(d.iconData[idx+0]);
            iw.buf.push_back(d.iconData[idx+1]); iw.buf.push_back(d.iconData[idx+2]);
        }
        d.dbusConn.sendSignal(d.sniObjectPath,"org.kde.StatusNotifierItem","NewIcon","a(iiay)",
            {iw.buf.begin(),iw.buf.end()});
    }
#ifdef HAS_X11
    if (d.x11display && d.x11window) {
        XClearArea(d.x11display,d.x11window,0,0,0,0,True);
        if(!d.iconData.empty()) {
            int w=d.iconWidth,h=d.iconHeight;
            GC gc = XCreateGC(d.x11display,d.x11window,0,nullptr);
            XImage* img = XCreateImage(d.x11display,
                DefaultVisual(d.x11display,d.x11screen),24,ZPixmap,0,nullptr,w,h,32,0);
            if(img) {
                img->data=new char[w*h*4];
                for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
                    int si=(y*w+x)*4,di=(y*w+x)*4;
                    img->data[di+0]=d.iconData[si+2]; img->data[di+1]=d.iconData[si+1];
                    img->data[di+2]=d.iconData[si+0]; img->data[di+3]=0;
                }
                XPutImage(d.x11display,d.x11window,gc,img,0,0,0,0,w,h);
                delete[] img->data; img->data=nullptr; XDestroyImage(img);
            }
            XFreeGC(d.x11display,gc);
        }
    }
#endif
}

bool TrayIcon::PlatformIsVisible() const {
    return m_data->backendVisible;
}

// Public wrappers
bool TrayIcon::Init(const std::string& p, const std::string& t) { return PlatformInit(p, t); }
bool TrayIcon::InitFromData(const uint8_t* rgba, int w, int h, const std::string& t) { return PlatformInitFromData(rgba, w, h, t); }
void TrayIcon::Show()          { if(!m_visible) { m_visible=true;  PlatformShow(); } }
void TrayIcon::UpdateMenu()    { PlatformUpdateMenu(); }
void TrayIcon::Hide()          { if(m_visible)  { m_visible=false; PlatformHide(); } }
bool TrayIcon::IsVisible() const { return PlatformIsVisible(); }
void TrayIcon::SetTooltip(const std::string& s) { PlatformSetTooltip(s); }
void TrayIcon::SetIcon(const std::string& s)    { PlatformSetIcon(s); }
void TrayIcon::SetMenu(const std::vector<TrayMenuItem>& items) { m_menuItems = items; }

void TrayIcon::PollEvents() { TrayEvent ev; while(PopEvent(ev)) { if(m_callback) m_callback(ev); } }
void TrayIcon::PushEvent(TrayEvent ev) { std::lock_guard<std::mutex> lk(m_eventMutex); m_eventQueue.push_back(ev); }
bool TrayIcon::PopEvent(TrayEvent& ev) {
    std::lock_guard<std::mutex> lk(m_eventMutex);
    if(m_eventQueue.empty()) return false;
    ev=m_eventQueue.front(); m_eventQueue.pop_front(); return true;
}
void TrayIcon::Process() { PollEvents(); }

} // namespace volt

#elif !defined(_WIN32) && !defined(__APPLE__)
#include "volt-ui/TrayIcon.h"
namespace volt {
struct TrayIcon::PlatformData { int dummy=0; };
TrayIcon::TrayIcon() : m_data(std::make_unique<PlatformData>()) {}
TrayIcon::~TrayIcon() = default;
TrayIcon::Ptr TrayIcon::Create() { return std::make_shared<TrayIcon>(); }
bool TrayIcon::PlatformInit(const std::string&,const std::string&) { return true; }
bool TrayIcon::PlatformInitFromData(const uint8_t*,int,int,const std::string&) { return true; }
void TrayIcon::PlatformShow() {}
void TrayIcon::PlatformHide() {}
void TrayIcon::PlatformUpdateMenu() {}
void TrayIcon::PlatformSetTooltip(const std::string&) {}
void TrayIcon::PlatformSetIcon(const std::string&) {}
bool TrayIcon::PlatformIsVisible() const { return false; }
bool TrayIcon::Init(const std::string& p, const std::string& t) { return PlatformInit(p, t); }
bool TrayIcon::InitFromData(const uint8_t* rgba, int w, int h, const std::string& t) { return PlatformInitFromData(rgba, w, h, t); }
void TrayIcon::Show() { m_visible=true; PlatformShow(); }
void TrayIcon::UpdateMenu() { PlatformUpdateMenu(); }
void TrayIcon::Hide() { m_visible=false; PlatformHide(); }
bool TrayIcon::IsVisible() const { return PlatformIsVisible(); }
void TrayIcon::SetTooltip(const std::string& s) { PlatformSetTooltip(s); }
void TrayIcon::SetIcon(const std::string& s) { PlatformSetIcon(s); }
void TrayIcon::SetMenu(const std::vector<TrayMenuItem>& items) { m_menuItems = items; }
void TrayIcon::PollEvents() {}
void TrayIcon::PushEvent(TrayEvent) {}
bool TrayIcon::PopEvent(TrayEvent&) { return false; }
void TrayIcon::Process() {}
} // namespace volt
#endif
