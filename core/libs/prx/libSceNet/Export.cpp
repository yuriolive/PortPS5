// libSceNet: clean-room offline replacement networking stack for PS5 guest binaries.
// Simulates local sockets, epoll event loops, memory pools, and resolvers under System V ABI
// with FreeBSD-compatible errno reporting, preventing network deadlocks offline.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

// Offline network stack: no link is ever up. Sockets are local bookkeeping objects (create/bind/listen/options
// succeed like they do on a console without a connection), anything that needs a peer fails with the same errno a
// console with no route gives, and blocking calls return on their timeout, an abort or a close — infinite
// ("wait forever") timeouts are serviced in bounded slices so the inert stack can never deadlock a caller.

namespace {
// FreeBSD errno numbers as reported through sceNetErrnoLoc (SCE_NET_ERROR_* is 0x80410100 + errno).
constexpr int NET_ENOENT = 2;
constexpr int NET_EBADF = 9;
constexpr int NET_EINVAL = 22;
constexpr int NET_EAGAIN = 35;
constexpr int NET_ENOTSOCK = 38;
constexpr int NET_EOPNOTSUPP = 45;
constexpr int NET_EPROTONOSUPPORT = 43;
constexpr int NET_EAFNOSUPPORT = 47;
constexpr int NET_EADDRINUSE = 48;
constexpr int NET_ENETUNREACH = 51;
constexpr int NET_ECONNABORTED = 53;
constexpr int NET_ENOTCONN = 57;
constexpr int NET_ETIMEDOUT = 60;
constexpr int NET_ECONNREFUSED = 61;
constexpr int NET_ERROR_RESOLVER_ENODNS = static_cast<int>(0x804101E1u);

constexpr int NET_AF_INET = 2;
constexpr int NET_AF_INET6 = 28;
constexpr int NET_SOCK_STREAM = 1;
constexpr int NET_SOCK_DGRAM = 2;
constexpr int NET_SOCK_RAW = 3;
constexpr int NET_SOL_SOCKET = 0xFFFF;
constexpr int NET_SO_SNDTIMEO = 0x1005;
constexpr int NET_SO_RCVTIMEO = 0x1006;
constexpr int NET_SO_NBIO = 0x1200;

struct Sock {
    int family = 0;
    int type = 0;
    bool nonblock = false;
    bool bound = false;
    bool listening = false;
    bool aborted = false;
    std::uint16_t port = 0;  // network byte order
    std::uint32_t addr = 0;  // network byte order
    int rcv_timeout_us = 0;
    int snd_timeout_us = 0;
};

std::mutex g_mutex;
std::condition_variable g_cv;
std::map<int, Sock> g_socks;
std::set<int> g_epolls;
std::set<int> g_pools;
std::set<int> g_resolvers;
int g_next_sock = 32;
int g_next_epoll = 0x4000;
int g_next_pool = 1;
int g_next_resolver = 0x100;
std::uint16_t g_next_port = 49152;
bool g_net_inited = false;

int* errno_slot() {
    static thread_local int e = 0;
    return &e;
}

int fail(int err) {
    *errno_slot() = err;
    return -1;
}

void log_soft(const char* func, const char* what) {
    static std::mutex mtx;
    static std::map<std::string, int> hits;
    std::lock_guard<std::mutex> lk(mtx);
    if (++hits[func] > 3) {
        return;
    }
    std::fprintf(stderr, "[SOFT-NET] %s -> %s\n", func, what);
    std::fflush(stderr);
}

std::uint16_t swap16(std::uint16_t v) { return static_cast<std::uint16_t>((v << 8) | (v >> 8)); }
std::uint32_t swap32(std::uint32_t v) {
    return (v << 24) | ((v & 0xFF00u) << 8) | ((v >> 8) & 0xFF00u) | (v >> 24);
}

// Waits on the socket until the timeout expires or it is closed/aborted.
// timeout_us <= 0 nominally means "wait forever", but on an inert stack nothing can ever arrive, so such a
// wait is bounded to INFINITE_WAIT_SLICE_US per iteration and then reports EAGAIN: the caller keeps correct
// blocking-loop semantics without the process being able to deadlock on the network.
// Returns 0 on timeout, -1 with errno set when the socket went away or was aborted.
int block_on(std::unique_lock<std::mutex>& lk, int fd, int timeout_us) {
    constexpr int INFINITE_WAIT_SLICE_US = 100000;  // 100 ms
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::microseconds(timeout_us > 0 ? timeout_us : INFINITE_WAIT_SLICE_US);
    for (;;) {
        auto it = g_socks.find(fd);
        if (it == g_socks.end()) {
            return fail(NET_EBADF);
        }
        if (it->second.aborted) {
            it->second.aborted = false;
            return fail(NET_ECONNABORTED);
        }
        if (g_cv.wait_until(lk, deadline) == std::cv_status::timeout) {
            return fail(NET_EAGAIN);
        }
    }
}
}  // namespace

extern "C" {

// Returns pointer to thread-local errno variable for networking operations.
int* APS5_VABI sceNetErrnoLoc(void) {
    return errno_slot();
}

// Initializes the networking library state.
// Returns 0 on success.
int APS5_VABI sceNetInit_nid_postfix(void) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_net_inited = true;
    return 0;
}

// Terminates the networking library state.
// Returns 0 on success.
int APS5_VABI sceNetTerm(void) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_net_inited = false;
    return 0;
}

// Creates a network memory pool context.
// Returns memory pool ID on success, or -1 on error with errno set.
int APS5_VABI sceNetPoolCreate(const char* name, int size, int flags) {
    (void)name;
    (void)flags;
    if (size <= 0) {
        return fail(NET_EINVAL);
    }
    std::lock_guard<std::mutex> lk(g_mutex);
    const int id = g_next_pool++;
    g_pools.insert(id);
    return id;
}

// Destroys a previously created network memory pool context.
// Returns 0 on success, or -1 on error with errno set.
int APS5_VABI sceNetPoolDestroy(int memid) {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_pools.erase(memid) != 0 ? 0 : fail(NET_EBADF);
}

// Creates an endpoint for network communication.
// Returns non-negative socket descriptor on success, or -1 on error with errno set.
int APS5_VABI sceNetSocket(const char* name, int family, int type, int protocol) {
    (void)name;
    (void)protocol;
    if (family != NET_AF_INET && family != NET_AF_INET6) {
        return fail(NET_EAFNOSUPPORT);
    }
    if (type != NET_SOCK_STREAM && type != NET_SOCK_DGRAM && type != NET_SOCK_RAW) {
        return fail(NET_EPROTONOSUPPORT);
    }
    std::lock_guard<std::mutex> lk(g_mutex);
    const int fd = g_next_sock++;
    Sock s;
    s.family = family;
    s.type = type;
    g_socks[fd] = s;
    return fd;
}

// Closes a socket descriptor and frees associated resources.
// Returns 0 on success, or -1 on error with errno set.
int APS5_VABI sceNetSocketClose(int s) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_socks.erase(s) == 0) {
        return fail(NET_EBADF);
    }
    g_cv.notify_all();
    return 0;
}

// Aborts pending operations on a socket.
// Returns 0 on success, or -1 on error with errno set.
int APS5_VABI sceNetSocketAbort(int s, int flags) {
    (void)flags;
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_socks.find(s);
    if (it == g_socks.end()) {
        return fail(NET_EBADF);
    }
    it->second.aborted = true;
    g_cv.notify_all();
    return 0;
}

// Binds a local address to a socket descriptor.
// Returns 0 on success, or -1 on error with errno set.
int APS5_VABI sceNetBind_nid_postfix(int s, const void* addr, uint32_t addrlen) {
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_socks.find(s);
    if (it == g_socks.end()) {
        return fail(NET_EBADF);
    }
    if (addr == nullptr || addrlen < 8) {
        return fail(NET_EINVAL);
    }
    if (it->second.bound) {
        return fail(NET_EINVAL);
    }
    // sockaddr_in: len, family, port (BE), addr (BE)
    std::uint16_t port = 0;
    std::uint32_t ip = 0;
    std::memcpy(&port, static_cast<const std::uint8_t*>(addr) + 2, 2);
    std::memcpy(&ip, static_cast<const std::uint8_t*>(addr) + 4, 4);
    if (port == 0) {
        port = swap16(g_next_port++);
    } else {
        for (const auto& kv : g_socks) {
            if (kv.first != s && kv.second.bound && kv.second.port == port && kv.second.type == it->second.type) {
                return fail(NET_EADDRINUSE);
            }
        }
    }
    it->second.bound = true;
    it->second.port = port;
    it->second.addr = ip;
    return 0;
}

// Marks the socket as a passive socket that will accept incoming connections.
// Returns 0 on success, or -1 on error with errno set.
int APS5_VABI sceNetListen(int s, int backlog) {
    (void)backlog;
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_socks.find(s);
    if (it == g_socks.end()) {
        return fail(NET_EBADF);
    }
    if (it->second.type != NET_SOCK_STREAM) {
        return fail(NET_EOPNOTSUPP);
    }
    if (!it->second.bound) {
        it->second.bound = true;
        it->second.port = swap16(g_next_port++);
    }
    it->second.listening = true;
    return 0;
}

// Accepts an incoming connection request on a listening socket.
// Returns new socket descriptor on success, or -1 on error with errno set.
int APS5_VABI sceNetAccept(int s, void* addr, uint32_t* addrlen) {
    (void)addr;
    (void)addrlen;
    std::unique_lock<std::mutex> lk(g_mutex);
    auto it = g_socks.find(s);
    if (it == g_socks.end()) {
        return fail(NET_EBADF);
    }
    if (!it->second.listening) {
        return fail(NET_EINVAL);
    }
    if (it->second.nonblock) {
        return fail(NET_EAGAIN);
    }
    // No peer can ever arrive: wait for close/abort like an idle listener.
    const int rc = block_on(lk, s, it->second.rcv_timeout_us);
    return rc;
}

// Initiates a connection on a socket.
// Returns 0 on success, or -1 on error with errno set (e.g. NET_ENETUNREACH offline).
int APS5_VABI sceNetConnect(int s, const void* addr, uint32_t addrlen) {
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_socks.find(s);
    if (it == g_socks.end()) {
        return fail(NET_EBADF);
    }
    if (addr == nullptr || addrlen < 8) {
        return fail(NET_EINVAL);
    }
    std::uint32_t ip = 0;
    std::memcpy(&ip, static_cast<const std::uint8_t*>(addr) + 4, 4);
    if ((swap32(ip) >> 24) == 127) {
        return fail(NET_ECONNREFUSED);  // loopback with no listener
    }
    return fail(NET_ENETUNREACH);
}

static int recv_common(int s) {
    std::unique_lock<std::mutex> lk(g_mutex);
    auto it = g_socks.find(s);
    if (it == g_socks.end()) {
        return fail(NET_EBADF);
    }
    if (it->second.type == NET_SOCK_STREAM) {
        return fail(NET_ENOTCONN);
    }
    if (it->second.nonblock) {
        return fail(NET_EAGAIN);
    }
    return block_on(lk, s, it->second.rcv_timeout_us);
}

// Receives a message from a connected socket.
// Returns number of bytes received, or -1 on error with errno set.
int64_t APS5_VABI sceNetRecv(int s, void* buf, size_t len, int flags) {
    (void)buf;
    (void)len;
    (void)flags;
    return recv_common(s);
}

// Receives a message from a socket and captures sender address.
// Returns number of bytes received, or -1 on error with errno set.
int64_t APS5_VABI sceNetRecvfrom(int s, void* buf, size_t len, int flags, void* from, uint32_t* fromlen) {
    (void)buf;
    (void)len;
    (void)flags;
    (void)from;
    (void)fromlen;
    return recv_common(s);
}

static int send_common(int s) {
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_socks.find(s);
    if (it == g_socks.end()) {
        return fail(NET_EBADF);
    }
    return fail(it->second.type == NET_SOCK_STREAM ? NET_ENOTCONN : NET_ENETUNREACH);
}

// Sends a message on a connected socket.
// Returns number of bytes sent, or -1 on error with errno set.
int64_t APS5_VABI sceNetSend(int s, const void* buf, size_t len, int flags) {
    (void)buf;
    (void)len;
    (void)flags;
    return send_common(s);
}

// Sends a message on a socket to a specified destination.
// Returns number of bytes sent, or -1 on error with errno set.
int64_t APS5_VABI sceNetSendto(int s, const void* buf, size_t len, int flags, const void* to, uint32_t tolen) {
    (void)buf;
    (void)len;
    (void)flags;
    (void)to;
    (void)tolen;
    return send_common(s);
}

// Shuts down all or part of a full-duplex connection on a socket.
// Returns 0 on success, or -1 on error with errno set.
int APS5_VABI sceNetShutdown(int s, int how) {
    (void)how;
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_socks.find(s);
    if (it == g_socks.end()) {
        return fail(NET_EBADF);
    }
    return it->second.type == NET_SOCK_STREAM ? fail(NET_ENOTCONN) : 0;
}

// Sets options on a socket descriptor.
// Returns 0 on success, or -1 on error with errno set.
int APS5_VABI sceNetSetsockopt(int s, int level, int optname, const void* optval, uint32_t optlen) {
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_socks.find(s);
    if (it == g_socks.end()) {
        return fail(NET_EBADF);
    }
    if (level == NET_SOL_SOCKET && optval != nullptr && optlen >= sizeof(int)) {
        int v = 0;
        std::memcpy(&v, optval, sizeof(v));
        switch (optname) {
            case NET_SO_NBIO: it->second.nonblock = v != 0; break;
            case NET_SO_RCVTIMEO: it->second.rcv_timeout_us = v; break;
            case NET_SO_SNDTIMEO: it->second.snd_timeout_us = v; break;
            default: break;
        }
    }
    return 0;
}

// Retrieves options associated with a socket descriptor.
// Returns 0 on success, or -1 on error with errno set.
int APS5_VABI sceNetGetsockopt(int s, int level, int optname, void* optval, uint32_t* optlen) {
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_socks.find(s);
    if (it == g_socks.end()) {
        return fail(NET_EBADF);
    }
    if (optval == nullptr || optlen == nullptr || *optlen < sizeof(int)) {
        return fail(NET_EINVAL);
    }
    int v = 0;
    if (level == NET_SOL_SOCKET) {
        switch (optname) {
            case NET_SO_NBIO: v = it->second.nonblock ? 1 : 0; break;
            case NET_SO_RCVTIMEO: v = it->second.rcv_timeout_us; break;
            case NET_SO_SNDTIMEO: v = it->second.snd_timeout_us; break;
            default: break;
        }
    }
    std::memcpy(optval, &v, sizeof(v));
    *optlen = sizeof(v);
    return 0;
}

// Retrieves the locally-bound address for a socket descriptor.
// Returns 0 on success, or -1 on error with errno set.
int APS5_VABI sceNetGetsockname(int s, void* addr, uint32_t* addrlen) {
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_socks.find(s);
    if (it == g_socks.end()) {
        return fail(NET_EBADF);
    }
    if (addr == nullptr || addrlen == nullptr) {
        return fail(NET_EINVAL);
    }
    std::uint8_t sa[16] = {};
    sa[0] = 16;
    sa[1] = static_cast<std::uint8_t>(NET_AF_INET);
    std::memcpy(sa + 2, &it->second.port, 2);
    std::memcpy(sa + 4, &it->second.addr, 4);
    const std::uint32_t n = *addrlen < sizeof(sa) ? *addrlen : static_cast<std::uint32_t>(sizeof(sa));
    std::memcpy(addr, sa, n);
    *addrlen = sizeof(sa);
    return 0;
}

// Retrieves internal socket status information.
// Returns 0 on success, or -1 on error with errno set.
int APS5_VABI sceNetGetSockInfo(int s, void* info, int n, int flags) {
    (void)info;
    (void)n;
    (void)flags;
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_socks.count(s) != 0 ? 0 : fail(NET_EBADF);
}

// Epoll: nothing can become ready without a network, so a wait sleeps for its timeout (microseconds; a
// negative infinite timeout is serviced in bounded slices) and reports zero events.
// Creates an epoll event notification instance.
// Returns epoll file descriptor on success, or -1 on error with errno set.
int APS5_VABI sceNetEpollCreate(const char* name, int flags) {
    (void)name;
    (void)flags;
    std::lock_guard<std::mutex> lk(g_mutex);
    const int id = g_next_epoll++;
    g_epolls.insert(id);
    return id;
}

// Destroys an epoll event notification instance.
// Returns 0 on success, or -1 on error with errno set.
int APS5_VABI sceNetEpollDestroy(int eid) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_epolls.erase(eid) == 0) {
        return fail(NET_EBADF);
    }
    g_cv.notify_all();
    return 0;
}

// Controls an epoll event notification instance.
// Returns 0 on success, or -1 on error with errno set.
int APS5_VABI sceNetEpollControl(int eid, int op, int id, const NetEpollEvent* event) {
    (void)event;
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_epolls.count(eid) == 0) {
        return fail(NET_EBADF);
    }
    if (g_socks.count(id) == 0) {
        return fail(NET_EBADF);
    }
    if (op < 1 || op > 3) {
        return fail(NET_EINVAL);
    }
    return 0;
}

// Waits for events on an epoll instance.
// Returns number of ready events (0 on timeout), or -1 on error with errno set.
int APS5_VABI sceNetEpollWait(int eid, NetEpollEvent* events, int maxevents, int timeout) {
    (void)events;
    if (maxevents <= 0) {
        return fail(NET_EINVAL);
    }
    std::unique_lock<std::mutex> lk(g_mutex);
    if (g_epolls.count(eid) == 0) {
        return fail(NET_EBADF);
    }
    if (timeout == 0) {
        return 0;
    }
    // timeout < 0 nominally waits until the epoll object is destroyed; bound each wait slice so an
    // infinite-timeout waiter on the inert stack still returns periodically (0 events) instead of
    // deadlocking if nobody ever destroys the epoll.
    constexpr int INFINITE_WAIT_SLICE_US = 100000;  // 100 ms
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::microseconds(timeout > 0 ? timeout : INFINITE_WAIT_SLICE_US);
    for (;;) {
        if (g_cv.wait_until(lk, deadline) == std::cv_status::timeout) {
            return 0;
        }
        if (g_epolls.count(eid) == 0) {
            return fail(NET_EBADF);
        }
    }
}

// Converts a 32-bit integer from host to network byte order.
uint32_t APS5_VABI sceNetHtonl_nid_postfix(uint32_t host32) { return swap32(host32); }

// Converts a 16-bit integer from host to network byte order.
uint16_t APS5_VABI sceNetHtons_nid_postfix(uint16_t host16) { return swap16(host16); }

// Converts a 32-bit integer from network to host byte order.
uint32_t APS5_VABI sceNetNtohl_nid_postfix(uint32_t net32) { return swap32(net32); }

// Converts a 16-bit integer from network to host byte order.
uint16_t APS5_VABI sceNetNtohs_nid_postfix(uint16_t net16) { return swap16(net16); }

// Converts an IP address from string to numeric network format.
// Returns 1 on success, 0 if input string is invalid format, or -1 on error.
int APS5_VABI sceNetInetPton(int af, const char* src, void* dst) {
    if (src == nullptr || dst == nullptr) {
        return fail(NET_EINVAL);
    }
    if (af != NET_AF_INET) {
        return fail(NET_EAFNOSUPPORT);
    }
    unsigned a = 0, b = 0, c = 0, d = 0;
    char tail = 0;
    if (std::sscanf(src, "%3u.%3u.%3u.%3u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 || d > 255) {
        return 0;
    }
    const std::uint8_t out[4] = {static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(c), static_cast<std::uint8_t>(d)};
    std::memcpy(dst, out, 4);
    return 1;
}

// Converts a numeric network address to string format.
// Returns destination pointer on success, or nullptr on error with errno set.
const char* APS5_VABI sceNetInetNtop(int af, const void* src, char* dst, uint32_t size) {
    if (src == nullptr || dst == nullptr || af != NET_AF_INET) {
        *errno_slot() = af != NET_AF_INET ? NET_EAFNOSUPPORT : NET_EINVAL;
        return nullptr;
    }
    const auto* p = static_cast<const std::uint8_t*>(src);
    char tmp[16];
    const int n = std::snprintf(tmp, sizeof(tmp), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
    if (n < 0 || static_cast<uint32_t>(n) >= size) {
        *errno_slot() = 28;  // ENOSPC
        return nullptr;
    }
    std::memcpy(dst, tmp, static_cast<std::size_t>(n) + 1);
    return dst;
}

// Formats an Ethernet MAC address into text representation.
// Returns 0 on success, or -1 on error with errno set.
int APS5_VABI sceNetEtherNtostr(const NetEtherAddr* n, char* str, size_t len) {
    if (n == nullptr || str == nullptr || len < 18) {
        return fail(NET_EINVAL);
    }
    std::snprintf(str, len, "%02x:%02x:%02x:%02x:%02x:%02x", n->data[0], n->data[1], n->data[2], n->data[3], n->data[4], n->data[5]);
    return 0;
}

// Retrieves synthetic MAC address of offline network device.
// Returns 0 on success, or -1 on error with errno set.
int APS5_VABI sceNetGetMacAddress(NetEtherAddr* addr, int flags) {
    (void)flags;
    if (addr == nullptr) {
        return fail(NET_EINVAL);
    }
    // Locally administered address; there is no adapter to read one from.
    const std::uint8_t mac[6] = {0x02, 0x50, 0x53, 0x35, 0x00, 0x01};
    std::memcpy(addr->data, mac, 6);
    return 0;
}

// Creates a DNS resolver context.
// Returns resolver ID on success, or -1 on error with errno set.
int APS5_VABI sceNetResolverCreate(const char* name, int memid, int flags) {
    (void)name;
    (void)memid;
    (void)flags;
    std::lock_guard<std::mutex> lk(g_mutex);
    const int id = g_next_resolver++;
    g_resolvers.insert(id);
    return id;
}

// Destroys a DNS resolver context.
// Returns 0 on success, or -1 on error with errno set.
int APS5_VABI sceNetResolverDestroy(int rid) {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_resolvers.erase(rid) != 0 ? 0 : fail(NET_EBADF);
}

// Resolves a hostname to a network address asynchronously (reports offline failure).
// Returns NET_ERROR_RESOLVER_ENODNS with errno set to NET_ETIMEDOUT.
int APS5_VABI sceNetResolverStartNtoa(int rid, const char* hostname, void* addr, int timeout, int retry, int flags) {
    (void)hostname;
    (void)addr;
    (void)timeout;
    (void)retry;
    (void)flags;
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_resolvers.count(rid) == 0) {
        return fail(NET_EBADF);
    }
    log_soft(__func__, "ENODNS (no network)");
    *errno_slot() = NET_ETIMEDOUT;
    return NET_ERROR_RESOLVER_ENODNS;
}

// Resolves a network address to a hostname asynchronously (reports offline failure).
// Returns NET_ERROR_RESOLVER_ENODNS with errno set to NET_ETIMEDOUT.
int APS5_VABI sceNetResolverStartAton(int rid, const void* addr, char* hostname, int len, int timeout, int retry, int flags) {
    (void)addr;
    (void)hostname;
    (void)len;
    (void)timeout;
    (void)retry;
    (void)flags;
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_resolvers.count(rid) == 0) {
        return fail(NET_EBADF);
    }
    log_soft(__func__, "ENODNS (no network)");
    *errno_slot() = NET_ETIMEDOUT;
    return NET_ERROR_RESOLVER_ENODNS;
}
}
