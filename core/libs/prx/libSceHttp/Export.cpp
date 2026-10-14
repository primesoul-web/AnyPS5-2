#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libSceHttp/src/HttpErrors.hpp"
#include "prx/libSceHttp/src/Network.hpp"
#include "prx/libSceHttp/src/Offline.hpp"

namespace {

int UnsupportedEpoll_nid_no_patch(const char* function) {
    NotImplemented_nid_no_patch(function);
    return 0;
}

int CookieEnabled_nid_no_patch(int id, int enable) {
    if (static_cast<std::uint32_t>(enable) > 1) return ERROR_INVALID_VALUE;
    return HttpNetwork::Unsupported_nid_no_patch(id, enable != 0, "sceHttpSetCookieEnabled");
}

}

extern "C" {

int APS5_VABI sceHttpAbortRequest(int request_id) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Abort_nid_no_patch(request_id)
        : HttpOffline::OfflineHttpAbortRequest_nid_no_patch(request_id);
}

int APS5_VABI sceHttpAddRequestHeader(int id, const char* name, const char* value, uint32_t mode) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Header_nid_no_patch(id, name, value, mode)
        : HttpOffline::OfflineHttpAddRequestHeader_nid_no_patch(id, name, value, mode);
}

int APS5_VABI sceHttpCreateConnection(int tmpl_id, const char* server_name, const char* scheme, uint16_t port, int enable_keep_alive) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::ConnectionHost_nid_no_patch(tmpl_id, server_name, scheme, port, enable_keep_alive)
        : HttpOffline::OfflineHttpCreateConnection_nid_no_patch(tmpl_id, server_name, scheme, port, enable_keep_alive);
}

int APS5_VABI sceHttpCreateConnectionWithURL(int tmpl_id, const char* url, int enable_keep_alive) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::ConnectionUrl_nid_no_patch(tmpl_id, url, enable_keep_alive)
        : HttpOffline::OfflineHttpCreateConnectionWithURL_nid_no_patch(tmpl_id, url, enable_keep_alive);
}

int APS5_VABI sceHttpCreateEpoll(int http_ctx_id, HttpEpollHandle* eh) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(http_ctx_id, true, __func__)
        : HttpOffline::OfflineHttpCreateEpoll_nid_no_patch(http_ctx_id, eh);
}

int APS5_VABI sceHttpCreateRequest(int conn_id, int method, const char* path, uint64_t content_length) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::RequestIndex_nid_no_patch(conn_id, method, path, content_length, false)
        : HttpOffline::OfflineHttpCreateRequest_nid_no_patch(conn_id, method, path, content_length);
}

int APS5_VABI sceHttpCreateRequestWithURL2(int conn_id, const char* method, const char* url, uint64_t content_length) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::RequestMethod_nid_no_patch(conn_id, method, url, content_length, true)
        : HttpOffline::OfflineHttpCreateRequestWithURL2_nid_no_patch(conn_id, method, url, content_length);
}

int APS5_VABI sceHttpCreateTemplate(int http_ctx_id, const char* user_agent, int http_ver, int is_auto_proxy_conf) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Template_nid_no_patch(http_ctx_id, user_agent, http_ver, is_auto_proxy_conf)
        : HttpOffline::OfflineHttpCreateTemplate_nid_no_patch(http_ctx_id, user_agent, http_ver, is_auto_proxy_conf);
}

int APS5_VABI sceHttpDeleteConnection(int conn_id) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Delete_nid_no_patch(conn_id, HttpNetwork::Kind::Connection)
        : HttpOffline::OfflineHttpDeleteConnection_nid_no_patch(conn_id);
}

int APS5_VABI sceHttpDeleteRequest(int req_id) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Delete_nid_no_patch(req_id, HttpNetwork::Kind::Request)
        : HttpOffline::OfflineHttpDeleteRequest_nid_no_patch(req_id);
}

int APS5_VABI sceHttpDeleteTemplate(int tmpl_id) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Delete_nid_no_patch(tmpl_id, HttpNetwork::Kind::Template)
        : HttpOffline::OfflineHttpDeleteTemplate_nid_no_patch(tmpl_id);
}

int APS5_VABI sceHttpDestroyEpoll(int http_ctx_id, HttpEpollHandle eh) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(http_ctx_id, true, __func__)
        : HttpOffline::OfflineHttpDestroyEpoll_nid_no_patch(http_ctx_id, eh);
}

int APS5_VABI sceHttpGetAllResponseHeaders(int request_id, char** header, size_t* header_size) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Headers_nid_no_patch(request_id, header, header_size)
        : HttpOffline::OfflineHttpGetAllResponseHeaders_nid_no_patch(request_id, header, header_size);
}

int APS5_VABI sceHttpGetResponseContentLength(int request_id, int* result, uint64_t* content_length) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Length_nid_no_patch(request_id, result, content_length)
        : HttpOffline::OfflineHttpGetResponseContentLength_nid_no_patch(request_id, result, content_length);
}

int APS5_VABI sceHttpGetStatusCode(int request_id, int* status_code) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Status_nid_no_patch(request_id, status_code)
        : HttpOffline::OfflineHttpGetStatusCode_nid_no_patch(request_id, status_code);
}

int APS5_VABI sceHttpInit_nid_postfix(int memid, int ssl_ctx_id, uint64_t pool_size) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Init_nid_no_patch(pool_size)
        : HttpOffline::OfflineHttpInit_nid_no_patch(memid, ssl_ctx_id, pool_size);
}

int APS5_VABI sceHttpsDisableOption(int id, uint32_t ssl_flags) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::SslOptions_nid_no_patch(id, ssl_flags, false)
        : HttpOffline::OfflineHttpsDisableOption_nid_no_patch(id, ssl_flags);
}

int APS5_VABI sceHttpSendRequest(int request_id, const void* post_data, size_t size) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Send_nid_no_patch(request_id, post_data, size)
        : HttpOffline::OfflineHttpSendRequest_nid_no_patch(request_id, post_data, size);
}

int APS5_VABI sceHttpSetAuthEnabled(int id, int enable) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(id, enable != 0, __func__)
        : HttpOffline::OfflineHttpSetAuthEnabled_nid_no_patch(id, enable);
}

int APS5_VABI sceHttpSetAuthInfoCallback(int id, HttpAuthInfoCallback callback, void* userArg) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(id, callback != nullptr, __func__)
        : HttpOffline::OfflineHttpSetAuthInfoCallback_nid_no_patch(id, callback, userArg);
}

int APS5_VABI sceHttpSetCookieEnabled(int id, int enable) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? CookieEnabled_nid_no_patch(id, enable)
        : HttpOffline::OfflineHttpSetCookieEnabled_nid_no_patch(id, enable);
}

int APS5_VABI sceHttpSetAutoRedirect(int id, int enable) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Configure_nid_no_patch(id, HttpNetwork::Option::Redirect, static_cast<std::uint64_t>(enable))
        : HttpOffline::OfflineHttpSetAutoRedirect_nid_no_patch(id, enable);
}

int APS5_VABI sceHttpSetConnectTimeOut(int id, uint32_t usec) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Configure_nid_no_patch(id, HttpNetwork::Option::ConnectTimeout, usec)
        : HttpOffline::OfflineHttpSetConnectTimeOut_nid_no_patch(id, usec);
}

int APS5_VABI sceHttpSetEpoll(int id, HttpEpollHandle eh, void* user_arg) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(id, true, __func__)
        : HttpOffline::OfflineHttpSetEpoll_nid_no_patch(id, eh, user_arg);
}

int APS5_VABI sceHttpSetNonblock(int id, int enable) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(id, enable != 0, __func__)
        : HttpOffline::OfflineHttpSetNonblock_nid_no_patch(id, enable);
}

int APS5_VABI sceHttpSetRecvTimeOut(int id, uint32_t usec) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Configure_nid_no_patch(id, HttpNetwork::Option::ReceiveTimeout, usec)
        : HttpOffline::OfflineHttpSetRecvTimeOut_nid_no_patch(id, usec);
}

int APS5_VABI sceHttpSetRecvBlockSize(int id, uint32_t block_size) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Configure_nid_no_patch(id, HttpNetwork::Option::ReceiveBlockSize, block_size)
        : HttpOffline::OfflineHttpSetRecvBlockSize_nid_no_patch(id, block_size);
}

int APS5_VABI sceHttpSetResponseHeaderMaxSize(int id, uint64_t header_size) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Configure_nid_no_patch(id, HttpNetwork::Option::HeaderLimit, header_size)
        : HttpOffline::OfflineHttpSetResponseHeaderMaxSize_nid_no_patch(id, header_size);
}

int APS5_VABI sceHttpSetRequestContentLength(int request_id, uint64_t content_length) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Configure_nid_no_patch(request_id, HttpNetwork::Option::ContentLength, content_length)
        : HttpOffline::OfflineHttpSetRequestContentLength_nid_no_patch(request_id, content_length);
}

int APS5_VABI sceHttpSetResolveRetry(int id, int32_t retry) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(id, retry != 0, __func__)
        : HttpOffline::OfflineHttpSetResolveRetry_nid_no_patch(id, retry);
}

int APS5_VABI sceHttpSetResolveTimeOut(int id, uint32_t usec) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Configure_nid_no_patch(id, HttpNetwork::Option::ResolveTimeout, usec)
        : HttpOffline::OfflineHttpSetResolveTimeOut_nid_no_patch(id, usec);
}

int APS5_VABI sceHttpSetSendTimeOut(int id, uint32_t usec) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Configure_nid_no_patch(id, HttpNetwork::Option::SendTimeout, usec)
        : HttpOffline::OfflineHttpSetSendTimeOut_nid_no_patch(id, usec);
}

int APS5_VABI sceHttpsSetMinSslVersion(int id, uint32_t ssl_version) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(id, ssl_version != 0, __func__)
        : HttpOffline::OfflineHttpsSetMinSslVersion_nid_no_patch(id, ssl_version);
}

int APS5_VABI sceHttpsSetSslCallback(int id, HttpsCallback cbfunc, void* user_arg) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(id, cbfunc != nullptr, __func__)
        : HttpOffline::OfflineHttpsSetSslCallback_nid_no_patch(id, cbfunc, user_arg);
}

int APS5_VABI sceHttpTerm_nid_postfix(int http_ctx_id) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Delete_nid_no_patch(http_ctx_id, HttpNetwork::Kind::Context)
        : HttpOffline::OfflineHttpTerm_nid_no_patch(http_ctx_id);
}

int APS5_VABI sceHttpUnsetEpoll(int id) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(id, true, __func__)
        : HttpOffline::OfflineHttpUnsetEpoll_nid_no_patch(id);
}

int APS5_VABI sceHttpWaitRequest(HttpEpollHandle eh, HttpNBEvent* nbev, int maxevents, int timeout) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? UnsupportedEpoll_nid_no_patch(__func__)
        : HttpOffline::OfflineHttpWaitRequest_nid_no_patch(eh, nbev, maxevents, timeout);
}

int APS5_VABI sceHttpCreateRequestWithURL(int conn_id, int method, const char* url, uint64_t content_length) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::RequestIndex_nid_no_patch(conn_id, method, url, content_length, true)
        : HttpOffline::OfflineHttpCreateRequestWithURL_nid_no_patch(conn_id, method, url, content_length);
}

int APS5_VABI sceHttpReadData(int request_id, void* data, size_t size) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Read_nid_no_patch(request_id, data, size)
        : HttpOffline::OfflineHttpReadData_nid_no_patch(request_id, data, size);
}

int APS5_VABI sceHttpRedirectCacheFlush(int http_ctx_id) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(http_ctx_id, true, __func__)
        : HttpOffline::OfflineHttpRedirectCacheFlush_nid_no_patch(http_ctx_id);
}

int APS5_VABI sceHttpSetChunkedTransferEnabled(int id, int enable) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(id, enable != 0, __func__)
        : HttpOffline::OfflineHttpSetChunkedTransferEnabled_nid_no_patch(id, enable);
}

int APS5_VABI sceHttpSetInflateGZIPEnabled(int id, int enable) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Configure_nid_no_patch(id, HttpNetwork::Option::Gzip, static_cast<std::uint64_t>(enable))
        : HttpOffline::OfflineHttpSetInflateGZIPEnabled_nid_no_patch(id, enable);
}

int APS5_VABI sceHttpSetRequestStatusCallback(int id, HttpRequestStatusCallback callback, void* argument) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(id, callback != nullptr, __func__)
        : HttpOffline::OfflineHttpSetRequestStatusCallback_nid_no_patch(id, callback, argument);
}

int APS5_VABI sceHttpParseResponseHeader(const char* header, std::size_t headerLen, const char* fieldStr,
                                       const char** fieldValue, std::size_t* valueLen) {
    constexpr int invalidResponse = static_cast<int>(0x80432060);
    constexpr int invalidValue = static_cast<int>(0x804321FE);
    constexpr int notFound = static_cast<int>(0x80432025);
    if (!header) return invalidResponse;
    if (!fieldStr || !fieldValue || !valueLen) return invalidValue;

    std::size_t fieldLen = 0;
    while (fieldLen < 0xfff && fieldStr[fieldLen] != '\0') ++fieldLen;
    const std::string_view input(header, headerLen);
    const auto isSpace = [](char c) { return c == ' ' || (c >= '\t' && c <= '\r'); };
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
    std::size_t start = 0;
    bool found = false;
    while (start < headerLen) {
        if (!isSpace(input[start]) && fieldLen < headerLen - start && input[start + fieldLen] == ':') {
            std::size_t i = 0;
            while (i < fieldLen && lower(input[start + i]) == lower(fieldStr[i])) ++i;
            if (i == fieldLen) {
                start += fieldLen + 1;
                found = true;
                break;
            }
        }
        const auto newline = input.find('\n', start);
        if (newline == std::string_view::npos) break;
        start = newline + 1;
    }
    if (!found) return notFound;

    while (start < headerLen && isSpace(input[start])) {
        if (input[start++] == '\n') break;
    }
    std::size_t end = headerLen;
    std::size_t consumed = headerLen;
    std::size_t scan = start;
    while (scan < headerLen) {
        const auto newline = input.find('\n', scan);
        if (newline == std::string_view::npos) break;
        const auto next = newline + 1;
        if (next < headerLen && (input[next] == ' ' || input[next] == '\t')) {
            scan = next;
            continue;
        }
        end = newline > start && input[newline - 1] == '\r' ? newline - 1 : newline;
        consumed = next;
        break;
    }
    if (end == start) {
        *fieldValue = nullptr;
        *valueLen = 0;
        return 0;
    }
    if (consumed > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::out_of_range("sceHttpParseResponseHeader: consumed byte count exceeds int");
    }
    *fieldValue = header + start;
    *valueLen = end - start;
    return static_cast<int>(consumed);
}

int APS5_VABI sceHttpCreateRequest2(int conn_id, const char* method, const char* path, uint64_t content_length) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::RequestMethod_nid_no_patch(conn_id, method, path, content_length, false)
        : HttpOffline::OfflineHttpCreateRequest2_nid_no_patch(conn_id, method, path, content_length);
}

int APS5_VABI sceHttpsEnableOption(int id, uint32_t ssl_flags) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::SslOptions_nid_no_patch(id, ssl_flags, true)
        : HttpOffline::OfflineHttpsEnableOption_nid_no_patch(id, ssl_flags);
}

int APS5_VABI sceHttpsLoadCert(int http_ctx_id, int num, void* ca_list, void* cert, void* key) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(http_ctx_id, true, __func__)
        : HttpOffline::OfflineHttpsLoadCert_nid_no_patch(http_ctx_id, num, ca_list, cert, key);
}

int APS5_VABI sceHttpsUnloadCert(int http_ctx_id) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(http_ctx_id, true, __func__)
        : HttpOffline::OfflineHttpsUnloadCert_nid_no_patch(http_ctx_id);
}

int APS5_VABI sceHttpGetLastErrno(int request_id, int* errno_out) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Error_nid_no_patch(request_id, errno_out)
        : HttpOffline::OfflineHttpGetLastErrno_nid_no_patch(request_id, errno_out);
}

int APS5_VABI sceHttpsGetSslError(int id, int* err_num, uint32_t* detail) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::SslError_nid_no_patch(id, err_num, detail)
        : HttpOffline::OfflineHttpsGetSslError_nid_no_patch(id, err_num, detail);
}

int APS5_VABI sceHttpSetCookieRecvCallback(int id, HttpCookieRecvCallback cbfunc, void* user_arg) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(id, cbfunc != nullptr, __func__)
        : HttpOffline::OfflineHttpSetCookieRecvCallback_nid_no_patch(id, cbfunc, user_arg);
}

int APS5_VABI sceHttpsSetSslVersion(int id, int ssl_version) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(id, ssl_version != 0, __func__)
        : HttpOffline::OfflineHttpsSetSslVersion_nid_no_patch(id, ssl_version);
}

int APS5_VABI sceHttpSetRedirectCallback(int id, HttpRedirectCallback cbfunc, void* user_arg) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(id, cbfunc != nullptr, __func__)
        : HttpOffline::OfflineHttpSetRedirectCallback_nid_no_patch(id, cbfunc, user_arg);
}

int APS5_VABI sceHttpAbortWaitRequest(HttpEpollHandle eh) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? UnsupportedEpoll_nid_no_patch(__func__)
        : HttpOffline::OfflineHttpAbortWaitRequest_nid_no_patch(eh);
}

int APS5_VABI sceHttpsGetCaList(int http_ctx_id, HttpsCaList* ca_list) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(http_ctx_id, true, __func__)
        : HttpOffline::OfflineHttpsGetCaList_nid_no_patch(http_ctx_id, ca_list);
}

int APS5_VABI sceHttpsFreeCaList(int http_ctx_id, HttpsCaList* ca_list) {
    return HttpNetwork::Enabled_nid_no_patch()
        ? HttpNetwork::Unsupported_nid_no_patch(http_ctx_id, true, __func__)
        : HttpOffline::OfflineHttpsFreeCaList_nid_no_patch(http_ctx_id, ca_list);
}

}
