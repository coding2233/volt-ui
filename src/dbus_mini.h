#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <map>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdlib>
#include <cerrno>

namespace volt {
namespace dbus_mini {

using uint8  = uint8_t;
using uint32 = uint32_t;
using int32  = int32_t;

struct Writer {
    std::vector<uint8> buf;
    void pad8() { while (buf.size() % 8) buf.push_back(0); }
    void u32(uint32 v) { buf.push_back(v&0xFF); buf.push_back((v>>8)&0xFF); buf.push_back((v>>16)&0xFF); buf.push_back((v>>24)&0xFF); }
    void s32(int32 v)  { u32((uint32)v); }
    void byte(uint8 v) { buf.push_back(v); }
    void str(const std::string& s) { u32((uint32)s.size()); for(char c:s) buf.push_back((uint8)c); buf.push_back(0); }
    void sig(const std::string& s) { byte((uint8)s.size()); for(char c:s) buf.push_back((uint8)c); buf.push_back(0); }
    void bool_(bool v) { u32(v?1:0); }
    void dictEntryBegin() { pad8(); }
    void variantBegin(const std::string& sig) { byte((uint8)sig.size()); for(char c:sig) buf.push_back((uint8)c); buf.push_back(0); }
    void arrayBegin(uint32 len) { u32(len); }
};

struct Reader {
    const uint8* data;
    size_t len;
    size_t pos = 0;
    Reader(const uint8* d, size_t l) : data(d), len(l) {}
    uint8 r8()  { return pos<len ? data[pos++] : 0; }
    uint32 r32() { uint32 v=0; for(int i=0;i<4&&pos<len;i++) v|=(uint32)data[pos++]<<(i*8); return v; }
    int32 rs32() { return (int32)r32(); }
    std::string rstr() { uint32 n=r32(); std::string s((const char*)data+pos, n); pos+=n+1; return s; }
    bool rBool() { return r32()!=0; }
};

class Message {
public:
    std::vector<uint8> header;
    std::vector<uint8> body;
    uint32 serial = 0;
    uint8 type = 1;

    static uint32 nextSerial() { static uint32 s=1; return s++; }

    Message(uint8 msgType, const std::string& dest, const std::string& path,
            const std::string& iface, const std::string& member, const std::string& sig="")
        : type(msgType) {
        serial = nextSerial();
        Writer hw;
        hw.byte('l'); hw.byte(msgType); hw.byte(0); hw.byte(1); hw.u32(0); hw.u32(serial);
        int fc = 2;
        if(!dest.empty()) fc++; if(!iface.empty()) fc++; if(!sig.empty()) fc++;
        hw.u32(fc*8); hw.pad8(); // approximate, will be fixed-padded by align
        hw.byte(1); hw.byte(1); hw.byte('o'); hw.byte(0); hw.str(path); hw.pad8();
        hw.byte(3); hw.byte(1); hw.byte('s'); hw.byte(0); hw.str(member); hw.pad8();
        if(!iface.empty()){ hw.byte(2); hw.byte(1); hw.byte('s'); hw.byte(0); hw.str(iface); hw.pad8(); }
        if(!dest.empty()){ hw.byte(6); hw.byte(1); hw.byte('s'); hw.byte(0); hw.str(dest); hw.pad8(); }
        if(!sig.empty()){ hw.byte(8); hw.byte(1); hw.byte('g'); hw.byte(0); hw.sig(sig); hw.pad8(); }
        header = hw.buf;
    }

    void build(std::vector<uint8>& out) {
        uint32 bodyLen = (uint32)body.size();
        out.clear();
        for(size_t i=0; i<header.size(); i++){
            if(i==4){ out.push_back(bodyLen&0xFF); out.push_back((bodyLen>>8)&0xFF); out.push_back((bodyLen>>16)&0xFF); out.push_back((bodyLen>>24)&0xFF); i+=3; }
            else out.push_back(header[i]);
        }
        while(out.size()%8) out.push_back(0);
        out.insert(out.end(), body.begin(), body.end());
    }
};

class Connection {
public:
    int fd = -1;
    std::string uniqueName;
    std::string busGuid;

    ~Connection() { disconnect(); }

    bool connect() {
        const char* addr = getenv("DBUS_SESSION_BUS_ADDRESS");
        if(!addr) return false;
        std::string s(addr);
        auto pos = s.find("unix:");
        if(pos==std::string::npos) return false; pos+=5;
        bool abstract=false; std::string path;
        if(s.find("abstract=",pos)==pos){ abstract=true; pos+=9; }
        else if(s.find("path=",pos)==pos) pos+=5; else return false;
        auto end = s.find(',',pos);
        path = s.substr(pos, end==std::string::npos ? s.size()-pos : end-pos);
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if(fd<0) return false;
        struct sockaddr_un addr_un; memset(&addr_un,0,sizeof(addr_un));
        addr_un.sun_family = AF_UNIX;
        if(abstract){ addr_un.sun_path[0]='\0'; strncpy(addr_un.sun_path+1,path.c_str(),sizeof(addr_un.sun_path)-2); }
        else strncpy(addr_un.sun_path,path.c_str(),sizeof(addr_un.sun_path)-1);
        socklen_t len = abstract ? offsetof(struct sockaddr_un,sun_path)+1+path.size() : sizeof(struct sockaddr_un);
        if(::connect(fd,(struct sockaddr*)&addr_un,len)<0){ ::close(fd); fd=-1; return false; }
        return authenticate();
    }

    void disconnect() { if(fd>=0){ ::close(fd); fd=-1; } }

    bool sendMessage(const std::vector<uint8>& data) {
        return (ssize_t)data.size() == write(fd, data.data(), data.size());
    }

    bool sendMethodCall(const std::string& dest, const std::string& path,
                        const std::string& iface, const std::string& method,
                        const std::vector<uint8>& bodyData, const std::string& sig) {
        Message msg(1, dest, path, iface, method, sig);
        msg.body = bodyData;
        std::vector<uint8> wire; msg.build(wire);
        return sendMessage(wire);
    }

    bool sendSignal(const std::string& path, const std::string& iface,
                    const std::string& name, const std::string& sig,
                    const std::vector<uint8>& bodyData) {
        Message msg(4, "", path, iface, name, sig);
        msg.body = bodyData;
        std::vector<uint8> wire; msg.build(wire);
        return sendMessage(wire);
    }

    bool readMessage(std::vector<uint8>& out) {
        uint8 hdr[16];
        if(!readExact(hdr,16)) return false;
        uint32 bodyLen = hdr[4]|(hdr[5]<<8)|(hdr[6]<<16)|(hdr[7]<<24);
        uint32 hdrFldLen = parseHeaderFieldLen(hdr+12);
        size_t padTo8 = ((16+hdrFldLen+7)&~7);
        size_t total = padTo8+bodyLen;
        out.clear(); out.insert(out.end(),hdr,hdr+16);
        out.resize(total);
        if(!readExact(out.data()+16, (int)(total-16))) return false;
        return true;
    }

    static uint32 parseHeaderFieldLen(const uint8* d) {
        return d[0]|(d[1]<<8)|(d[2]<<16)|(d[3]<<24);
    }

    static bool parseReply(const std::vector<uint8>& msg, std::string& errName, std::vector<uint8>& bodyOut) {
        if(msg.size()<16) return false;
        uint8 msgType = msg[1];
        uint32 bodyLen = msg[4]|(msg[5]<<8)|(msg[6]<<16)|(msg[7]<<24);
        uint32 hfl = parseHeaderFieldLen(msg.data()+12);
        size_t bs = ((16+hfl+7)&~7);
        bodyOut.assign(msg.begin()+bs, msg.begin()+bs+bodyLen);
        if(msgType==3){ Reader r(msg.data()+bs, bodyLen); errName = r.rstr(); return false; }
        return true;
    }

private:
    bool authenticate() {
        uid_t uid = getuid();
        char hexUid[32]; snprintf(hexUid,sizeof(hexUid),"%x",(int)uid);
        size_t hl = strlen(hexUid);
        std::string authCmd = "\0AUTH EXTERNAL ";
        authCmd.append(16,' ');
        authCmd[15]=(char)hl; memcpy(&authCmd[16],hexUid,hl);
        authCmd.resize(17+hl); authCmd += "\r\n";
        if(write(fd,authCmd.data(),authCmd.size()) != (ssize_t)authCmd.size()) return false;
        char buf[256];
        int n=readLine(buf,sizeof(buf));
        if(n<=0) return false;
        std::string resp(buf,n);
        if(resp.find("OK ")==0) return true;
        return false;
    }

    int readLine(char* buf, int maxLen) {
        for(int i=0;i<maxLen-1;i++){
            char c; if(::read(fd,&c,1)!=1) return -1;
            buf[i]=c; if(c=='\n'){ buf[i+1]='\0'; return i+1; }
        }
        return -1;
    }

    bool readExact(uint8* buf, int len) {
        int total=0;
        while(total<len){ int n=::read(fd,buf+total,len-total); if(n<=0) return false; total+=n; }
        return true;
    }
};

inline Writer& operator<<(Writer& w, const std::string& s) { w.str(s); return w; }
inline Writer& operator<<(Writer& w, uint32 v) { w.u32(v); return w; }
inline Writer& operator<<(Writer& w, int32 v) { w.s32(v); return w; }
inline Writer& operator<<(Writer& w, bool v) { w.bool_(v); return w; }

} // namespace dbus_mini
} // namespace volt
