// tests/net/OfflineNetStackTests.cpp
// GoogleTest-based verification suite for PortPS5 libSceNet offline stack implementation:
// Verifies socket lifecycle, address bindings, local error reporting, conversion utilities,
// and non-blocking epoll behavior under the offline model.
// Complies with System V ABI invariants and PortPS5 testing rules.

#include "common/TestHarness.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

#include <cstdint>
#include <cstring>

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
}

namespace {

using namespace PortPS5::Testing;

constexpr int NET_AF_INET = 2;
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

} // namespace
