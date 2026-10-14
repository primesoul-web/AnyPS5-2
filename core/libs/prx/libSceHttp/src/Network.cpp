#include "Network.hpp"
#include "Transport.hpp"
#include "HttpErrors.hpp"
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <map>
#include <mutex>
#include <string_view>

extern "C" int APS5_VABI sceHttpUriParse(SceHttpUriElement*, const char*, void*, std::size_t*, std::size_t);

namespace HttpNetwork {
namespace {

struct Node {
    Kind kind;
    int parent;
    Settings settings;
    std::string url;
    std::string method;
    std::uint64_t contentLength = 0;
    std::mutex mutex;
    std::mutex operation;
    std::shared_ptr<ITransport> transport;
    bool deleted = false;
    bool aborted = false;
    bool sent = false;
    bool ready = false;
    int result = ERROR_BEFORE_SEND;
};

std::mutex registryMutex;
std::map<int, std::shared_ptr<Node>> registry;
int nextHandle = 1;

std::shared_ptr<Node> find(int id) {
    std::lock_guard lock(registryMutex);
    const auto item = registry.find(id);
    return item == registry.end() ? nullptr : item->second;
}

int insert(Kind kind, int parent, Settings settings, std::string url = {}, std::string method = {}, std::uint64_t length = 0) {
    auto node = std::make_shared<Node>();
    node->kind = kind;
    node->parent = parent;
    node->settings = std::move(settings);
    node->url = std::move(url);
    node->method = std::move(method);
    node->contentLength = length;
    std::lock_guard lock(registryMutex);
    if (parent != 0 && !registry.contains(parent)) return ERROR_INVALID_ID;
    if (nextHandle == std::numeric_limits<int>::max()) return ERROR_OUT_OF_MEMORY;
    const auto id = nextHandle++;
    registry.emplace(id, std::move(node));
    return id;
}

char lower(char value) {
    return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
}

bool equal(std::string_view left, std::string_view right) {
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) { return lower(a) == lower(b); });
}

bool token(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos;
    });
}

int validateUrl(const char* url, std::string* origin = nullptr) {
    if (!url) return ERROR_INVALID_URL;
    std::size_t size = 0;
    auto result = sceHttpUriParse(nullptr, url, nullptr, &size, 0);
    if (result != 0) return result;
    std::vector<char> pool(size);
    SceHttpUriElement parts{};
    result = sceHttpUriParse(&parts, url, pool.data(), nullptr, pool.size());
    if (result != 0) return result;
    if ((!equal(parts.scheme, "http") && !equal(parts.scheme, "https")) || parts.opaque || parts.hostname[0] == '\0') return ERROR_INVALID_URL;
    if (parts.username[0] || parts.password[0]) return ERROR_INVALID_URL;
    if (origin) {
        *origin = equal(parts.scheme, "https") ? "https://" : "http://";
        const std::string_view host(parts.hostname);
        if (host.find(':') != std::string_view::npos) *origin += '[';
        origin->append(host);
        if (host.find(':') != std::string_view::npos) *origin += ']';
        *origin += ':' + std::to_string(parts.port);
    }
    return 0;
}

}

bool Enabled_nid_no_patch() {
    static const bool enabled = [] {
        const char* value = std::getenv("APS5_HTTP_NETWORK");
        if (!value || !*value || std::string_view(value) == "0") return false;
        if (std::string_view(value) != "1") throw std::runtime_error("APS5_HTTP_NETWORK: expected 0 or 1");
#ifndef _WIN32
        throw std::runtime_error("APS5_HTTP_NETWORK: host HTTP is only implemented on Windows");
#endif
        return true;
    }();
    return enabled;
}

int Init_nid_no_patch(std::uint64_t poolSize) {
    if (poolSize == 0) return ERROR_INVALID_VALUE;
    return insert(Kind::Context, 0, {});
}

int Template_nid_no_patch(int context, const char* agent, int version, int proxy) {
    auto parent = find(context);
    if (!parent || parent->kind != Kind::Context) return ERROR_INVALID_ID;
    if (!agent || (version != 1 && version != 2) || static_cast<unsigned>(proxy) > 1) return ERROR_INVALID_VALUE;
    if (proxy) NotImplemented_nid_no_patch("sceHttpCreateTemplate: automatic proxy configuration");
    std::lock_guard lock(parent->mutex);
    auto settings = parent->settings;
    settings.agent = agent;
    settings.version = version;
    return insert(Kind::Template, context, std::move(settings));
}

int ConnectionUrl_nid_no_patch(int id, const char* url, int keepAlive) {
    auto parent = find(id);
    if (!parent || parent->kind != Kind::Template) return ERROR_INVALID_ID;
    if (static_cast<unsigned>(keepAlive) > 1) return ERROR_INVALID_VALUE;
    std::string origin;
    const auto result = validateUrl(url, &origin);
    if (result != 0) return result;
    std::lock_guard lock(parent->mutex);
    auto settings = parent->settings;
    settings.keepAlive = keepAlive != 0;
    return insert(Kind::Connection, id, std::move(settings), std::move(origin));
}

int ConnectionHost_nid_no_patch(int id, const char* host, const char* scheme, std::uint16_t port, int keepAlive) {
    if (!host || !scheme) return ERROR_INVALID_VALUE;
    std::string url = std::string(scheme) + "://";
    const std::string_view name(host);
    if (name.find(':') != std::string_view::npos && !name.starts_with('[')) url += '[';
    url += host;
    if (name.find(':') != std::string_view::npos && !name.starts_with('[')) url += ']';
    if (port != 0) url += ':' + std::to_string(port);
    return ConnectionUrl_nid_no_patch(id, url.c_str(), keepAlive);
}

int RequestMethod_nid_no_patch(int id, const char* method, const char* path, std::uint64_t length, bool absolute) {
    auto parent = find(id);
    if (!parent || parent->kind != Kind::Connection) return ERROR_INVALID_ID;
    if (!method || !token(method) || !path) return ERROR_INVALID_VALUE;
    if (equal(method, "CONNECT")) NotImplemented_nid_no_patch("sceHttpCreateRequest: CONNECT tunnels");
    std::lock_guard lock(parent->mutex);
    const auto url = absolute ? std::string(path) : parent->url + (path[0] == '/' ? "" : "/") + path;
    const auto result = validateUrl(url.c_str());
    if (result != 0) return result;
    return insert(Kind::Request, id, parent->settings, url, method, length);
}

int RequestIndex_nid_no_patch(int id, int method, const char* path, std::uint64_t length, bool absolute) {
    constexpr const char* methods[] = {"GET", "POST", "HEAD", "OPTIONS", "PUT", "DELETE", "TRACE", "CONNECT"};
    if (method < 0 || method >= 8) return ERROR_UNKNOWN_METHOD;
    return RequestMethod_nid_no_patch(id, methods[method], path, length, absolute);
}

int Delete_nid_no_patch(int id, Kind kind) {
    std::vector<std::shared_ptr<Node>> removed;
    {
        std::lock_guard lock(registryMutex);
        const auto item = registry.find(id);
        if (item == registry.end() || item->second->kind != kind) return ERROR_INVALID_ID;
        std::vector<int> ids{id};
        for (std::size_t i = 0; i < ids.size(); ++i) {
            for (const auto& [child, node] : registry) if (node->parent == ids[i]) ids.push_back(child);
        }
        for (const auto child : ids) {
            removed.push_back(registry.at(child));
            registry.erase(child);
        }
    }
    for (const auto& node : removed) {
        std::shared_ptr<ITransport> transport;
        {
            std::lock_guard lock(node->mutex);
            node->deleted = true;
            node->aborted = true;
            transport = node->transport;
        }
        if (transport) transport->Abort();
    }
    return 0;
}

int Configure_nid_no_patch(int id, Option option, std::uint64_t value) {
    const auto node = find(id);
    if (!node) return ERROR_INVALID_ID;
    std::lock_guard lock(node->mutex);
    if (node->deleted) return ERROR_INVALID_ID;
    if (node->sent) return ERROR_AFTER_SEND;
    auto& settings = node->settings;
    switch (option) {
    case Option::Redirect:
    case Option::Gzip:
        if (value > 1) return ERROR_INVALID_VALUE;
        if (option == Option::Redirect) settings.redirect = value != 0;
        if (option == Option::Gzip) settings.gzip = value != 0;
        break;
    case Option::ResolveTimeout: settings.resolveTimeout = static_cast<std::uint32_t>(value); break;
    case Option::ConnectTimeout: settings.connectTimeout = static_cast<std::uint32_t>(value); break;
    case Option::SendTimeout: settings.sendTimeout = static_cast<std::uint32_t>(value); break;
    case Option::ReceiveTimeout: settings.receiveTimeout = static_cast<std::uint32_t>(value); break;
    case Option::HeaderLimit:
        if (value == 0 || value > std::numeric_limits<std::uint32_t>::max()) return ERROR_INVALID_VALUE;
        settings.headerLimit = value;
        break;
    case Option::ContentLength:
        if (node->kind != Kind::Request) return ERROR_INVALID_ID;
        node->contentLength = value;
        break;
    case Option::ReceiveBlockSize:
        if (value == 0 || value > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) return ERROR_INVALID_VALUE;
        settings.receiveBlockSize = static_cast<std::uint32_t>(value);
        break;
    }
    return 0;
}

int SslOptions_nid_no_patch(int id, std::uint32_t flags, bool enable) {
    const auto node = find(id);
    if (!node) return ERROR_INVALID_ID;
    if (flags & ~0xffu) return ERROR_INVALID_VALUE;
    if (flags & 0xc2u) NotImplemented_nid_no_patch("sceHttpsEnableOption: client verification, session reuse or SNI control");
    std::lock_guard lock(node->mutex);
    if (node->deleted) return ERROR_INVALID_ID;
    if (node->sent) return ERROR_AFTER_SEND;
    const auto proposed = enable ? node->settings.sslFlags | flags : node->settings.sslFlags & ~flags;
    if ((proposed & 0x18u) != 0 && (proposed & 0x18u) != 0x18u) NotImplemented_nid_no_patch("sceHttpsEnableOption: separate certificate date checks");
    node->settings.sslFlags = proposed;
    return 0;
}

int Header_nid_no_patch(int id, const char* name, const char* value, std::uint32_t mode) {
    const auto node = find(id);
    if (!node) return ERROR_INVALID_ID;
    if (!name || !value || !token(name) || mode > 1 || std::string_view(value).find_first_of("\r\n") != std::string_view::npos) return ERROR_INVALID_VALUE;
    if (equal(name, "Content-Length") || equal(name, "Transfer-Encoding")) NotImplemented_nid_no_patch("sceHttpAddRequestHeader: framing headers");
    std::lock_guard lock(node->mutex);
    if (node->deleted) return ERROR_INVALID_ID;
    if (node->sent) return ERROR_AFTER_SEND;
    auto& headers = node->settings.headers;
    if (mode == 0) std::erase_if(headers, [&](const auto& header) { return equal(header.first, name); });
    headers.emplace_back(name, value);
    return 0;
}

int Send_nid_no_patch(int id, const void* data, std::size_t size) {
    const auto node = find(id);
    if (!node || node->kind != Kind::Request) return ERROR_INVALID_ID;
    if (size != 0 && !data) return ERROR_INVALID_VALUE;
    std::unique_lock operation(node->operation, std::try_to_lock);
    if (!operation.owns_lock()) return ERROR_HTTP_BUSY;
    std::shared_ptr<ITransport> transport;
    Settings settings;
    {
        std::lock_guard lock(node->mutex);
        if (node->deleted) return ERROR_INVALID_ID;
        if (node->aborted) return ERROR_HTTP_ABORTED;
        if (node->sent) return ERROR_AFTER_SEND;
        if (node->contentLength != size) return ERROR_INVALID_VALUE;
        if (node->settings.redirect && std::any_of(node->settings.headers.begin(), node->settings.headers.end(), [](const auto& header) {
            return equal(header.first, "Authorization") || equal(header.first, "Proxy-Authorization") || equal(header.first, "Cookie");
        })) NotImplemented_nid_no_patch("sceHttpSendRequest: automatic redirects with credential headers");
        transport = CreateTransport_nid_no_patch();
        node->transport = transport;
        node->sent = true;
        settings = node->settings;
    }
    const auto result = transport->Send(settings, node->url, node->method, data, size);
    std::lock_guard lock(node->mutex);
    node->result = node->aborted ? ERROR_HTTP_ABORTED : result;
    node->ready = node->result == 0;
    return node->result;
}

int Read_nid_no_patch(int id, void* data, std::size_t size) {
    const auto node = find(id);
    if (!node || node->kind != Kind::Request) return ERROR_INVALID_ID;
    if (size && !data) return ERROR_INVALID_VALUE;
    std::unique_lock operation(node->operation, std::try_to_lock);
    if (!operation.owns_lock()) return ERROR_HTTP_BUSY;
    std::shared_ptr<ITransport> transport;
    std::size_t blockSize = 0;
    {
        std::lock_guard lock(node->mutex);
        if (node->deleted) return ERROR_INVALID_ID;
        if (node->aborted) return ERROR_HTTP_ABORTED;
        if (!node->ready) return node->result;
        transport = node->transport;
        blockSize = node->settings.receiveBlockSize;
    }
    if (size == 0) return 0;
    const auto result = transport->Read(data, std::min(size, blockSize));
    std::lock_guard lock(node->mutex);
    if (node->aborted) return ERROR_HTTP_ABORTED;
    if (result < 0) {
        node->ready = false;
        node->result = result;
    }
    return result;
}

int Status_nid_no_patch(int id, int* status) {
    const auto node = find(id);
    if (!node || node->kind != Kind::Request) return ERROR_INVALID_ID;
    if (!status) return ERROR_INVALID_VALUE;
    std::lock_guard lock(node->mutex);
    if (!node->ready || node->aborted) return node->aborted ? ERROR_HTTP_ABORTED : node->result;
    *status = node->transport->status;
    return 0;
}

int Headers_nid_no_patch(int id, char** headers, std::size_t* size) {
    const auto node = find(id);
    if (!node || node->kind != Kind::Request) return ERROR_INVALID_ID;
    if (!headers || !size) return ERROR_INVALID_VALUE;
    std::lock_guard lock(node->mutex);
    if (!node->ready || node->aborted) return node->aborted ? ERROR_HTTP_ABORTED : node->result;
    *headers = node->transport->headers.data();
    *size = node->transport->headers.size() + 1;
    return 0;
}

int Length_nid_no_patch(int id, int* result, std::uint64_t* length) {
    const auto node = find(id);
    if (!node || node->kind != Kind::Request) return ERROR_INVALID_ID;
    if (!result || !length) return ERROR_INVALID_VALUE;
    std::lock_guard lock(node->mutex);
    if (!node->ready || node->aborted) return node->aborted ? ERROR_HTTP_ABORTED : node->result;
    *result = node->transport->hasLength ? 0 : 1;
    *length = node->transport->hasLength ? node->transport->length : 0;
    return 0;
}

int Abort_nid_no_patch(int id) {
    const auto node = find(id);
    if (!node || node->kind != Kind::Request) return ERROR_INVALID_ID;
    std::shared_ptr<ITransport> transport;
    {
        std::lock_guard lock(node->mutex);
        node->aborted = true;
        transport = node->transport;
    }
    if (transport) transport->Abort();
    return 0;
}

int Error_nid_no_patch(int id, int* error) {
    const auto node = find(id);
    if (!node || node->kind != Kind::Request) return ERROR_INVALID_ID;
    if (!error) return ERROR_INVALID_VALUE;
    std::lock_guard lock(node->mutex);
    *error = node->transport ? node->transport->Error() : 0;
    return 0;
}

int SslError_nid_no_patch(int id, int* error, std::uint32_t* detail) {
    if (!detail) return ERROR_INVALID_VALUE;
    const auto result = Error_nid_no_patch(id, error);
    if (result != 0) return result;
    const auto node = find(id);
    if (!node) return ERROR_INVALID_ID;
    std::lock_guard lock(node->mutex);
    *detail = node->transport ? node->transport->SslError() : 0;
    return 0;
}

int Unsupported_nid_no_patch(int id, bool requested, const char* function) {
    if (!find(id)) return ERROR_INVALID_ID;
    if (requested) NotImplemented_nid_no_patch(function);
    return 0;
}

#ifndef _WIN32
std::shared_ptr<ITransport> CreateTransport_nid_no_patch() {
    throw std::runtime_error("host HTTP is only implemented on Windows");
}
#endif

}
