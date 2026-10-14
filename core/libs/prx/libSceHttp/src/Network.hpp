#ifndef CORE_LIBS_PRX_LIBSCEHTTP_SRC_NETWORK_HPP
#define CORE_LIBS_PRX_LIBSCEHTTP_SRC_NETWORK_HPP

#include <cstddef>
#include <cstdint>

namespace HttpNetwork {

enum class Kind { Context, Template, Connection, Request };
enum class Option { Redirect, ResolveTimeout, ConnectTimeout, SendTimeout, ReceiveTimeout, HeaderLimit, ContentLength, Gzip, ReceiveBlockSize };

extern "C" {

bool Enabled_nid_no_patch();
int Init_nid_no_patch(std::uint64_t poolSize);
int Template_nid_no_patch(int context, const char* agent, int version, int proxy);
int ConnectionUrl_nid_no_patch(int parent, const char* url, int keepAlive);
int ConnectionHost_nid_no_patch(int parent, const char* host, const char* scheme, std::uint16_t port, int keepAlive);
int RequestMethod_nid_no_patch(int parent, const char* method, const char* path, std::uint64_t length, bool absolute);
int RequestIndex_nid_no_patch(int parent, int method, const char* path, std::uint64_t length, bool absolute);
int Delete_nid_no_patch(int id, Kind kind);
int Configure_nid_no_patch(int id, Option option, std::uint64_t value);
int SslOptions_nid_no_patch(int id, std::uint32_t flags, bool enable);
int Header_nid_no_patch(int id, const char* name, const char* value, std::uint32_t mode);
int Send_nid_no_patch(int id, const void* data, std::size_t size);
int Read_nid_no_patch(int id, void* data, std::size_t size);
int Status_nid_no_patch(int id, int* status);
int Headers_nid_no_patch(int id, char** headers, std::size_t* size);
int Length_nid_no_patch(int id, int* result, std::uint64_t* length);
int Abort_nid_no_patch(int id);
int Error_nid_no_patch(int id, int* error);
int SslError_nid_no_patch(int id, int* error, std::uint32_t* detail);
int Unsupported_nid_no_patch(int id, bool requested, const char* function);

}

}

#endif
