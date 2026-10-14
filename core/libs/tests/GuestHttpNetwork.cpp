#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>

extern "C" {
int APS5_VABI sceHttpInit_nid_postfix(int, int, std::uint64_t);
int APS5_VABI sceHttpTerm_nid_postfix(int);
int APS5_VABI sceHttpCreateTemplate(int, const char*, int, int);
int APS5_VABI sceHttpCreateConnectionWithURL(int, const char*, int);
int APS5_VABI sceHttpCreateConnection(int, const char*, const char*, std::uint16_t, int);
int APS5_VABI sceHttpCreateRequest(int, int, const char*, std::uint64_t);
int APS5_VABI sceHttpCreateRequest2(int, const char*, const char*, std::uint64_t);
int APS5_VABI sceHttpCreateRequestWithURL(int, int, const char*, std::uint64_t);
int APS5_VABI sceHttpCreateRequestWithURL2(int, const char*, const char*, std::uint64_t);
int APS5_VABI sceHttpDeleteRequest(int);
int APS5_VABI sceHttpDeleteConnection(int);
int APS5_VABI sceHttpDeleteTemplate(int);
int APS5_VABI sceHttpAddRequestHeader(int, const char*, const char*, std::uint32_t);
int APS5_VABI sceHttpSendRequest(int, const void*, std::size_t);
int APS5_VABI sceHttpReadData(int, void*, std::size_t);
int APS5_VABI sceHttpGetStatusCode(int, int*);
int APS5_VABI sceHttpGetAllResponseHeaders(int, char**, std::size_t*);
int APS5_VABI sceHttpGetResponseContentLength(int, int*, std::uint64_t*);
int APS5_VABI sceHttpSetRecvTimeOut(int, std::uint32_t);
int APS5_VABI sceHttpSetConnectTimeOut(int, std::uint32_t);
int APS5_VABI sceHttpSetSendTimeOut(int, std::uint32_t);
int APS5_VABI sceHttpSetResolveTimeOut(int, std::uint32_t);
int APS5_VABI sceHttpSetRecvBlockSize(int, std::uint32_t);
int APS5_VABI sceHttpSetResponseHeaderMaxSize(int, std::uint64_t);
int APS5_VABI sceHttpSetRequestContentLength(int, std::uint64_t);
int APS5_VABI sceHttpSetAutoRedirect(int, int);
int APS5_VABI sceHttpSetInflateGZIPEnabled(int, int);
int APS5_VABI sceHttpSetCookieEnabled(int, int);
int APS5_VABI sceHttpSetNonblock(int, int);
int APS5_VABI sceHttpGetLastErrno(int, int*);
int APS5_VABI sceHttpsGetSslError(int, int*, std::uint32_t*);
int APS5_VABI sceHttpsDisableOption(int, std::uint32_t);
int APS5_VABI sceHttpAbortRequest(int);
}

static void Require(bool value, const char* label) {
    if (!value) {
        std::fprintf(stderr, "HTTP network: %s\n", label);
        std::abort();
    }
}

static std::string ReadBody(int request) {
    std::string result;
    char buffer[32];
    for (;;) {
        const auto count = sceHttpReadData(request, buffer, sizeof(buffer));
        Require(count >= 0 && count <= static_cast<int>(sizeof(buffer)), "body read failed");
        if (!count) return result;
        result.append(buffer, count);
        Require(result.size() <= 4096, "body exceeded test limit");
    }
}

static int Get(int connection, const char* path) {
    const auto request = sceHttpCreateRequest(connection, 0, path, 0);
    Require(request > 0, "GET request creation");
    Require(sceHttpSendRequest(request, nullptr, 0) == 0, "GET send");
    return request;
}

static void WaitForServer(int connection, const std::string& tag) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto state = Get(connection, ("/state/" + tag).c_str());
        const auto received = ReadBody(state) == "1";
        Require(sceHttpDeleteRequest(state) == 0, "state request cleanup");
        if (received) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Require(false, "server did not receive pending request");
}

int main(int argc, char** argv) {
    Require(argc == 3, "HTTP and HTTPS base URLs required");
    constexpr int invalidId = static_cast<int>(0x80431100u);
    constexpr int invalidValue = static_cast<int>(0x804311feu);
    constexpr int aborted = static_cast<int>(0x80431080u);
    const auto context = sceHttpInit_nid_postfix(0, 0, 65536);
    Require(context > 0, "context creation");
    const auto tmpl = sceHttpCreateTemplate(context, "AnyPS5-local-test", 2, 0);
    Require(tmpl > 0, "template creation");
    Require(sceHttpSetConnectTimeOut(tmpl, 1000000) == 0 && sceHttpSetResolveTimeOut(tmpl, 1000000) == 0 && sceHttpSetSendTimeOut(tmpl, 1000000) == 0 && sceHttpSetRecvTimeOut(tmpl, 1000000) == 0, "timeout configuration");
    Require(sceHttpAddRequestHeader(tmpl, "X-Inherited", "template", 0) == 0, "template header");
    const auto connection = sceHttpCreateConnectionWithURL(tmpl, argv[1], 1);
    Require(connection > 0, "connection creation");
    Require(sceHttpCreateTemplate(-1, "x", 2, 0) == invalidId, "invalid parent");
    Require(sceHttpCreateConnectionWithURL(tmpl, "file:///local", 0) < 0, "non-HTTP URL accepted");
    Require(sceHttpCreateConnectionWithURL(tmpl, "http://user:secret@localhost/", 0) < 0, "URL credentials accepted");
    Require(sceHttpCreateRequest(connection, 99, "/", 0) < 0, "unknown method");
    Require(sceHttpCreateRequest2(connection, "GET\r\nX: y", "/", 0) == invalidValue, "method injection");
    Require(sceHttpAddRequestHeader(connection, "X-Bad\r\n", "a", 0) == invalidValue && sceHttpAddRequestHeader(connection, "X-Bad", "a\r\nHost: other", 0) == invalidValue, "header injection");
    Require(sceHttpSetRecvBlockSize(connection, 0) == invalidValue, "zero read block");
    Require(sceHttpSetAutoRedirect(connection, -1) == invalidValue, "invalid redirect value");
    const auto before = sceHttpCreateRequest(connection, 0, "/binary", 0);
    int status = 777;
    Require(sceHttpGetStatusCode(before, &status) == static_cast<int>(0x80431065u) && status == 777, "before-send status");
    Require(sceHttpSetRecvBlockSize(before, 3) == 0, "small read block");
    const auto binaryResult = sceHttpSendRequest(before, nullptr, 0);
    if (binaryResult != 0) {
        int error = 0;
        sceHttpGetLastErrno(before, &error);
        std::fprintf(stderr, "binary send result=0x%x host error=%d\n", binaryResult, error);
    }
    Require(binaryResult == 0, "binary send");
    Require(sceHttpGetStatusCode(before, &status) == 0 && status == 200, "binary status");
    char* headers = nullptr;
    std::size_t headerSize = 0;
    Require(sceHttpGetAllResponseHeaders(before, &headers, &headerSize) == 0 && headerSize == std::strlen(headers) + 1 && std::strstr(headers, "X-Test: local"), "response headers");
    int lengthResult = -1;
    std::uint64_t length = 999;
    Require(sceHttpGetResponseContentLength(before, &lengthResult, &length) == 0 && lengthResult == 0 && length == 8, "binary content length");
    char small[32];
    Require(sceHttpReadData(before, small, sizeof(small)) == 3 && std::memcmp(small, "abc", 3) == 0, "read block cap");
    Require(ReadBody(before) == std::string("\0tail", 5), "binary body and partial reads");
    Require(sceHttpReadData(before, nullptr, 0) == 0, "empty read");
    Require(sceHttpSendRequest(before, nullptr, 0) == static_cast<int>(0x80431066u), "send after send");
    Require(sceHttpDeleteRequest(before) == 0 && sceHttpGetStatusCode(before, &status) == invalidId, "deleted request");

    const auto chunked = Get(connection, "/chunked");
    Require(sceHttpGetResponseContentLength(chunked, &lengthResult, &length) == 0 && lengthResult == 1 && length == 0, "unknown response length");
    Require(ReadBody(chunked) == "firstsecond", "chunk framing leaked into body");
    Require(sceHttpDeleteRequest(chunked) == 0, "chunked cleanup");
    const auto post = sceHttpCreateRequestWithURL2(connection, "POST", (std::string(argv[1]) + "/echo").c_str(), 0);
    const std::string payload("post\0bytes", 10);
    Require(sceHttpSendRequest(post, payload.data(), payload.size()) == invalidValue, "body length mismatch");
    Require(sceHttpSetRequestContentLength(post, payload.size()) == 0, "content length update");
    Require(sceHttpSendRequest(post, payload.data(), payload.size()) == 0 && ReadBody(post) == payload, "POST echo");
    Require(sceHttpDeleteRequest(post) == 0, "POST cleanup");
    const auto head = sceHttpCreateRequest2(connection, "HEAD", "/binary", 0);
    Require(sceHttpSendRequest(head, nullptr, 0) == 0 && ReadBody(head).empty(), "HEAD body");
    Require(sceHttpGetResponseContentLength(head, &lengthResult, &length) == 0 && length == 8, "HEAD length");
    Require(sceHttpDeleteRequest(head) == 0, "HEAD cleanup");
    const auto missing = Get(connection, "/missing");
    Require(sceHttpGetStatusCode(missing, &status) == 0 && status == 404 && ReadBody(missing) == "missing", "HTTP error is a response");
    Require(sceHttpDeleteRequest(missing) == 0, "404 cleanup");
    const auto disconnected = sceHttpCreateRequest(connection, 0, "/disconnect", 0);
    Require(sceHttpSendRequest(disconnected, nullptr, 0) < 0, "connection failure accepted");
    int disconnectError = 0;
    Require(sceHttpGetLastErrno(disconnected, &disconnectError) == 0 && disconnectError != 0, "connection failure diagnostics");
    Require(sceHttpDeleteRequest(disconnected) == 0, "disconnected cleanup");
    const auto redirect = Get(connection, "/redirect");
    Require(sceHttpGetStatusCode(redirect, &status) == 0 && status == 200 && ReadBody(redirect) == "final", "redirect following");
    Require(sceHttpDeleteRequest(redirect) == 0, "redirect cleanup");
    const auto noRedirect = sceHttpCreateRequest(connection, 0, "/redirect", 0);
    Require(sceHttpSetAutoRedirect(noRedirect, 0) == 0 && sceHttpSendRequest(noRedirect, nullptr, 0) == 0, "redirect-disabled send");
    Require(sceHttpGetStatusCode(noRedirect, &status) == 0 && status == 302 && ReadBody(noRedirect).empty(), "redirect-disabled response");
    Require(sceHttpDeleteRequest(noRedirect) == 0, "redirect-disabled cleanup");
    for (const auto* header : {"aUtHoRiZaTiOn", "Proxy-Authorization", "Cookie"}) {
        const auto credentialTemplate = sceHttpCreateTemplate(context, "AnyPS5-local-test", 2, 0);
        Require(sceHttpAddRequestHeader(credentialTemplate, header, "local-test", 0) == 0, "inherited credential header setup");
        const auto credentialConnection = sceHttpCreateConnectionWithURL(credentialTemplate, argv[1], 0);
        const auto credentials = sceHttpCreateRequest(credentialConnection, 0, "/credentials", 0);
        bool rejected = false;
        try { sceHttpSendRequest(credentials, nullptr, 0); } catch (const std::runtime_error&) { rejected = true; }
        Require(rejected, "credential headers accepted with automatic redirects");
        Require(sceHttpSetAutoRedirect(credentials, 0) == 0 && sceHttpSendRequest(credentials, nullptr, 0) == 0 && ReadBody(credentials) == "local-test", "credential request without automatic redirects");
        Require(sceHttpDeleteTemplate(credentialTemplate) == 0, "credential request cleanup");
    }
    const auto gzip = sceHttpCreateRequest(connection, 0, "/gzip", 0);
    Require(sceHttpSetInflateGZIPEnabled(gzip, 1) == 0 && sceHttpSendRequest(gzip, nullptr, 0) == 0 && ReadBody(gzip) == "compressed body", "GZIP decoding");
    Require(sceHttpDeleteRequest(gzip) == 0, "GZIP cleanup");
    const auto headerRequest = sceHttpCreateRequest(connection, 0, "/headers", 0);
    Require(sceHttpAddRequestHeader(headerRequest, "X-Value", "old", 0) == 0 && sceHttpAddRequestHeader(headerRequest, "x-value", "new", 0) == 0 && sceHttpAddRequestHeader(headerRequest, "X-Value", "added", 1) == 0, "header overwrite and add");
    Require(sceHttpSendRequest(headerRequest, nullptr, 0) == 0 && ReadBody(headerRequest) == "template|new,added|AnyPS5-local-test", "inherited and repeated headers");
    Require(sceHttpDeleteRequest(headerRequest) == 0, "header cleanup");
    const auto huge = sceHttpCreateRequest(connection, 0, "/largeheaders", 0);
    Require(sceHttpSetResponseHeaderMaxSize(huge, 512) == 0, "response header cap");
    Require(sceHttpSendRequest(huge, nullptr, 0) == static_cast<int>(0x80431073u), "large header accepted");
    Require(sceHttpDeleteRequest(huge) == 0, "header-limit cleanup");

    const auto tlsConnection = sceHttpCreateConnectionWithURL(tmpl, argv[2], 0);
    const auto badTls = sceHttpCreateRequest(tlsConnection, 0, "/final", 0);
    Require(sceHttpSendRequest(badTls, nullptr, 0) == static_cast<int>(0x80431075u), "untrusted TLS certificate accepted by default");
    int hostError = 0;
    std::uint32_t sslDetail = 0;
    Require(sceHttpsGetSslError(badTls, &hostError, &sslDetail) == 0 && hostError != 0 && sslDetail != 0, "TLS diagnostics");
    Require(sceHttpDeleteRequest(badTls) == 0, "TLS failure cleanup");
    const auto tls = sceHttpCreateRequestWithURL(tlsConnection, 0, (std::string(argv[2]) + "/final").c_str(), 0);
    Require(sceHttpsDisableOption(tls, 0x20) == 0, "explicit test CA-check override");
    const auto tlsResult = sceHttpSendRequest(tls, nullptr, 0);
    if (tlsResult != 0) {
        sceHttpsGetSslError(tls, &hostError, &sslDetail);
        std::fprintf(stderr, "TLS send result=0x%x host error=%d ssl detail=0x%x\n", tlsResult, hostError, sslDetail);
    }
    Require(tlsResult == 0 && ReadBody(tls) == "final", "HTTPS transfer");
    Require(sceHttpDeleteRequest(tls) == 0, "HTTPS cleanup");

    const auto timeout = sceHttpCreateRequest(connection, 0, "/slowheaders/timeout", 0);
    Require(sceHttpSetRecvTimeOut(timeout, 100000) == 0, "receive timeout");
    const auto timeoutResult = sceHttpSendRequest(timeout, nullptr, 0);
    if (timeoutResult != static_cast<int>(0x80431068u)) std::fprintf(stderr, "timeout result=0x%x\n", timeoutResult);
    Require(timeoutResult == static_cast<int>(0x80431068u), "timeout result");
    Require(sceHttpGetLastErrno(timeout, &hostError) == 0 && hostError != 0, "timeout diagnostics");
    Require(sceHttpDeleteRequest(timeout) == 0, "timeout cleanup");
    const auto bodyTimeout = sceHttpCreateRequest(connection, 0, "/slowbody/timeout", 0);
    Require(sceHttpSetRecvTimeOut(bodyTimeout, 100000) == 0 && sceHttpSendRequest(bodyTimeout, nullptr, 0) == 0, "body timeout setup");
    Require(sceHttpReadData(bodyTimeout, small, sizeof(small)) == static_cast<int>(0x80431068u), "body receive timeout");
    Require(sceHttpReadData(bodyTimeout, small, sizeof(small)) == static_cast<int>(0x80431068u), "body timeout remains failed");
    Require(sceHttpDeleteRequest(bodyTimeout) == 0, "body timeout cleanup");
    const auto preAborted = sceHttpCreateRequest(connection, 0, "/final", 0);
    Require(sceHttpAbortRequest(preAborted) == 0 && sceHttpSendRequest(preAborted, nullptr, 0) == aborted, "abort before send");
    Require(sceHttpDeleteRequest(preAborted) == 0, "pre-aborted cleanup");
    const auto isolatedA = sceHttpCreateRequest(connection, 0, "/headers", 0);
    const auto isolatedB = sceHttpCreateRequest(connection, 0, "/headers", 0);
    Require(sceHttpAddRequestHeader(isolatedA, "X-Value", "A", 0) == 0 && sceHttpAddRequestHeader(isolatedB, "X-Value", "B", 0) == 0, "isolated headers setup");
    auto sendA = std::async(std::launch::async, [&] { return sceHttpSendRequest(isolatedA, nullptr, 0); });
    auto sendB = std::async(std::launch::async, [&] { return sceHttpSendRequest(isolatedB, nullptr, 0); });
    Require(sendA.get() == 0 && sendB.get() == 0 && ReadBody(isolatedA) == "template|A|AnyPS5-local-test" && ReadBody(isolatedB) == "template|B|AnyPS5-local-test", "simultaneous requests share state");
    Require(sceHttpDeleteRequest(isolatedA) == 0 && sceHttpDeleteRequest(isolatedB) == 0, "isolated requests cleanup");
    for (int i = 0; i < 5; ++i) {
        const auto tag = "abort" + std::to_string(i);
        const auto pending = sceHttpCreateRequest(connection, 0, ("/slowheaders/" + tag).c_str(), 0);
        Require(sceHttpSetRecvTimeOut(pending, 0) == 0, "infinite receive wait setup");
        auto send = std::async(std::launch::async, [&] { return sceHttpSendRequest(pending, nullptr, 0); });
        WaitForServer(connection, tag);
        Require(sceHttpSendRequest(pending, nullptr, 0) == static_cast<int>(0x80431021u), "concurrent request operation");
        Require(sceHttpAbortRequest(pending) == 0 && sceHttpAbortRequest(pending) == 0, "abort is repeatable");
        Require(send.wait_for(std::chrono::seconds(1)) == std::future_status::ready && send.get() == aborted, "abort did not wake send");
        Require(sceHttpDeleteRequest(pending) == 0, "aborted cleanup");
    }
    const auto pendingRead = Get(connection, "/slowbody/read");
    auto read = std::async(std::launch::async, [&] { char byte; return sceHttpReadData(pendingRead, &byte, 1); });
    Require(read.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout, "slow body was not pending");
    Require(sceHttpAbortRequest(pendingRead) == 0, "abort pending read");
    Require(read.wait_for(std::chrono::seconds(1)) == std::future_status::ready && read.get() == aborted, "abort did not wake read");
    Require(sceHttpDeleteRequest(pendingRead) == 0, "aborted read cleanup");
    const auto child = sceHttpCreateConnectionWithURL(tmpl, argv[1], 0);
    const auto pendingDelete = sceHttpCreateRequest(child, 0, "/slowheaders/delete", 0);
    auto deletedSend = std::async(std::launch::async, [&] { return sceHttpSendRequest(pendingDelete, nullptr, 0); });
    WaitForServer(connection, "delete");
    Require(sceHttpDeleteConnection(child) == 0, "delete active parent");
    Require(deletedSend.wait_for(std::chrono::seconds(1)) == std::future_status::ready && deletedSend.get() == aborted, "parent delete did not cancel child");
    Require(sceHttpDeleteRequest(pendingDelete) == invalidId, "deleted descendant survives");

    bool unsupported = false;
    try { sceHttpSetNonblock(connection, 1); } catch (const std::runtime_error&) { unsupported = true; }
    Require(unsupported, "nonblocking silently accepted");
    unsupported = false;
    try { sceHttpSetCookieEnabled(connection, 1); } catch (const std::runtime_error&) { unsupported = true; }
    Require(unsupported, "cookies silently accepted");
    Require(sceHttpTerm_nid_postfix(context) == 0 && sceHttpDeleteTemplate(tmpl) == invalidId && sceHttpTerm_nid_postfix(context) == invalidId, "context teardown invalidates descendants");
    std::puts("HTTP network checks passed");
}
