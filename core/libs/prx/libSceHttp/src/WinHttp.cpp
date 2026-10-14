#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include "Transport.hpp"
#include "HttpErrors.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <string_view>

namespace HttpNetwork {
namespace {

std::wstring wide(const std::string& value) {
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count == 0 && !value.empty()) return {};
    std::wstring result(count, L'\0');
    if (count) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

std::string narrow(std::wstring_view value) {
    const auto count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0');
    if (count) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

int milliseconds(std::uint32_t usec) {
    return usec == 0 ? 0 : static_cast<int>((static_cast<std::uint64_t>(usec) + 999) / 1000);
}

class WinHttpTransport final : public ITransport {
public:
    ~WinHttpTransport() override {
        close();
        if (connection) WinHttpCloseHandle(connection);
        if (session) WinHttpCloseHandle(session);
    }

    int Send(const Settings& settings, const std::string& url, const std::string& method, const void* data, std::size_t size) override {
        receiveTimeout = settings.receiveTimeout;
        if (size > std::numeric_limits<DWORD>::max()) return ERROR_INVALID_VALUE;
        const auto encoded = wide(url);
        if (encoded.empty()) return ERROR_INVALID_URL;
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = static_cast<DWORD>(-1);
        parts.dwUrlPathLength = static_cast<DWORD>(-1);
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(encoded.c_str(), static_cast<DWORD>(encoded.size()), 0, &parts)) return ERROR_INVALID_URL;
        const auto agent = wide(settings.agent);
        const auto verb = wide(method);
        if ((!settings.agent.empty() && agent.empty()) || verb.empty()) return ERROR_INVALID_VALUE;
        std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring path;
        if (parts.dwUrlPathLength) path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (path.empty()) path = L"/";
        if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        if (const auto fragment = path.find(L'#'); fragment != std::wstring::npos) path.resize(fragment);
        {
            std::lock_guard lock(apiMutex);
            if (aborted) return ERROR_HTTP_ABORTED;
            session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
            if (!session) return fail(GetLastError());
            if (!WinHttpSetTimeouts(session, milliseconds(settings.resolveTimeout), milliseconds(settings.connectTimeout), milliseconds(settings.sendTimeout), milliseconds(settings.receiveTimeout))) return fail(GetLastError());
            connection = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
            if (!connection) return fail(GetLastError());
            request = WinHttpOpenRequest(connection, verb.c_str(), path.c_str(), settings.version == 1 ? L"HTTP/1.0" : L"HTTP/1.1", WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
            if (!request) return fail(GetLastError());
            DWORD_PTR context = reinterpret_cast<DWORD_PTR>(this);
            if (!WinHttpSetOption(request, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) return fail(GetLastError());
            if (WinHttpSetStatusCallback(request, Callback, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0) == WINHTTP_INVALID_STATUS_CALLBACK) return fail(GetLastError());
            callbackInstalled = true;
            DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
            if (!settings.redirect) disabled |= WINHTTP_DISABLE_REDIRECTS;
            if (!settings.keepAlive) disabled |= WINHTTP_DISABLE_KEEP_ALIVE;
            if (!WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled))) return fail(GetLastError());
            DWORD redirectPolicy = settings.redirect ? WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP : WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy))) return fail(GetLastError());
            DWORD headerLimit = static_cast<DWORD>(settings.headerLimit);
            if (!WinHttpSetOption(request, WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE, &headerLimit, sizeof(headerLimit))) return fail(GetLastError());
            DWORD security = 0;
            if (!(settings.sslFlags & 1u)) security = SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
            else {
                if (!(settings.sslFlags & 4u)) security |= SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
                if (!(settings.sslFlags & 0x18u)) security |= SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
                if (!(settings.sslFlags & 0x20u)) security |= SECURITY_FLAG_IGNORE_UNKNOWN_CA;
            }
            if (security && !WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &security, sizeof(security))) return fail(GetLastError());
            if (settings.gzip) {
                DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_GZIP;
                if (!WinHttpSetOption(request, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression))) return fail(GetLastError());
            }
            for (const auto& [name, value] : settings.headers) {
                const auto line = wide(name + ": " + value);
                if (line.empty()) return ERROR_INVALID_VALUE;
                if (!WinHttpAddRequestHeaders(request, line.c_str(), static_cast<DWORD>(line.size()), WINHTTP_ADDREQ_FLAG_ADD)) return fail(GetLastError());
            }
        }
        auto result = perform(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, [&] {
            return WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, const_cast<void*>(data), static_cast<DWORD>(size), static_cast<DWORD>(size), reinterpret_cast<DWORD_PTR>(this));
        });
        if (result != 0) return result;
        result = perform(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, [&] { return WinHttpReceiveResponse(request, nullptr); }, receiveTimeout);
        if (result != 0) return result;
        std::lock_guard lock(apiMutex);
        if (aborted || !request) return ERROR_HTTP_ABORTED;
        DWORD code = 0;
        DWORD bytes = sizeof(code);
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &bytes, WINHTTP_NO_HEADER_INDEX)) return fail(GetLastError());
        status = static_cast<int>(code);
        bytes = 0;
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &bytes, WINHTTP_NO_HEADER_INDEX) || GetLastError() != ERROR_INSUFFICIENT_BUFFER) return fail(GetLastError());
        if (bytes > (settings.headerLimit + 1) * sizeof(wchar_t)) return ERROR_HTTP_HEADER_TOO_LARGE;
        std::wstring raw((bytes + sizeof(wchar_t) - 1) / sizeof(wchar_t), L'\0');
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &bytes, WINHTTP_NO_HEADER_INDEX)) return fail(GetLastError());
        raw.resize(bytes / sizeof(wchar_t));
        while (!raw.empty() && raw.back() == L'\0') raw.pop_back();
        headers = narrow(raw);
        if (headers.size() > settings.headerLimit) return ERROR_HTTP_HEADER_TOO_LARGE;
        wchar_t lengthBuffer[32]{};
        bytes = sizeof(lengthBuffer);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lengthBuffer, &bytes, WINHTTP_NO_HEADER_INDEX)) {
            const auto value = narrow(lengthBuffer);
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), length);
            hasLength = parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
        } else if (GetLastError() != ERROR_WINHTTP_HEADER_NOT_FOUND) return fail(GetLastError());
        return 0;
    }

    int Read(void* data, std::size_t size) override {
        const auto result = perform(WINHTTP_CALLBACK_STATUS_READ_COMPLETE, [&] { return WinHttpReadData(request, data, static_cast<DWORD>(size), nullptr); }, receiveTimeout);
        return result == 0 ? static_cast<int>(readBytes) : result;
    }

    void Abort() override {
        aborted = true;
        close();
    }

    int Error() const override { return static_cast<int>(lastError.load()); }
    std::uint32_t SslError() const override { return sslError.load(); }

private:
    HINTERNET session = nullptr;
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;
    std::mutex apiMutex;
    std::mutex completionMutex;
    std::condition_variable completionChanged;
    std::atomic<bool> aborted = false;
    std::atomic<DWORD> lastError = 0;
    std::atomic<DWORD> sslError = 0;
    bool callbackInstalled = false;
    bool closed = false;
    bool completed = false;
    DWORD expected = 0;
    DWORD operationError = 0;
    DWORD readBytes = 0;
    std::uint32_t receiveTimeout = 0;
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();

    int fail(DWORD error) {
        lastError = error;
        if (aborted || error == ERROR_WINHTTP_OPERATION_CANCELLED) return ERROR_HTTP_ABORTED;
        if (error == ERROR_WINHTTP_TIMEOUT) return ERROR_HTTP_TIMEOUT;
        if (error == ERROR_WINHTTP_SECURE_FAILURE) return ERROR_HTTP_SSL;
        if (error == ERROR_WINHTTP_HEADER_SIZE_OVERFLOW) return ERROR_HTTP_HEADER_TOO_LARGE;
        if (error == ERROR_NOT_ENOUGH_MEMORY || error == ERROR_OUTOFMEMORY) return ERROR_OUT_OF_MEMORY;
        return ERROR_NETWORK;
    }

    template<typename TCall>
    int perform(DWORD notification, TCall call, std::uint32_t timeout = 0) {
        DWORD immediateError = 0;
        {
            std::lock_guard lock(apiMutex);
            if (aborted || !request) return ERROR_HTTP_ABORTED;
            {
                std::lock_guard completionLock(completionMutex);
                completed = false;
                expected = notification;
                operationError = 0;
                deadline = timeout ? std::chrono::steady_clock::now() + std::chrono::microseconds(timeout) : std::chrono::steady_clock::time_point::max();
            }
            if (!call()) immediateError = GetLastError();
        }
        if (immediateError) {
            close();
            return fail(immediateError);
        }
        std::unique_lock lock(completionMutex);
        while (!completed && !closed) {
            if (deadline == std::chrono::steady_clock::time_point::max()) completionChanged.wait(lock);
            else if (completionChanged.wait_until(lock, deadline) == std::cv_status::timeout && !completed && !closed) {
                lock.unlock();
                close();
                return fail(ERROR_WINHTTP_TIMEOUT);
            }
        }
        const auto error = operationError;
        lock.unlock();
        if (aborted) {
            close();
            return ERROR_HTTP_ABORTED;
        }
        if (error) {
            close();
            return fail(error);
        }
        return 0;
    }

    void close() {
        bool waitForClose = false;
        {
            std::lock_guard lock(apiMutex);
            waitForClose = callbackInstalled;
            if (request) {
                const auto handle = request;
                if (!WinHttpCloseHandle(handle)) std::terminate();
                request = nullptr;
            }
        }
        if (waitForClose) {
            std::unique_lock lock(completionMutex);
            completionChanged.wait(lock, [&] { return closed; });
        }
    }

    static void CALLBACK Callback(HINTERNET, DWORD_PTR context, DWORD status, void* information, DWORD size) {
        if (!context) return;
        auto& self = *reinterpret_cast<WinHttpTransport*>(context);
        std::lock_guard lock(self.completionMutex);
        if (status == WINHTTP_CALLBACK_STATUS_REQUEST_SENT && self.expected == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE && self.receiveTimeout) {
            self.deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(self.receiveTimeout);
            self.completionChanged.notify_all();
        } else if (status == WINHTTP_CALLBACK_STATUS_SECURE_FAILURE) {
            if (information && size == sizeof(DWORD)) self.sslError = *static_cast<DWORD*>(information);
        } else if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
            self.closed = true;
            self.operationError = ERROR_WINHTTP_OPERATION_CANCELLED;
            self.completionChanged.notify_all();
        } else if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
            self.operationError = static_cast<WINHTTP_ASYNC_RESULT*>(information)->dwError;
            self.completed = true;
            self.completionChanged.notify_all();
        } else if (status == self.expected) {
            self.readBytes = status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE ? size : 0;
            self.completed = true;
            self.completionChanged.notify_all();
        }
    }
};

}

std::shared_ptr<ITransport> CreateTransport_nid_no_patch() {
    return std::make_shared<WinHttpTransport>();
}

}
#endif
