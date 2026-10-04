#pragma once
// Minimal D-Bus wire codec + blocking client, enough for
// org.kde.StatusNotifierItem / com.canonical.dbusmenu. Little-endian only.
//
// Alignment rules (D-Bus spec): u32/i32/bool align 4, string/object-path
// align 4, struct/dict-entry align 8, variant aligns 1 but its contained
// value aligns to its own type, array data is preceded by a 4-byte byte
// length and padded to the element alignment.
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <map>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cerrno>

namespace volt {
namespace dbus_mini {

using uint8  = uint8_t;
using uint32 = uint32_t;
using int32  = int32_t;
using uint64 = uint64_t;

struct Writer {
    std::vector<uint8> buf;

    void pad(size_t a) { if (a > 1) while (buf.size() % a) buf.push_back(0); }
    void pad8() { pad(8); }
    void u32(uint32 v) { pad(4); buf.push_back(v & 0xFF); buf.push_back((v >> 8) & 0xFF); buf.push_back((v >> 16) & 0xFF); buf.push_back((v >> 24) & 0xFF); }
    void s32(int32 v) { u32((uint32)v); }
    void byte(uint8 v) { buf.push_back(v); }
    void str(const std::string& s) { pad(4); u32((uint32)s.size()); for (char c : s) buf.push_back((uint8)c); buf.push_back(0); }
    void sig(const std::string& s) { byte((uint8)s.size()); for (char c : s) buf.push_back((uint8)c); buf.push_back(0); }
    void bool_(bool v) { u32(v ? 1 : 0); }
    void dictEntryBegin() { pad(8); }
    void variantBegin(const std::string& s) { sig(s); }

    // Array length placeholder: pad to 4, write 0, pad to the element
    // alignment, then element bytes follow. endArray patches the byte length
    // measured from the first element (not from the length field).
    std::vector<std::pair<size_t, size_t>> arrayData;   // (dataStart, elemAlign)
    size_t arrayBegin(size_t elemAlign) {
        pad(4);
        size_t at = buf.size();
        u32(0);
        pad(elemAlign);
        arrayData.push_back({ buf.size(), elemAlign });
        return at;
    }
    void endArray(size_t at) {
        size_t dataStart = arrayData.back().first;
        size_t elemAlign = arrayData.back().second;
        arrayData.pop_back();
        uint32 len = (uint32)(buf.size() - dataStart);
        buf[at + 0] = len & 0xFF; buf[at + 1] = (len >> 8) & 0xFF;
        buf[at + 2] = (len >> 16) & 0xFF; buf[at + 3] = (len >> 24) & 0xFF;
    }
};

struct Reader {
    const uint8* data;
    size_t len;
    size_t pos = 0;
    Reader(const uint8* d, size_t l) : data(d), len(l) {}

    void pad(size_t a) { if (a > 1) while (pos % a && pos < len) pos++; }
    uint8 r8() { return pos < len ? data[pos++] : 0; }
    uint32 r32() { pad(4); uint32 v = 0; for (int i = 0; i < 4 && pos < len; i++) v |= (uint32)data[pos++] << (i * 8); return v; }
    int32 rs32() { return (int32)r32(); }
    std::string rstr() { pad(4); uint32 n = r32(); std::string s((const char*)data + (pos < len ? pos : 0), n); pos += n + 1; return s; }
    std::string rsig() { uint8 n = r8(); std::string s((const char*)data + (pos < len ? pos : 0), n); pos += n + 1; return s; }
    bool rBool() { return r32() != 0; }
};

class Message {
public:
    std::vector<uint8> body;
    uint32 serial = 0;
    uint32 replySerial = 0;
    uint8 type = 1;                 // 1 method_call, 2 method_return, 3 error, 4 signal
    std::string dest, path, iface, member, sig;

    static uint32 nextSerial() { static uint32 s = 1; return s++; }

    Message(uint8 msgType, const std::string& d, const std::string& p,
            const std::string& i, const std::string& m, const std::string& s = "")
        : type(msgType), dest(d), path(p), iface(i), member(m), sig(s) {
        serial = nextSerial();
    }

    // Header field descriptors, ordered as written.
    struct Field { uint8 code; char type; std::string sval; uint32 uval = 0; };

    void build(std::vector<uint8>& out) const {
        std::vector<Field> fields;
        if (!path.empty())   fields.push_back({ 1, 'o', path, 0 });
        if (!iface.empty())  fields.push_back({ 2, 's', iface, 0 });
        if (!member.empty()) fields.push_back({ 3, 's', member, 0 });
        if (replySerial)     fields.push_back({ 5, 'u', {}, replySerial });
        if (!dest.empty())   fields.push_back({ 6, 's', dest, 0 });
        if (!sig.empty())    fields.push_back({ 8, 'g', sig, 0 });

        // dbus-broker validates the field array length exactly: pad between
        // fields to 8, but do NOT count padding after the last field.
        Writer f;
        for (size_t i = 0; i < fields.size(); ++i) {
            const Field& fl = fields[i];
            f.byte(fl.code); f.byte(1); f.byte((uint8)fl.type); f.byte(0);
            if (fl.type == 'u') f.u32(fl.uval);
            else if (fl.type == 'g') f.sig(fl.sval);
            else f.str(fl.sval);
            if (i + 1 < fields.size()) f.pad8();
        }

        Writer h;
        h.byte('l'); h.byte(type); h.byte(0); h.byte(1);
        h.u32((uint32)body.size());
        h.u32(serial);
        h.u32((uint32)f.buf.size());

        out = h.buf;
        out.insert(out.end(), f.buf.begin(), f.buf.end());
        while (out.size() % 8) out.push_back(0);   // align the body
        out.insert(out.end(), body.begin(), body.end());
#ifdef DBUS_MINI_DEBUG
        fprintf(stderr, "[dbus] send type=%d member=%s bytes=%zu:", type, member.c_str(), out.size());
        for (size_t i = 0; i < out.size() && i < 96; i++) fprintf(stderr, " %02x", out[i]);
        fprintf(stderr, "\n");
#endif
    }
};

struct ParsedMessage {
    bool valid = false;
    uint8 type = 0;
    uint32 serial = 0;
    uint32 replySerial = 0;
    std::string path, iface, member, sender, destination, signature;
    std::vector<uint8> body;
};

class Connection {
public:
    int fd = -1;
    std::string uniqueName;
    std::string busGuid;

    ~Connection() { disconnect(); }

    bool connect() {
        const char* addr = getenv("DBUS_SESSION_BUS_ADDRESS");
        if (!addr) return false;
        std::string s(addr);
        auto pos = s.find("unix:");
        if (pos == std::string::npos) return false; pos += 5;
        bool abstract = false; std::string path;
        if (s.find("abstract=", pos) == pos) { abstract = true; pos += 9; }
        else if (s.find("path=", pos) == pos) pos += 5; else return false;
        auto end = s.find(',', pos);
        path = s.substr(pos, end == std::string::npos ? s.size() - pos : end - pos);
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return false;
        struct sockaddr_un addr_un; memset(&addr_un, 0, sizeof(addr_un));
        addr_un.sun_family = AF_UNIX;
        if (abstract) { addr_un.sun_path[0] = '\0'; strncpy(addr_un.sun_path + 1, path.c_str(), sizeof(addr_un.sun_path) - 2); }
        else strncpy(addr_un.sun_path, path.c_str(), sizeof(addr_un.sun_path) - 1);
        socklen_t len = abstract ? offsetof(struct sockaddr_un, sun_path) + 1 + path.size() : sizeof(struct sockaddr_un);
        if (::connect(fd, (struct sockaddr*)&addr_un, len) < 0) { ::close(fd); fd = -1; return false; }
        if (!authenticate()) { disconnect(); return false; }
        if (!sendBegin()) { disconnect(); return false; }
        if (!callHello()) { disconnect(); return false; }
        return true;
    }

    void disconnect() { if (fd >= 0) { ::close(fd); fd = -1; } }

    bool sendMessage(const std::vector<uint8>& data) {
        size_t off = 0;
        while (off < data.size()) {
            ssize_t n = write(fd, data.data() + off, data.size() - off);
            if (n <= 0) return false;
            off += (size_t)n;
        }
        return true;
    }

    bool send(const Message& msg) {
        std::vector<uint8> wire; msg.build(wire);
        return sendMessage(wire);
    }

    bool sendMethodCall(const std::string& dest, const std::string& path,
                        const std::string& iface, const std::string& method,
                        const std::vector<uint8>& bodyData, const std::string& sig) {
        Message msg(1, dest, path, iface, method, sig);
        msg.body = bodyData;
        return send(msg);
    }

    bool sendSignal(const std::string& path, const std::string& iface,
                    const std::string& name, const std::string& sig,
                    const std::vector<uint8>& bodyData) {
        Message msg(4, "", path, iface, name, sig);
        msg.body = bodyData;
        return send(msg);
    }

    // Method return for an incoming call (sender + serial from ParsedMessage).
    bool sendMethodReturn(const std::string& dest, uint32 replySerial,
                          const std::string& path, const std::string& iface,
                          const std::string& member, const std::string& sig,
                          const std::vector<uint8>& bodyData) {
        Message msg(2, dest, path, iface, member, sig);
        msg.replySerial = replySerial;
        msg.body = bodyData;
        return send(msg);
    }

    bool sendError(const std::string& dest, uint32 replySerial, const std::string& errName,
                   const std::string& message) {
        Message msg(3, dest, "", "org.freedesktop.DBus.Error", errName, "s");
        msg.replySerial = replySerial;
        Writer w; w.str(message);
        msg.body = w.buf;
        return send(msg);
    }

    bool readMessage(std::vector<uint8>& out) {
        uint8 hdr[16];
        if (!readExact(hdr, 16)) return false;
        uint32 bodyLen = hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | (hdr[7] << 24);
        uint32 hdrFldLen = parseHeaderFieldLen(hdr + 12);
        size_t padTo8 = ((16 + hdrFldLen + 7) & ~(size_t)7);
        size_t total = padTo8 + bodyLen;
        out.clear(); out.insert(out.end(), hdr, hdr + 16);
        out.resize(total);
        if (!readExact(out.data() + 16, (int)(total - 16))) return false;
        return true;
    }

    static uint32 parseHeaderFieldLen(const uint8* d) {
        return d[0] | (d[1] << 8) | (d[2] << 16) | (d[3] << 24);
    }

    static bool parseReply(const std::vector<uint8>& msg, std::string& errName, std::vector<uint8>& bodyOut) {
        if (msg.size() < 16) return false;
        uint8 msgType = msg[1];
        uint32 bodyLen = msg[4] | (msg[5] << 8) | (msg[6] << 16) | (msg[7] << 24);
        uint32 hfl = parseHeaderFieldLen(msg.data() + 12);
        size_t bs = ((16 + hfl + 7) & ~(size_t)7);
        if (bs + bodyLen > msg.size()) bodyLen = (uint32)(msg.size() - bs);
        bodyOut.assign(msg.begin() + bs, msg.begin() + bs + bodyLen);
        if (msgType == 3) { Reader r(msg.data() + bs, bodyLen); errName = r.rstr(); return false; }
        return true;
    }

    // Decode type/serial/sender/interface/member/signature/body from a raw message.
    static bool parseMessage(const std::vector<uint8>& msg, ParsedMessage& out) {
        out = ParsedMessage{};
        if (msg.size() < 16) return false;
        out.type = msg[1];
        out.serial = msg[8] | (msg[9] << 8) | (msg[10] << 16) | (msg[11] << 24);
        uint32 hfl = parseHeaderFieldLen(msg.data() + 12);
        uint32 bodyLen = msg[4] | (msg[5] << 8) | (msg[6] << 16) | (msg[7] << 24);
        size_t fieldsEnd = 16 + hfl;
        size_t bs = ((fieldsEnd + 7) & ~(size_t)7);
        if (bs > msg.size()) return false;
        if (bs + bodyLen > msg.size()) bodyLen = (uint32)(msg.size() - bs);
        out.body.assign(msg.begin() + bs, msg.begin() + bs + bodyLen);

        Reader r(msg.data(), msg.size());
        r.pos = 16;
        while (r.pos < fieldsEnd && r.pos < msg.size()) {
            uint8 code = r.r8();
            std::string vsig = r.rsig();
            std::string sval;
            uint32 uval = 0;
            if (vsig == "s" || vsig == "o") sval = r.rstr();
            else if (vsig == "g") sval = r.rsig();
            else if (vsig == "u") uval = r.r32();
            else break;                       // unsupported header field type
            switch (code) {
                case 1: out.path = sval; break;
                case 2: out.iface = sval; break;
                case 3: out.member = sval; break;
                case 5: out.replySerial = uval; break;
                case 6: out.destination = sval; break;
                case 7: out.sender = sval; break;
                case 8: out.signature = sval; break;
                default: break;
            }
            r.pad(8);   // each header field is an 8-aligned struct
        }
        out.valid = true;
        return true;
    }

private:
    bool authenticate() {
        // EXTERNAL takes the hex encoding of the decimal ASCII uid.
        char dec[32];
        snprintf(dec, sizeof(dec), "%d", (int)getuid());
        static const char* hexd = "0123456789abcdef";
        std::string hex;
        for (char* p = dec; *p; ++p) {
            unsigned char u = (unsigned char)*p;
            hex.push_back(hexd[u >> 4]);
            hex.push_back(hexd[u & 0xF]);
        }
        // NOTE: the leading NUL is a real protocol byte, so build the string
        // explicitly (a "\0..." literal would truncate).
        std::string authCmd;
        authCmd.push_back('\0');
        authCmd += "AUTH EXTERNAL ";
        authCmd += hex;
        authCmd += "\r\n";
        if (write(fd, authCmd.data(), authCmd.size()) != (ssize_t)authCmd.size()) {
#ifdef DBUS_MINI_DEBUG
            fprintf(stderr, "[dbus] auth write failed\n");
#endif
            return false;
        }
        char buf[256];
        int n = readLine(buf, sizeof(buf));
        if (n <= 0) {
#ifdef DBUS_MINI_DEBUG
            fprintf(stderr, "[dbus] auth read failed\n");
#endif
            return false;
        }
        std::string resp(buf, n);
#ifdef DBUS_MINI_DEBUG
        fprintf(stderr, "[dbus] auth resp: %s", resp.c_str());
#endif
        if (resp.find("OK ")==0) { busGuid = resp.substr(3); return true; }
        return false;
    }

    bool sendBegin() {
        const char* begin = "BEGIN\r\n";
        return write(fd, begin, 7) == 7;
    }

    bool callHello() {
        if (!sendMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus",
                            "org.freedesktop.DBus", "Hello", {}, "")) {
#ifdef DBUS_MINI_DEBUG
            fprintf(stderr, "[dbus] Hello send failed\n");
#endif
            return false;
        }
        std::vector<uint8> reply;
        if (!readMessage(reply)) {
#ifdef DBUS_MINI_DEBUG
            fprintf(stderr, "[dbus] Hello read failed\n");
#endif
            return false;
        }
        std::string err; std::vector<uint8> body;
        if (!parseReply(reply, err, body)) {
#ifdef DBUS_MINI_DEBUG
            fprintf(stderr, "[dbus] Hello error: %s\n", err.c_str());
#endif
            return false;
        }
        Reader r(body.data(), body.size());
        uniqueName = r.rstr();
        return !uniqueName.empty();
    }

    int readLine(char* buf, int maxLen) {
        for (int i = 0; i < maxLen - 1; i++) {
            char c; if (::read(fd, &c, 1) != 1) return -1;
            buf[i] = c; if (c == '\n') { buf[i + 1] = '\0'; return i + 1; }
        }
        return -1;
    }

    bool readExact(uint8* buf, int len) {
        int total = 0;
        while (total < len) { int n = ::read(fd, buf + total, len - total); if (n <= 0) return false; total += n; }
        return true;
    }
};

inline Writer& operator<<(Writer& w, const std::string& s) { w.str(s); return w; }
inline Writer& operator<<(Writer& w, uint32 v) { w.u32(v); return w; }
inline Writer& operator<<(Writer& w, int32 v) { w.s32(v); return w; }
inline Writer& operator<<(Writer& w, bool v) { w.bool_(v); return w; }

} // namespace dbus_mini
} // namespace volt
