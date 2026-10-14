#ifndef CORE_LIBS_PRX_LIBSCEHTTP_SRC_TRANSPORT_HPP
#define CORE_LIBS_PRX_LIBSCEHTTP_SRC_TRANSPORT_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace HttpNetwork {

struct Settings {
    std::string agent;
    int version = 2;
    bool redirect = true;
    bool gzip = false;
    bool keepAlive = true;
    std::uint32_t resolveTimeout = 30000000;
    std::uint32_t connectTimeout = 30000000;
    std::uint32_t sendTimeout = 30000000;
    std::uint32_t receiveTimeout = 30000000;
    std::uint64_t headerLimit = 16384;
    std::uint32_t sslFlags = 0x3d;
    std::uint32_t receiveBlockSize = 16384;
    std::vector<std::pair<std::string, std::string>> headers;
};

class ITransport {
public:
    virtual ~ITransport() = default;
    virtual int Send(const Settings& settings, const std::string& url, const std::string& method, const void* data, std::size_t size) = 0;
    virtual int Read(void* data, std::size_t size) = 0;
    virtual void Abort() = 0;
    virtual int Error() const = 0;
    virtual std::uint32_t SslError() const = 0;
    std::string headers;
    int status = 0;
    bool hasLength = false;
    std::uint64_t length = 0;
};

extern "C" std::shared_ptr<ITransport> CreateTransport_nid_no_patch();

}

#endif
