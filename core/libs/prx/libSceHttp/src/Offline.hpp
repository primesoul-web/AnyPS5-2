#ifndef CORE_LIBS_PRX_LIBSCEHTTP_SRC_OFFLINE_HPP
#define CORE_LIBS_PRX_LIBSCEHTTP_SRC_OFFLINE_HPP

#include <cstddef>
#include <cstdint>
#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

using HttpRequestStatusCallback = void (APS5_VABI *)(int, int, void*);

struct HttpsCaList {
    void* certs;
    int certsNum;
};

namespace HttpOffline {

extern "C" {

int OfflineHttpAbortRequest_nid_no_patch(int request_id);
int OfflineHttpAddRequestHeader_nid_no_patch(int id, const char* name, const char* value, uint32_t mode);
int OfflineHttpCreateConnection_nid_no_patch(int tmpl_id, const char* server_name, const char* scheme, uint16_t port, int enable_keep_alive);
int OfflineHttpCreateConnectionWithURL_nid_no_patch(int tmpl_id, const char* url, int enable_keep_alive);
int OfflineHttpCreateEpoll_nid_no_patch(int http_ctx_id, HttpEpollHandle* eh);
int OfflineHttpCreateRequest_nid_no_patch(int conn_id, int method, const char* path, uint64_t content_length);
int OfflineHttpCreateRequestWithURL2_nid_no_patch(int conn_id, const char* method, const char* url, uint64_t content_length);
int OfflineHttpCreateTemplate_nid_no_patch(int http_ctx_id, const char* user_agent, int http_ver, int is_auto_proxy_conf);
int OfflineHttpDeleteConnection_nid_no_patch(int conn_id);
int OfflineHttpDeleteRequest_nid_no_patch(int req_id);
int OfflineHttpDeleteTemplate_nid_no_patch(int tmpl_id);
int OfflineHttpDestroyEpoll_nid_no_patch(int http_ctx_id, HttpEpollHandle eh);
int OfflineHttpGetAllResponseHeaders_nid_no_patch(int request_id, char** header, size_t* header_size);
int OfflineHttpGetResponseContentLength_nid_no_patch(int request_id, int* result, uint64_t* content_length);
int OfflineHttpGetStatusCode_nid_no_patch(int request_id, int* status_code);
int OfflineHttpInit_nid_no_patch(int memid, int ssl_ctx_id, uint64_t pool_size);
int OfflineHttpsDisableOption_nid_no_patch(int id, uint32_t ssl_flags);
int OfflineHttpSendRequest_nid_no_patch(int request_id, const void* post_data, size_t size);
int OfflineHttpSetAuthEnabled_nid_no_patch(int id, int enable);
int OfflineHttpSetAuthInfoCallback_nid_no_patch(int id, HttpAuthInfoCallback callback, void* userArg);
int OfflineHttpSetCookieEnabled_nid_no_patch(int id, int enable);
int OfflineHttpSetAutoRedirect_nid_no_patch(int id, int enable);
int OfflineHttpSetConnectTimeOut_nid_no_patch(int id, uint32_t usec);
int OfflineHttpSetEpoll_nid_no_patch(int id, HttpEpollHandle eh, void* user_arg);
int OfflineHttpSetNonblock_nid_no_patch(int id, int enable);
int OfflineHttpSetRecvTimeOut_nid_no_patch(int id, uint32_t usec);
int OfflineHttpSetRecvBlockSize_nid_no_patch(int id, uint32_t block_size);
int OfflineHttpSetResponseHeaderMaxSize_nid_no_patch(int id, uint64_t header_size);
int OfflineHttpSetRequestContentLength_nid_no_patch(int request_id, uint64_t content_length);
int OfflineHttpSetResolveRetry_nid_no_patch(int id, int32_t retry);
int OfflineHttpSetResolveTimeOut_nid_no_patch(int id, uint32_t usec);
int OfflineHttpSetSendTimeOut_nid_no_patch(int id, uint32_t usec);
int OfflineHttpsSetMinSslVersion_nid_no_patch(int id, uint32_t ssl_version);
int OfflineHttpsSetSslCallback_nid_no_patch(int id, HttpsCallback cbfunc, void* user_arg);
int OfflineHttpTerm_nid_no_patch(int http_ctx_id);
int OfflineHttpUnsetEpoll_nid_no_patch(int id);
int OfflineHttpWaitRequest_nid_no_patch(HttpEpollHandle eh, HttpNBEvent* nbev, int maxevents, int timeout);
int OfflineHttpCreateRequestWithURL_nid_no_patch(int conn_id, int method, const char* url, uint64_t content_length);
int OfflineHttpReadData_nid_no_patch(int request_id, void* data, size_t size);
int OfflineHttpRedirectCacheFlush_nid_no_patch(int http_ctx_id);
int OfflineHttpSetChunkedTransferEnabled_nid_no_patch(int id, int enable);
int OfflineHttpSetInflateGZIPEnabled_nid_no_patch(int id, int enable);
int OfflineHttpSetRequestStatusCallback_nid_no_patch(int id, HttpRequestStatusCallback callback, void* argument);
int OfflineHttpCreateRequest2_nid_no_patch(int conn_id, const char* method, const char* path, uint64_t content_length);
int OfflineHttpsEnableOption_nid_no_patch(int id, uint32_t ssl_flags);
int OfflineHttpsLoadCert_nid_no_patch(int http_ctx_id, int num, void* ca_list, void* cert, void* key);
int OfflineHttpsUnloadCert_nid_no_patch(int http_ctx_id);
int OfflineHttpGetLastErrno_nid_no_patch(int request_id, int* errno_out);
int OfflineHttpsGetSslError_nid_no_patch(int id, int* err_num, uint32_t* detail);
int OfflineHttpSetCookieRecvCallback_nid_no_patch(int id, HttpCookieRecvCallback cbfunc, void* user_arg);
int OfflineHttpsSetSslVersion_nid_no_patch(int id, int ssl_version);
int OfflineHttpSetRedirectCallback_nid_no_patch(int id, HttpRedirectCallback cbfunc, void* user_arg);
int OfflineHttpAbortWaitRequest_nid_no_patch(HttpEpollHandle eh);
int OfflineHttpsGetCaList_nid_no_patch(int http_ctx_id, HttpsCaList* ca_list);
int OfflineHttpsFreeCaList_nid_no_patch(int http_ctx_id, HttpsCaList* ca_list);

}

}

#endif
