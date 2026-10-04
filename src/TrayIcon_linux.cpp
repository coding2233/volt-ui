#if defined(__linux__) && !defined(__ANDROID__)
#include "volt-ui/TrayIcon.h"
#include "dbus_mini.h"
#include <cstring>
#include <cmath>
#include <mutex>
#include <thread>
#include <unistd.h>

#if __has_include(<X11/Xlib.h>) && __has_include(<X11/Xatom.h>) && __has_include(<X11/Xutil.h>)
#define HAS_X11 1
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#undef None
#undef Bool
#undef Status
#ifndef SYSTEM_TRAY_REQUEST_DOCK
#define SYSTEM_TRAY_REQUEST_DOCK 0
#endif
#endif

namespace volt {
using namespace dbus_mini;

namespace {

// ---- small marshalling helpers for a{sv} / layout / icon ------------------
void DictEntryS(Writer& w, const std::string& k, const std::string& v) {
    w.dictEntryBegin(); w.str(k); w.variantBegin("s"); w.str(v);
}
void DictEntryO(Writer& w, const std::string& k, const std::string& v) {
    w.dictEntryBegin(); w.str(k); w.variantBegin("o"); w.str(v);
}
void DictEntryB(Writer& w, const std::string& k, bool v) {
    w.dictEntryBegin(); w.str(k); w.variantBegin("b"); w.bool_(v);
}
void DictEntryI(Writer& w, const std::string& k, int32_t v) {
    w.dictEntryBegin(); w.str(k); w.variantBegin("i"); w.s32(v);
}
void DictEntryU(Writer& w, const std::string& k, uint32 v) {
    w.dictEntryBegin(); w.str(k); w.variantBegin("u"); w.u32(v);
}
void DictEntryAS(Writer& w, const std::string& k, const std::vector<std::string>& vals) {
    w.dictEntryBegin(); w.str(k); w.variantBegin("as");
    size_t a = w.arrayBegin(4);
    for (auto& s : vals) w.str(s);
    w.endArray(a);
}
// SNI IconPixmap: a(iiay) of (width, height, ARGB32) — pixmap is RGBA, so the
// wire order is A,R,G,B.
void WriteIconPixmap(Writer& w, int width, int height, const std::vector<uint8_t>& rgba) {
    size_t a = w.arrayBegin(8);
    w.pad8();
    w.s32(width);
    w.s32(height);
    size_t bytes = w.arrayBegin(1);
    int npix = width * height;
    for (int i = 0; i < npix && (size_t)(i * 4 + 3) < rgba.size(); ++i) {
        w.buf.push_back(rgba[i * 4 + 3]);
        w.buf.push_back(rgba[i * 4 + 0]);
        w.buf.push_back(rgba[i * 4 + 1]);
        w.buf.push_back(rgba[i * 4 + 2]);
    }
    w.endArray(bytes);
    w.endArray(a);
}
void DictEntryIconPixmap(Writer& w, const std::string& k, int width, int height,
                         const std::vector<uint8_t>& rgba) {
    w.dictEntryBegin(); w.str(k); w.variantBegin("a(iiay)");
    WriteIconPixmap(w, width, height, rgba);
}

// ---- com.canonical.dbusmenu layout ---------------------------------------
void WriteMenuItem(Writer& w, int id, const TrayMenuItem& it) {
    w.variantBegin("(ia{sv}av)");
    w.pad8();
    w.s32(id);
    {
        size_t props = w.arrayBegin(8);
        if (it.separator) {
            DictEntryS(w, "type", "separator");
        } else {
            DictEntryS(w, "label", it.label);
            DictEntryB(w, "enabled", it.enabled);
            DictEntryB(w, "visible", true);
            if (it.checked) {
                DictEntryS(w, "toggle-type", "checkmark");
                DictEntryI(w, "toggle-state", 1);
            }
        }
        w.endArray(props);
    }
    {
        size_t children = w.arrayBegin(1);   // flat menu: no submenus
        w.endArray(children);
    }
}
void WriteRootLayout(Writer& w, const std::vector<TrayMenuItem>& items) {
    w.pad8();
    w.s32(0);
    {
        size_t props = w.arrayBegin(8);
        DictEntryS(w, "children-display", "submenu");
        w.endArray(props);
    }
    {
        size_t children = w.arrayBegin(1);
        for (size_t i = 0; i < items.size(); ++i)
            WriteMenuItem(w, (int)i + 1, items[i]);
        w.endArray(children);
    }
}

const char* kIntrospectXml =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    "  <interface name=\"org.kde.StatusNotifierItem\">\n"
    "    <property name=\"Category\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"Id\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"Title\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"Status\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"IconName\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"IconPixmap\" type=\"a(iiay)\" access=\"read\"/>\n"
    "    <property name=\"IconThemePath\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"Menu\" type=\"o\" access=\"read\"/>\n"
    "    <property name=\"ItemIsMenu\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"WindowId\" type=\"u\" access=\"read\"/>\n"
    "  </interface>\n"
    "  <interface name=\"org.freedesktop.DBus.Properties\">\n"
    "    <method name=\"Get\"><arg name=\"interface\" type=\"s\" direction=\"in\"/>"
    "<arg name=\"property\" type=\"s\" direction=\"in\"/><arg name=\"value\" type=\"v\" direction=\"out\"/></method>\n"
    "    <method name=\"GetAll\"><arg name=\"interface\" type=\"s\" direction=\"in\"/>"
    "<arg name=\"properties\" type=\"a{sv}\" direction=\"out\"/></method>\n"
    "  </interface>\n"
    "</node>\n";

} // namespace

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
    std::string menuPath = "/MenuBar";
    uint32 revision = 1;
    std::mutex sendMutex;

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

// StatusNotifierItem properties as an a{sv} body (used by Properties.GetAll).
static void AppendSniProperties(Writer& w, const std::string& tooltip,
                                const std::string& menuPath, int iconW, int iconH,
                                const std::vector<uint8_t>& icon) {
    size_t arr = w.arrayBegin(8);       // a{sv} needs its array length prefix
    DictEntryS(w, "Category", "ApplicationStatus");
    DictEntryS(w, "Id", "codebee");
    DictEntryS(w, "Title", tooltip);
    DictEntryS(w, "Status", "Active");
    DictEntryS(w, "IconName", "");
    DictEntryS(w, "IconThemePath", "");
    DictEntryO(w, "Menu", menuPath);
    DictEntryB(w, "ItemIsMenu", false);
    DictEntryU(w, "WindowId", 0);
    DictEntryIconPixmap(w, "IconPixmap", iconW, iconH, icon);
    w.endArray(arr);
}

// ---------------------------------------------------------------------------
// SNI via D-Bus (com.canonical.dbusmenu for the menu)
// ---------------------------------------------------------------------------
void TrayIcon::PlatformShow() {
    auto& d = *m_data;
    bool sniOk = false;

    if (d.dbusConn.connect()) {
        std::string bn="org.freedesktop.DBus", bp="/org/freedesktop/DBus", bi="org.freedesktop.DBus";
        Writer hw; hw << std::string("org.kde.StatusNotifierWatcher");
        if (d.dbusConn.sendMethodCall(bn,bp,bi,"NameHasOwner",{hw.buf.begin(),hw.buf.end()},"s")) {
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
                                    Writer rw; rw.str(sname);   // char[] must not bind the bool overload
                                    d.dbusConn.sendMethodCall("org.kde.StatusNotifierWatcher","/StatusNotifierWatcher",
                                        "org.kde.StatusNotifierWatcher","RegisterStatusNotifierItem",{rw.buf.begin(),rw.buf.end()},"s");

                                    { Writer pw; pw << std::string("Active");
                                      d.dbusConn.sendSignal(d.sniObjectPath,"org.kde.StatusNotifierItem","NewStatus","s",{pw.buf.begin(),pw.buf.end()}); }
                                    if (!d.tooltip.empty()) {
                                        Writer tw; tw << d.tooltip;
                                        d.dbusConn.sendSignal(d.sniObjectPath,"org.kde.StatusNotifierItem","NewTitle","s",{tw.buf.begin(),tw.buf.end()}); }
                                    // NewIcon with no args; the host reads the IconPixmap property.
                                    d.dbusConn.sendSignal(d.sniObjectPath,"org.kde.StatusNotifierItem","NewIcon","",{});

                                    d.isSni=true; d.backendVisible=true; sniOk=true;
                                    d.running=true;
                                    auto tray=this;
                                    auto& dd = d;
                                    d.eventThread = std::thread([tray,&dd](){
                                        while(dd.running){
                                            std::vector<uint8_t> msg;
                                            if (dd.dbusConn.readMessage(msg)) {
                                                ParsedMessage pm;
                                                if (Connection::parseMessage(msg, pm) && pm.type == 1) {
                                                    std::vector<uint8_t> out; std::string rsig;
                                                    bool handled = tray->HandleDBusCall(pm, out, rsig);
                                                    if (handled) {
                                                        std::lock_guard<std::mutex> lk(dd.sendMutex);
                                                        if (rsig.empty())
                                                            dd.dbusConn.sendMethodReturn(pm.sender, pm.serial, pm.path, pm.iface, pm.member, "", out);
                                                        else
                                                            dd.dbusConn.sendMethodReturn(pm.sender, pm.serial, pm.path, pm.iface, pm.member, rsig, out);
                                                    }
                                                }
                                            } else {
                                                break;
                                            }
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
        Atom traySel = XInternAtom(d.x11display, "_NET_SYSTEM_TRAY_S0", 0);
        Window trayOwner = XGetSelectionOwner(d.x11display, traySel);
        if (trayOwner == 0) { XCloseDisplay(d.x11display); d.x11display = nullptr; return; }

        d.x11window = XCreateSimpleWindow(d.x11display,
            RootWindow(d.x11display, d.x11screen), 0, 0,
            d.iconWidth, d.iconHeight, 0, 0, 0x000000);
        XSelectInput(d.x11display, d.x11window,
                     ExposureMask | ButtonPressMask | ButtonReleaseMask | StructureNotifyMask);
        XClassHint ch; ch.res_name=const_cast<char*>("codebee");
        ch.res_class=const_cast<char*>("CodeBee");
        XSetClassHint(d.x11display, d.x11window, &ch);

        XEvent ev; memset(&ev,0,sizeof(ev));
        ev.xclient.type=ClientMessage; ev.xclient.window=trayOwner;
        ev.xclient.message_type = XInternAtom(d.x11display, "_NET_SYSTEM_TRAY_OPCODE", 0);
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = CurrentTime;
        ev.xclient.data.l[1] = SYSTEM_TRAY_REQUEST_DOCK;
        ev.xclient.data.l[2] = d.x11window;
        XSendEvent(d.x11display, trayOwner, 0, 0, &ev);
        XSync(d.x11display, 0);
        d.x11docked=true; d.backendVisible=true;

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
                    if(event.type==ButtonPress) {
                        TrayEvent ev;
                        if(event.xbutton.button==Button1) ev.type=TrayEventType::LeftClick;
                        else if(event.xbutton.button==3) ev.type=TrayEventType::RightClick;
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

// Handle one incoming method call on the SNI or menu object. Returns true when
// a reply should be sent (rsig/out hold its signature/body).
bool TrayIcon::HandleDBusCall(const dbus_mini::ParsedMessage& pm,
                              std::vector<uint8_t>& out, std::string& rsig) {
    auto& d = *m_data;

    // ---- org.freedesktop.DBus.Introspectable ----
    if (pm.iface == "org.freedesktop.DBus.Introspectable" && pm.member == "Introspect") {
        Writer w; w.str(kIntrospectXml);
        out = w.buf; rsig = "s";
        return true;
    }

    // ---- org.freedesktop.DBus.Properties.Get/GetAll ----
    if (pm.iface == "org.freedesktop.DBus.Properties" &&
        (pm.member == "Get" || pm.member == "GetAll")) {
        Reader r(pm.body.data(), pm.body.size());
        std::string iface = r.rstr();
        if (pm.member == "Get") {
            std::string prop = r.rstr();
            Writer w;
            if (iface == "org.kde.StatusNotifierItem") {
                if (prop == "Category")       { w.variantBegin("s"); w.str("ApplicationStatus"); }
                else if (prop == "Id")        { w.variantBegin("s"); w.str("codebee"); }
                else if (prop == "Title")     { w.variantBegin("s"); w.str(d.tooltip); }
                else if (prop == "Status")    { w.variantBegin("s"); w.str("Active"); }
                else if (prop == "IconName")  { w.variantBegin("s"); w.str(""); }
                else if (prop == "IconThemePath") { w.variantBegin("s"); w.str(""); }
                else if (prop == "Menu")      { w.variantBegin("o"); w.str(d.menuPath); }
                else if (prop == "ItemIsMenu"){ w.variantBegin("b"); w.bool_(false); }
                else if (prop == "WindowId")  { w.variantBegin("u"); w.u32(0); }
                else if (prop == "IconPixmap"){ w.variantBegin("a(iiay)"); WriteIconPixmap(w, d.iconWidth, d.iconHeight, d.iconData); }
                else return false;
            } else if (iface == "com.canonical.dbusmenu") {
                if (prop == "Version")          { w.variantBegin("u"); w.u32(3); }
                else if (prop == "Status")      { w.variantBegin("s"); w.str("normal"); }
                else if (prop == "TextDirection"){ w.variantBegin("s"); w.str("ltr"); }
                else if (prop == "IconThemePath"){ w.variantBegin("as"); size_t a=w.arrayBegin(4); w.endArray(a); }
                else return false;
            } else {
                return false;
            }
            out = w.buf; rsig = "v";
            return true;
        } else {
            Writer w;
            if (iface == "org.kde.StatusNotifierItem") {
                AppendSniProperties(w, d.tooltip, d.menuPath, d.iconWidth, d.iconHeight, d.iconData);
            } else if (iface == "com.canonical.dbusmenu") {
                size_t arr = w.arrayBegin(8);
                DictEntryU(w, "Version", 3);
                DictEntryS(w, "Status", "normal");
                DictEntryS(w, "TextDirection", "ltr");
                DictEntryAS(w, "IconThemePath", {});
                w.endArray(arr);
            } else {
                return false;
            }
            out = w.buf; rsig = "a{sv}";
            return true;
        }
    }

    // ---- com.canonical.dbusmenu methods ----
    if (pm.iface == "com.canonical.dbusmenu") {
        Reader r(pm.body.data(), pm.body.size());
        Writer w;
        if (pm.member == "GetLayout") {
            r.rs32();                 // parentId (flat menu -> ignore)
            r.rs32();                 // recursionDepth
            w.u32(d.revision);
            WriteRootLayout(w, m_menuItems);
            out = w.buf; rsig = "u(ia{sv}av)";
            return true;
        }
        if (pm.member == "GetProperty") {
            int id = r.rs32();
            std::string name = r.rstr();
            const TrayMenuItem* it = (id >= 1 && id <= (int)m_menuItems.size())
                ? &m_menuItems[(size_t)id - 1] : nullptr;
            if (name == "label" && it)        { w.variantBegin("s"); w.str(it->label); }
            else if (name == "enabled" && it) { w.variantBegin("b"); w.bool_(it->enabled); }
            else if (name == "visible")       { w.variantBegin("b"); w.bool_(true); }
            else if (name == "type")          { w.variantBegin("s"); w.str(it && it->separator ? "separator" : "standard"); }
            else if (name == "children-display") { w.variantBegin("s"); w.str("submenu"); }
            else if (name == "toggle-state")  { w.variantBegin("i"); w.s32(it && it->checked ? 1 : 0); }
            else return false;
            out = w.buf; rsig = "v";
            return true;
        }
        if (pm.member == "GetGroupProperties") {
            uint32 nids = r.r32();
            std::vector<int> ids;
            for (uint32 i = 0; i < nids && i < 512; ++i) ids.push_back(r.rs32());
            // property name list is ignored; return the common set.
            size_t arr = w.arrayBegin(8);
            for (int id : ids) {
                w.pad8();
                w.s32(id);
                size_t props = w.arrayBegin(8);
                const TrayMenuItem* it = (id >= 1 && id <= (int)m_menuItems.size())
                    ? &m_menuItems[(size_t)id - 1] : nullptr;
                if (it) {
                    if (it->separator) DictEntryS(w, "type", "separator");
                    else {
                        DictEntryS(w, "label", it->label);
                        DictEntryB(w, "enabled", it->enabled);
                        DictEntryB(w, "visible", true);
                    }
                }
                w.endArray(props);
                w.pad8();
            }
            w.endArray(arr);
            out = w.buf; rsig = "a(ia{sv})";
            return true;
        }
        if (pm.member == "Event") {
            int id = r.rs32();
            std::string ev = r.rstr();
            if (ev == "clicked" && id >= 1 && id <= (int)m_menuItems.size()) {
                TrayEvent te; te.type = TrayEventType::MenuSelect;
                te.menuId = m_menuItems[(size_t)id - 1].id;
                PushEvent(te);
            }
            out.clear(); rsig = "";
            return true;
        }
        if (pm.member == "EventGroup") {
            Writer e; size_t a = e.arrayBegin(4); e.endArray(a);
            out = e.buf; rsig = "ai";
            return true;
        }
        if (pm.member == "AboutToShow") {
            Writer b; b.bool_(false);
            out = b.buf; rsig = "b";
            return true;
        }
        if (pm.member == "AboutToShowGroup") {
            Writer e; size_t a = e.arrayBegin(4); e.endArray(a);
            out = e.buf; rsig = "aiai";
            return true;
        }
    }
    return false;
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
        d.revision++;
        std::lock_guard<std::mutex> lk(d.sendMutex);
        Writer w; w.u32(d.revision); w.s32(0);
        d.dbusConn.sendSignal(d.menuPath, "com.canonical.dbusmenu", "LayoutUpdated", "ui",
                              {w.buf.begin(), w.buf.end()});
    }
}

void TrayIcon::PlatformSetTooltip(const std::string& tooltip) {
    auto& d = *m_data;
    d.tooltip = tooltip;
    if (d.isSni) {
        std::lock_guard<std::mutex> lk(d.sendMutex);
        Writer tw; tw << tooltip;
        d.dbusConn.sendSignal(d.sniObjectPath,"org.kde.StatusNotifierItem","NewTitle","s",
            {tw.buf.begin(),tw.buf.end()});
    }
#ifdef HAS_X11
    if (d.x11display && d.x11window) XStoreName(d.x11display, d.x11window, tooltip.c_str());
#endif
}

void TrayIcon::PlatformSetIcon(const std::string&) {
    auto& d = *m_data;
    d.iconWidth=16; d.iconHeight=16;
    d.iconData.resize(16*16*4,0);
    for(int y=0;y<16;y++) for(int x=0;x<16;x++) {
        int idx=(y*16+x)*4;
        d.iconData[idx+0]=50; d.iconData[idx+1]=130; d.iconData[idx+2]=200; d.iconData[idx+3]=255;
    }
    if (d.isSni) {
        std::lock_guard<std::mutex> lk(d.sendMutex);
        d.dbusConn.sendSignal(d.sniObjectPath,"org.kde.StatusNotifierItem","NewIcon","",{});
    }
#ifdef HAS_X11
    if (d.x11display && d.x11window) {
        XClearArea(d.x11display,d.x11window,0,0,0,0,True);
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
