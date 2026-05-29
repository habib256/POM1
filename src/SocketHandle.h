// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// RAII wrapper for a BSD / Winsock socket descriptor.
// Move-only; closes the underlying FD on destruction so an exception between
// socket() and close() cannot leak it.

#ifndef SOCKET_HANDLE_H
#define SOCKET_HANDLE_H

#include "POM1Build.h"

#if !POM1_IS_WASM
  #ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
      #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
      #define NOMINMAX
    #endif
    #include <winsock2.h>
    using NativeSocket = SOCKET;
    inline constexpr NativeSocket kInvalidNativeSocket = INVALID_SOCKET;
    inline void closeNativeSocket(NativeSocket s) { ::closesocket(s); }
  #else
    #include <unistd.h>
    using NativeSocket = int;
    inline constexpr NativeSocket kInvalidNativeSocket = -1;
    inline void closeNativeSocket(NativeSocket s) { ::close(s); }
  #endif
#else
  // WASM builds never open real sockets; keep a compatible type so the same
  // class compiles and the cards can degrade to NO CARRIER gracefully.
  using NativeSocket = int;
  inline constexpr NativeSocket kInvalidNativeSocket = -1;
  inline void closeNativeSocket(NativeSocket) {}
#endif

class SocketHandle
{
public:
    SocketHandle() = default;
    explicit SocketHandle(NativeSocket fd) noexcept : fd_(fd) {}
    ~SocketHandle() { reset(); }

    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;

    SocketHandle(SocketHandle&& other) noexcept : fd_(other.fd_) { other.fd_ = kInvalidNativeSocket; }
    SocketHandle& operator=(SocketHandle&& other) noexcept
    {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = kInvalidNativeSocket;
        }
        return *this;
    }

    NativeSocket get() const noexcept { return fd_; }
    bool         valid() const noexcept { return fd_ != kInvalidNativeSocket; }
    explicit operator bool() const noexcept { return valid(); }

    // Implicit conversion so handles can be passed straight to socket APIs
    // (send, recv, select, setsockopt, fcntl, ioctlsocket, FD_SET, ...).
    operator NativeSocket() const noexcept { return fd_; }

    // Close current FD (if any), then adopt `fd`.
    void reset(NativeSocket fd = kInvalidNativeSocket) noexcept
    {
        if (fd_ != kInvalidNativeSocket) {
            closeNativeSocket(fd_);
        }
        fd_ = fd;
    }

    // Relinquish ownership without closing.
    NativeSocket release() noexcept
    {
        NativeSocket fd = fd_;
        fd_ = kInvalidNativeSocket;
        return fd;
    }

private:
    NativeSocket fd_ = kInvalidNativeSocket;
};

#endif // SOCKET_HANDLE_H
