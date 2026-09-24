#pragma once

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <memory>
#include <string>
#include <string_view>

// A blocking WebSocket client over WinHTTP. One thread sends and receives, and any thread may Abort to unblock it.
class WebSocket {
  public:
    // Connects and completes the upgrade, throwing Win32Error with the failing WinHTTP call.
    WebSocket(std::wstring_view host, INTERNET_PORT port, std::wstring_view path);
    ~WebSocket();
    WebSocket(const WebSocket&)            = delete;
    WebSocket& operator=(const WebSocket&) = delete;

    void       Send(std::string_view text);

    // One whole message as text. Throws when the connection fails, is aborted, or the server closes it.
    std::string Receive();

    // Closes the socket so a Receive blocked on another thread returns with an error.
    void Abort();

  private:
    struct InternetCloser {
        void operator()(HINTERNET handle) const {
            WinHttpCloseHandle(handle);
        }
    };
    using UniqueInternet = std::unique_ptr<void, InternetCloser>;

    UniqueInternet session_;
    UniqueInternet connection_;
    // Atomic because Abort takes it from another thread while Receive may be using it.
    std::atomic<HINTERNET> socket_;
};
