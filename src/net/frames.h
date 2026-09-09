// =====================================================================================================
// TAPESTRY · src/net/frames.h · the client wire: length-prefixed frames, and the transactor's socket
//
// §4.2 and §12: THE TRANSACTOR HOLDS THE NETWORK. The peer keeps fusord's module gate verbatim, which
// forbids the Winsock module on this platform, so every socket in this store lives on this side of the
// line and the peer reads committed deltas over a pipe. Winsock is included here and nowhere else.
//
// A frame is a 4-byte big-endian length followed by that many bytes. Two rules earn their keep:
//   * the length is read before the body, and a length above the cap is `too_large` — a protocol
//     fault, counted, with the connection closed, never a multi-gigabyte allocation on a stranger's
//     say-so;
//   * a short read is a closed connection, not a partial frame: the reader loops until the byte count
//     is met or the peer is gone.
//
// The REQUEST BODY IS THE TAPE'S OWN ENCODING. A write's body on the wire is byte-for-byte the body
// that will be hashed into the entry, read by the same strict fixed-order reader (`read_tx_body`).
// One encoding, one reader, used in both places: there is no second grammar to drift.
// =====================================================================================================
#pragma once

#include <cstdint>
#include <string>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#endif

namespace tapestry {
namespace net {

inline const uint32_t MAX_FRAME = 8u << 20;   // 8 MiB; above this is `too_large`

inline bool startup() {
#if defined(_WIN32)
    static bool done = false;
    if (done) return true;
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return false;
    done = true;
#endif
    return true;
}

struct Conn {
#if defined(_WIN32)
    SOCKET s = INVALID_SOCKET;
    bool ok() const { return s != INVALID_SOCKET; }
    void close_() { if (s != INVALID_SOCKET) { closesocket(s); s = INVALID_SOCKET; } }
#else
    int s = -1;
    bool ok() const { return s >= 0; }
    void close_() { if (s >= 0) { ::close(s); s = -1; } }
#endif

    bool send_all(const char* p, size_t n) {
        size_t done = 0;
        while (done < n) {
            const int got = ::send(s, p + done, (int)(n - done), 0);
            if (got <= 0) return false;
            done += (size_t)got;
        }
        return true;
    }
    bool recv_all(char* p, size_t n) {
        size_t done = 0;
        while (done < n) {
            const int got = ::recv(s, p + done, (int)(n - done), 0);
            if (got <= 0) return false;             // a short read is a closed peer, not a part frame
            done += (size_t)got;
        }
        return true;
    }
    bool send_frame(const std::string& body) {
        if (body.size() > MAX_FRAME) return false;
        const uint32_t n = (uint32_t)body.size();
        char hdr[4] = { (char)((n >> 24) & 0xff), (char)((n >> 16) & 0xff),
                        (char)((n >> 8) & 0xff), (char)(n & 0xff) };
        return send_all(hdr, 4) && send_all(body.data(), body.size());
    }
    // Returns false on a closed peer. `too_large` is set when the declared length is over the cap:
    // the caller counts it as a protocol fault and drops the connection without reading the body.
    bool recv_frame(std::string* body, bool* too_large) {
        *too_large = false;
        char hdr[4];
        if (!recv_all(hdr, 4)) return false;
        const uint32_t n = ((uint32_t)(unsigned char)hdr[0] << 24) | ((uint32_t)(unsigned char)hdr[1] << 16)
                         | ((uint32_t)(unsigned char)hdr[2] << 8)  | (uint32_t)(unsigned char)hdr[3];
        if (n > MAX_FRAME) { *too_large = true; return false; }
        body->assign(n, '\0');
        if (n == 0) return true;
        return recv_all(&(*body)[0], n);
    }
};

struct Listener {
    Conn l;
    uint16_t port = 0;

    bool listen_on(uint16_t p, std::string* err) {
        if (!startup()) { if (err) *err = "wsastartup"; return false; }
        l.s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (!l.ok()) { if (err) *err = "socket"; return false; }
        int yes = 1;
        ::setsockopt(l.s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(p);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);      // loopback only: this is not an exposed port
        if (::bind(l.s, (sockaddr*)&a, sizeof(a)) != 0) { if (err) *err = "bind"; return false; }
        if (::listen(l.s, 16) != 0) { if (err) *err = "listen"; return false; }
        int alen = (int)sizeof(a);
        if (::getsockname(l.s, (sockaddr*)&a, &alen) == 0) port = ntohs(a.sin_port);
        return true;
    }
    bool accept_one(Conn* c) {
        c->s = ::accept(l.s, nullptr, nullptr);
        return c->ok();
    }
    void close_() { l.close_(); }
};

inline bool connect_to(uint16_t port, Conn* c, std::string* err) {
    if (!startup()) { if (err) *err = "wsastartup"; return false; }
    c->s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!c->ok()) { if (err) *err = "socket"; return false; }
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(c->s, (sockaddr*)&a, sizeof(a)) != 0) { if (err) *err = "connect"; c->close_(); return false; }
    return true;
}

} // namespace net
} // namespace tapestry
