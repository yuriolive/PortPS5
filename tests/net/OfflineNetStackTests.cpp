// tests/net/OfflineNetStackTests.cpp
// GoogleTest-based verification suite for PortPS5 libSceNet offline stack implementation:
// Verifies socket lifecycle, address bindings, local error reporting, conversion utilities,
// and non-blocking epoll behavior under the offline model.
// Complies with System V ABI invariants and PortPS5 testing rules.

#include "common/TestHarness.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

extern "C" {
int* APS5_VABI sceNetErrnoLoc(void);
int APS5_VABI sceNetInit_nid_postfix(void);
int APS5_VABI sceNetTerm(void);
int APS5_VABI sceNetPoolCreate(const char* name, int size, int flags);
int APS5_VABI sceNetPoolDestroy(int memid);
int APS5_VABI sceNetSocket(const char* name, int family, int type, int protocol);
int APS5_VABI sceNetSocketClose(int s);
int APS5_VABI sceNetSocketAbort(int s, int flags);
int APS5_VABI sceNetBind_nid_postfix(int s, const void* addr, uint32_t addrlen);
int APS5_VABI sceNetListen(int s, int backlog);
int APS5_VABI sceNetAccept(int s, void* addr, uint32_t* addrlen);
int APS5_VABI sceNetConnect(int s, const void* addr, uint32_t addrlen);
int64_t APS5_VABI sceNetRecv(int s, void* buf, size_t len, int flags);
int64_t APS5_VABI sceNetSend(int s, const void* buf, size_t len, int flags);
int APS5_VABI sceNetShutdown(int s, int how);
int APS5_VABI sceNetSetsockopt(int s, int level, int optname, const void* optval, uint32_t optlen);
int APS5_VABI sceNetGetsockopt(int s, int level, int optname, void* optval, uint32_t* optlen);
int APS5_VABI sceNetGetsockname(int s, void* addr, uint32_t* addrlen);
int APS5_VABI sceNetGetSockInfo(int s, void* info, int n, int flags);
int APS5_VABI sceNetEpollCreate(const char* name, int flags);
int APS5_VABI sceNetEpollDestroy(int eid);
int APS5_VABI sceNetEpollWait(int eid, void* events, int maxevents, int timeout);
uint32_t APS5_VABI sceNetHtonl_nid_postfix(uint32_t host32);
uint16_t APS5_VABI sceNetHtons_nid_postfix(uint16_t host16);
uint32_t APS5_VABI sceNetNtohl_nid_postfix(uint32_t net32);
uint16_t APS5_VABI sceNetNtohs_nid_postfix(uint16_t net16);
int APS5_VABI sceNetInetPton(int af, const char* src, void* dst);
const char* APS5_VABI sceNetInetNtop(int af, const void* src, char* dst, uint32_t size);
int APS5_VABI sceNetResolverCreate(const char* name, int memid, int flags);
int APS5_VABI sceNetResolverDestroy(int rid);
int APS5_VABI sceNetResolverStartNtoa(int rid, const char* hostname, void* addr, int timeout, int retry, int flags);
struct NetEtherAddr {
    uint8_t data[6];
};
int APS5_VABI sceNetGetMacAddress(NetEtherAddr* addr, int flags);
}

namespace {

using namespace PortPS5::Testing;

constexpr int NET_AF_INET = 2;
constexpr int NET_AF_INET6 = 28;
constexpr int NET_SOCK_STREAM = 1;
constexpr int NET_SOCK_DGRAM = 2;
constexpr int NET_SOL_SOCKET = 0xFFFF;
constexpr int NET_SO_NBIO = 0x1200;

// Verifies network initialization and termination cycle.
TEST(OfflineNetStack, InitAndTermLifecycle) {
    EXPECT_EQ(sceNetInit_nid_postfix(), 0);
    EXPECT_NE(sceNetErrnoLoc(), nullptr);
    EXPECT_EQ(sceNetTerm(), 0);
}

// Verifies byte-order swapping and network/host conversion helpers.
TEST(OfflineNetStack, EndianConversion) {
    constexpr uint32_t host32 = 0x12345678;
    constexpr uint16_t host16 = 0x1234;
    const uint32_t net32 = sceNetHtonl_nid_postfix(host32);
    const uint16_t net16 = sceNetHtons_nid_postfix(host16);

    EXPECT_EQ(sceNetNtohl_nid_postfix(net32), host32);
    EXPECT_EQ(sceNetNtohs_nid_postfix(net16), host16);
}

// Verifies string to numeric IP address parsing and reverse formatting.
TEST(OfflineNetStack, InetPtonAndNtop) {
    uint8_t ipBytes[4] = {};
    const int rc = sceNetInetPton(NET_AF_INET, "192.168.1.100", ipBytes);
    EXPECT_EQ(rc, 1);
    EXPECT_EQ(ipBytes[0], 192);
    EXPECT_EQ(ipBytes[1], 168);
    EXPECT_EQ(ipBytes[2], 1);
    EXPECT_EQ(ipBytes[3], 100);

    char strBuf[32] = {};
    const char* str = sceNetInetNtop(NET_AF_INET, ipBytes, strBuf, sizeof(strBuf));
    EXPECT_NE(str, nullptr);
    EXPECT_STREQ(strBuf, "192.168.1.100");
}

// Verifies socket creation, option configuration, and descriptor closure.
TEST(OfflineNetStack, SocketCreateAndClose) {
    const int sock = sceNetSocket("test_tcp", NET_AF_INET, NET_SOCK_STREAM, 0);
    EXPECT_GE(sock, 0);

    int nbio = 1;
    EXPECT_EQ(sceNetSetsockopt(sock, NET_SOL_SOCKET, NET_SO_NBIO, &nbio, sizeof(nbio)), 0);

    int queryNbio = 0;
    uint32_t optlen = sizeof(queryNbio);
    EXPECT_EQ(sceNetGetsockopt(sock, NET_SOL_SOCKET, NET_SO_NBIO, &queryNbio, &optlen), 0);
    EXPECT_EQ(queryNbio, 1);

    EXPECT_EQ(sceNetSocketClose(sock), 0);
    // Double close should fail with EBADF
    EXPECT_EQ(sceNetSocketClose(sock), -1);
}

// Verifies non-blocking connect returns offline network unreachable errno.
TEST(OfflineNetStack, ConnectOfflineFailsGracefully) {
    const int sock = sceNetSocket("test_connect", NET_AF_INET, NET_SOCK_STREAM, 0);
    ASSERT_GE(sock, 0);

    uint8_t addr[16] = {};
    addr[0] = 16;
    addr[1] = static_cast<uint8_t>(NET_AF_INET);
    // 10.0.0.1
    addr[4] = 10; addr[5] = 0; addr[6] = 0; addr[7] = 1;

    const int rc = sceNetConnect(sock, addr, sizeof(addr));
    EXPECT_EQ(rc, -1);
    EXPECT_EQ(*sceNetErrnoLoc(), 51); // NET_ENETUNREACH

    EXPECT_EQ(sceNetSocketClose(sock), 0);
}

// Verifies epoll creation, timeout waiting, and destruction.
TEST(OfflineNetStack, EpollTimeoutReturnsZeroEvents) {
    const int eid = sceNetEpollCreate("test_epoll", 0);
    EXPECT_GE(eid, 0);

    // Timeout of 0 should immediately return 0 events without blocking
    const int ready = sceNetEpollWait(eid, nullptr, 10, 0);
    EXPECT_EQ(ready, 0);

    EXPECT_EQ(sceNetEpollDestroy(eid), 0);
}

// Verifies offline DNS resolver stub reports ENODNS failure cleanly.
TEST(OfflineNetStack, ResolverOfflineFailure) {
    const int rid = sceNetResolverCreate("test_resolver", 0, 0);
    EXPECT_GE(rid, 0);

    uint8_t outAddr[16] = {};
    const int res = sceNetResolverStartNtoa(rid, "playstation.com", outAddr, 1000, 1, 0);
    EXPECT_EQ(res, static_cast<int>(0x804101E1u)); // NET_ERROR_RESOLVER_ENODNS
    EXPECT_EQ(*sceNetErrnoLoc(), 60); // NET_ETIMEDOUT

    EXPECT_EQ(sceNetResolverDestroy(rid), 0);
}

// Verifies getsockname retrieves bound address and port.
TEST(OfflineNetStack, SockNameAndBinding) {
    const int sock = sceNetSocket("test_bind", NET_AF_INET, NET_SOCK_STREAM, 0);
    ASSERT_GE(sock, 0);

    uint8_t bindAddr[16] = {};
    bindAddr[0] = 16;
    bindAddr[1] = static_cast<uint8_t>(NET_AF_INET);
    uint16_t portBe = sceNetHtons_nid_postfix(8080);
    std::memcpy(bindAddr + 2, &portBe, 2);
    // 127.0.0.1
    bindAddr[4] = 127; bindAddr[5] = 0; bindAddr[6] = 0; bindAddr[7] = 1;

    EXPECT_EQ(sceNetBind_nid_postfix(sock, bindAddr, sizeof(bindAddr)), 0);

    uint8_t queriedAddr[16] = {};
    uint32_t queriedLen = sizeof(queriedAddr);
    EXPECT_EQ(sceNetGetsockname(sock, queriedAddr, &queriedLen), 0);
    EXPECT_EQ(queriedLen, 16u);

    uint16_t outPort = 0;
    std::memcpy(&outPort, queriedAddr + 2, 2);
    EXPECT_EQ(outPort, portBe);

    EXPECT_EQ(sceNetSocketClose(sock), 0);
}

// Verifies non-blocking listen and accept return EAGAIN when no incoming connection arrives.
TEST(OfflineNetStack, NonBlockingListenAndAccept) {
    const int sock = sceNetSocket("test_listener", NET_AF_INET, NET_SOCK_STREAM, 0);
    ASSERT_GE(sock, 0);

    int nbio = 1;
    EXPECT_EQ(sceNetSetsockopt(sock, NET_SOL_SOCKET, NET_SO_NBIO, &nbio, sizeof(nbio)), 0);
    EXPECT_EQ(sceNetListen(sock, 5), 0);

    uint8_t clientAddr[16] = {};
    uint32_t clientLen = sizeof(clientAddr);
    const int accepted = sceNetAccept(sock, clientAddr, &clientLen);
    EXPECT_EQ(accepted, -1);
    EXPECT_EQ(*sceNetErrnoLoc(), 35); // NET_EAGAIN

    EXPECT_EQ(sceNetSocketClose(sock), 0);
}

// Verifies recv and send on stream socket return ENOTCONN when unconnected.
TEST(OfflineNetStack, RecvAndSendUnconnectedStream) {
    const int sock = sceNetSocket("test_stream_unconnected", NET_AF_INET, NET_SOCK_STREAM, 0);
    ASSERT_GE(sock, 0);

    char buf[64] = "sample payload";
    const int64_t sent = sceNetSend(sock, buf, sizeof(buf), 0);
    EXPECT_EQ(sent, -1);
    EXPECT_EQ(*sceNetErrnoLoc(), 57); // NET_ENOTCONN

    char recvBuf[64] = {};
    const int64_t recvd = sceNetRecv(sock, recvBuf, sizeof(recvBuf), 0);
    EXPECT_EQ(recvd, -1);
    EXPECT_EQ(*sceNetErrnoLoc(), 57); // NET_ENOTCONN

    EXPECT_EQ(sceNetSocketClose(sock), 0);
}

// Verifies multi-threaded abort wakeups return NET_ECONNABORTED for all concurrent waiters.
TEST(OfflineNetStack, MultiThreadedSocketAbort) {
    const int sock = sceNetSocket("test_abort_listener", NET_AF_INET, NET_SOCK_STREAM, 0);
    ASSERT_GE(sock, 0);
    EXPECT_EQ(sceNetListen(sock, 5), 0);

    std::atomic<int> readyCount{0};
    std::atomic<int> rc1{0}, rc2{0};
    std::atomic<int> err1{0}, err2{0};

    std::thread t1([&]() {
        readyCount.fetch_add(1);
        uint8_t addr[16] = {};
        uint32_t len = sizeof(addr);
        rc1 = sceNetAccept(sock, addr, &len);
        err1 = *sceNetErrnoLoc();
    });

    std::thread t2([&]() {
        readyCount.fetch_add(1);
        uint8_t addr[16] = {};
        uint32_t len = sizeof(addr);
        rc2 = sceNetAccept(sock, addr, &len);
        err2 = *sceNetErrnoLoc();
    });

    while (readyCount.load() < 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    // Give waiters time to enter block_on
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    EXPECT_EQ(sceNetSocketAbort(sock, 0), 0);

    t1.join();
    t2.join();

    EXPECT_EQ(rc1.load(), -1);
    EXPECT_EQ(err1.load(), 53); // NET_ECONNABORTED
    EXPECT_EQ(rc2.load(), -1);
    EXPECT_EQ(err2.load(), 53); // NET_ECONNABORTED

    // Abort is sticky: a waiter entering after the wakeup must also observe it.
    EXPECT_EQ(sceNetAccept(sock, nullptr, nullptr), -1);
    EXPECT_EQ(*sceNetErrnoLoc(), 53); // NET_ECONNABORTED
    EXPECT_EQ(sceNetAccept(sock, nullptr, nullptr), -1);
    EXPECT_EQ(*sceNetErrnoLoc(), 53);

    EXPECT_EQ(sceNetSocketClose(sock), 0);
}

// Verifies sceNetTerm purges tracked sockets and state allowing clean re-init.
TEST(OfflineNetStack, TermPurgesStateAndAllowsReinit) {
    const int sock1 = sceNetSocket("term_test_sock", NET_AF_INET, NET_SOCK_STREAM, 0);
    ASSERT_GE(sock1, 0);
    EXPECT_EQ(sceNetListen(sock1, 5), 0);

    // Terminate network stack
    EXPECT_EQ(sceNetTerm(), 0);

    // Old socket handle should now be invalid
    EXPECT_EQ(sceNetSocketClose(sock1), -1);
    EXPECT_EQ(*sceNetErrnoLoc(), 9); // NET_EBADF

    // Re-initialize cleanly
    EXPECT_EQ(sceNetInit_nid_postfix(), 0);
    const int sock2 = sceNetSocket("reinit_sock", NET_AF_INET, NET_SOCK_STREAM, 0);
    EXPECT_GE(sock2, 0);
    EXPECT_EQ(sceNetSocketClose(sock2), 0);
}

// Verifies IPv6 socket binding, address parsing, and retrieval via sceNetGetsockname.
TEST(OfflineNetStack, Ipv6BindingAndSockname) {
    constexpr int NET_AF_INET6 = 28;
    const int sock = sceNetSocket("test_ipv6", NET_AF_INET6, NET_SOCK_STREAM, 0);
    ASSERT_GE(sock, 0);

    uint8_t bindAddr6[28] = {};
    bindAddr6[0] = 28;
    bindAddr6[1] = static_cast<uint8_t>(NET_AF_INET6);
    uint16_t portBe = sceNetHtons_nid_postfix(9090);
    std::memcpy(bindAddr6 + 2, &portBe, 2);
    // fe80::1
    ASSERT_EQ(sceNetInetPton(NET_AF_INET6, "fe80::1", bindAddr6 + 8), 1);

    EXPECT_EQ(sceNetBind_nid_postfix(sock, bindAddr6, sizeof(bindAddr6)), 0);

    uint8_t queriedAddr[28] = {};
    uint32_t queriedLen = sizeof(queriedAddr);
    EXPECT_EQ(sceNetGetsockname(sock, queriedAddr, &queriedLen), 0);
    EXPECT_EQ(queriedLen, 28u);
    EXPECT_EQ(queriedAddr[1], static_cast<uint8_t>(NET_AF_INET6));

    uint16_t outPort = 0;
    std::memcpy(&outPort, queriedAddr + 2, 2);
    EXPECT_EQ(outPort, portBe);

    char ipStr[64] = {};
    EXPECT_NE(sceNetInetNtop(NET_AF_INET6, queriedAddr + 8, ipStr, sizeof(ipStr)), nullptr);
    EXPECT_NE(std::strstr(ipStr, "fe80"), nullptr);

    EXPECT_EQ(sceNetSocketClose(sock), 0);
}

// IPv4 (including the legacy zero-family alias) cannot bind an IPv6 socket, or vice versa.
TEST(OfflineNetStack, BindRejectsMismatchedFamily) {
    const int v4 = sceNetSocket("family4", NET_AF_INET, NET_SOCK_STREAM, 0);
    const int v6 = sceNetSocket("family6", NET_AF_INET6, NET_SOCK_STREAM, 0);
    ASSERT_GE(v4, 0);
    ASSERT_GE(v6, 0);
    uint8_t addr[28] = {};
    addr[0] = sizeof(addr);
    for (uint8_t family : {0, NET_AF_INET, NET_AF_INET6}) {
        addr[1] = family;
        EXPECT_EQ(sceNetBind_nid_postfix(family == NET_AF_INET6 ? v4 : v6, addr, sizeof(addr)), -1);
        EXPECT_EQ(*sceNetErrnoLoc(), 47); // NET_EAFNOSUPPORT
    }
    EXPECT_EQ(sceNetSocketClose(v4), 0);
    EXPECT_EQ(sceNetSocketClose(v6), 0);
}

// Explicit port collisions require both the same family and socket type, in either bind order.
TEST(OfflineNetStack, BindPortCollisionUsesFamilyAndType) {
    for (int firstFamily : {NET_AF_INET, NET_AF_INET6}) {
        const int secondFamily = firstFamily == NET_AF_INET ? NET_AF_INET6 : NET_AF_INET;
        int sockets[4] = {};
        const int families[] = {firstFamily, secondFamily, firstFamily, firstFamily};
        const int types[] = {NET_SOCK_STREAM, NET_SOCK_STREAM, NET_SOCK_STREAM, NET_SOCK_DGRAM};
        for (int i = 0; i < 4; ++i) {
            sockets[i] = sceNetSocket("port_family", families[i], types[i], 0);
            ASSERT_GE(sockets[i], 0);
            uint8_t addr[28] = {};
            addr[0] = sizeof(addr);
            addr[1] = static_cast<uint8_t>(families[i]);
            const uint16_t port = sceNetHtons_nid_postfix(9080);
            std::memcpy(addr + 2, &port, sizeof(port));
            EXPECT_EQ(sceNetBind_nid_postfix(sockets[i], addr, sizeof(addr)), i == 2 ? -1 : 0);
            if (i == 2) EXPECT_EQ(*sceNetErrnoLoc(), 48); // NET_EADDRINUSE
        }
        for (int socket : sockets) EXPECT_EQ(sceNetSocketClose(socket), 0);
    }
}

// Strict hextets reject strtoul extensions and malformed separators without touching the output.
TEST(OfflineNetStack, Ipv6RejectsMalformedGroups) {
    const char* invalid[] = {
        "", ":", ":1", "1:2:3:4:5:6:7:8:", "1:2:3:4:5:6:7:",
        "1:2:3:4:5:6:7:8::", "::1:2:3:4:5:6:7:8", "1:2:3:4::5:6:7:8",
        ":::1", "1:::2", "1::2::3", "1:2:3:4:5:6:7", "1:2:3:4:5:6:7:8:9",
        "0x1::", "+1::", "-0::", " 1::", "\t1::", "1:: ", "1::+2", "1::0x2",
        "00000::", "10000::", "1::00000", "gggg::"
    };
    for (const char* address : invalid) {
        SCOPED_TRACE(address);
        uint8_t result[16];
        std::memset(result, 0xa5, sizeof(result));
        EXPECT_EQ(sceNetInetPton(NET_AF_INET6, address, result), 0);
        for (uint8_t byte : result) EXPECT_EQ(byte, 0xa5);
    }
}

// Expanded/compressed addresses preserve network byte order, including single-group compression.
TEST(OfflineNetStack, Ipv6AcceptsValidGroups) {
    struct Case {
        const char* text;
        uint16_t words[8];
    };
    const Case cases[] = {
        {"::", {}}, {"::1", {0, 0, 0, 0, 0, 0, 0, 1}},
        {"1::", {1}}, {"2001:db8::1", {0x2001, 0xdb8, 0, 0, 0, 0, 0, 1}},
        {"1:2:3:4:5:6:7:8", {1, 2, 3, 4, 5, 6, 7, 8}},
        {"::1:2:3:4:5:6:7", {0, 1, 2, 3, 4, 5, 6, 7}},
        {"1:2:3:4:5:6:7::", {1, 2, 3, 4, 5, 6, 7, 0}},
        {"1:2:3:4::6:7:8", {1, 2, 3, 4, 0, 6, 7, 8}},
        {"AbCd:0000:ffff:0:1:2:3:4", {0xabcd, 0, 0xffff, 0, 1, 2, 3, 4}}
    };
    for (const auto& test : cases) {
        SCOPED_TRACE(test.text);
        uint8_t result[16] = {};
        ASSERT_EQ(sceNetInetPton(NET_AF_INET6, test.text, result), 1);
        for (int i = 0; i < 8; ++i) {
            EXPECT_EQ(result[2 * i], test.words[i] >> 8);
            EXPECT_EQ(result[2 * i + 1], test.words[i] & 0xff);
        }
    }
}

// Verifies ephemeral port allocation wraps and avoids collision with already bound sockets.
TEST(OfflineNetStack, EphemeralPortAllocation) {
    const int s1 = sceNetSocket("ephemeral1", NET_AF_INET, NET_SOCK_STREAM, 0);
    const int s2 = sceNetSocket("ephemeral2", NET_AF_INET, NET_SOCK_STREAM, 0);
    ASSERT_GE(s1, 0);
    ASSERT_GE(s2, 0);

    EXPECT_EQ(sceNetListen(s1, 5), 0);
    EXPECT_EQ(sceNetListen(s2, 5), 0);

    uint8_t a1[16] = {}, a2[16] = {};
    uint32_t l1 = sizeof(a1), l2 = sizeof(a2);
    EXPECT_EQ(sceNetGetsockname(s1, a1, &l1), 0);
    EXPECT_EQ(sceNetGetsockname(s2, a2, &l2), 0);

    uint16_t p1 = 0, p2 = 0;
    std::memcpy(&p1, a1 + 2, 2);
    std::memcpy(&p2, a2 + 2, 2);

    p1 = sceNetNtohs_nid_postfix(p1);
    p2 = sceNetNtohs_nid_postfix(p2);

    EXPECT_GE(p1, 49152u);
    EXPECT_LE(p1, 65535u);
    EXPECT_GE(p2, 49152u);
    EXPECT_LE(p2, 65535u);
    EXPECT_NE(p1, p2);

    EXPECT_EQ(sceNetSocketClose(s1), 0);
    EXPECT_EQ(sceNetSocketClose(s2), 0);
}

} // namespace

