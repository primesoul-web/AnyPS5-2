#pragma once
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>

namespace KernelSocketConnect {

inline bool IsLoopback(const sockaddr* address) {
    if (address->sa_family == AF_INET) return (ntohl(reinterpret_cast<const sockaddr_in*>(address)->sin_addr.s_addr) >> 24) == 127;
    if (address->sa_family == AF_INET6) return IN6_IS_ADDR_LOOPBACK(&reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr);
    return false;
}

inline void RefuseLoopbackAtOnce(SOCKET socket, const sockaddr* address) {
    if (!IsLoopback(address)) return;
    TCP_INITIAL_RTO_PARAMETERS parameters{TCP_INITIAL_RTO_UNSPECIFIED_RTT, TCP_INITIAL_RTO_NO_SYN_RETRANSMISSIONS};
    DWORD returned = 0;
    WSAIoctl(socket, SIO_TCP_INITIAL_RTO, &parameters, sizeof(parameters), nullptr, 0, &returned, nullptr, nullptr);
}

}
#endif
