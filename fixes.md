# fixes.md

Every change made to the project in this refactor, in three files:

- `src/net/connection_win32.cpp`: rewritten (fixes and SRP split). Changes 1 to 24.
- `include/net/connection.hpp`: change 25.
- `CMakeLists.txt`: change 26.

---

## Overview: the new structure (SRP)

The old file put everything into a single function, `Connection::OpenSocket`. It is now split
into classes that all live in the same namespace, `StreamBits::Net`. Each class has one job:

| Class               | Single responsibility                                                    |
|---------------------|--------------------------------------------------------------------------|
| `WinsockRuntime`    | Winsock initialization (`WSAStartup` / `WSACleanup`)                     |
| `CompletionHandler` | Interface for anything that receives IOCP completions                    |
| `CompletionPort`    | The IOCP handle and its worker threads; dispatches completions            |
| `IoContext`         | State of one overlapped operation (OVERLAPPED + buffer)                  |
| `Session`           | One accepted client: socket, in-flight I/O, lifetime (reference count)   |
| `SessionRegistry`   | The set of live sessions, so shutdown can close and wait for them        |
| `Listener`          | The listening socket: open, configure, bind, listen, accept              |
| `TcpServer`         | Composes the pieces above, runs the accept loop and the shutdown order   |
| `ConsoleStopSignal` | Routes Ctrl+C / Ctrl+Break to `TcpServer::Stop()`                        |

`Connection::OpenSocket` keeps the same signature and is now only a thin entry point that
composes these classes.

```text
OpenSocket
 ├─ WinsockRuntime          (lives longest: WSACleanup runs last)
 ├─ TcpServer
 │   ├─ CompletionPort ──── worker threads ──► CompletionHandler::OnCompletion
 │   ├─ SessionRegistry ─── Session* set                         ▲
 │   └─ Listener ── accept ──► new Session ──────────────────────┘
 └─ ConsoleStopSignal ─── Ctrl+C ──► TcpServer::Stop()
```

This is a single file, which matches the one-file-per-platform layout that `CMakeLists.txt`
already uses (`connection_win32.cpp` / `connection_linux.cpp`). The comment block at the top of
the file lists the classes in this same order.

---

## 1. Removed the two blank lines at the top of the file

Old code

```cpp


#include "net/connection.hpp"
```

New code

```cpp
#include "net/connection.hpp"
```

I did that because it is purely cosmetic: the file now starts with its first include.

---

## 2. Added `WIN32_LEAN_AND_MEAN` and `NOMINMAX` before the Windows headers

Old code

```cpp
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
```

New code

```cpp
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
```

I did that because:
- `NOMINMAX` stops `<windows.h>` from defining the `min`/`max` macros, which break
  `std::max` (used in change 21).
- `WIN32_LEAN_AND_MEAN` leaves out rarely used parts of `<windows.h>`, including the old
  `winsock.h`, which conflicts with `winsock2.h`. It also compiles faster.
- The `#ifndef` guards avoid redefinition warnings if the build ever defines them globally.

---

## 3. Standard headers updated

Old code

```cpp
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>
```

New code

```cpp
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <format>
#include <iostream>
#include <mutex>
#include <span>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <vector>
```

I did that because the new code needs them: `std::max` (algorithm), `std::array` for the
buffer, `std::atomic` for reference counts and flags, `std::condition_variable` + `std::mutex`
for the registry and the log, `std::expected` + `std::error_code` (system_error) for error
handling (the same style as `server_api.hpp`), `std::format` for log messages,
`std::span`/`std::string_view` to pass received bytes without copying or null-terminating them,
and `std::unordered_set` for the registry.

---

## 4. Removed `#pragma comment(lib, "Ws2_32.lib")`

Old code

```cpp
#pragma comment(lib, "Ws2_32.lib")
```

New code

```cpp
// (removed — see change 26: CMakeLists.txt now links ws2_32)
```

I did that because `#pragma comment(lib)` only works with compilers that support it (MSVC,
clang-cl). MinGW's GCC ignores it and the link fails. The build system is the right place to
declare link dependencies.

---

## 5. Added the overview comment and moved everything into `namespace StreamBits::Net`

Old code

```cpp
namespace {

  enum Operation { ... };
  struct IoContext { ... };
  struct HandleContext { ... };
  void WorkerThread(HANDLE iocp) { ... }
}

void StreamBits::Net::Connection::OpenSocket(uint16_t port_number = 8080) { ... }
```

New code

```cpp
// Win32 / Winsock implementation of the network layer. Each class has one job:
//
//   WinsockRuntime     Winsock initialization (WSAStartup / WSACleanup)
//   CompletionPort     the IOCP and its worker threads; dispatches completions to handlers
//   Session            one accepted client: socket, buffers, in-flight I/O, lifetime
//   SessionRegistry    the set of live sessions, so shutdown can close and wait for them
//   Listener           the listening socket: open, configure, bind, listen, accept
//   TcpServer          composes the pieces above and runs the accept loop
//   ConsoleStopSignal  routes Ctrl+C / Ctrl+Break to TcpServer::Stop()

namespace StreamBits::Net {

  namespace {
    // Log, Describe, LastSocketError, LastSystemError (change 6)
  }

  class WinsockRuntime { ... };
  class CompletionHandler { ... };
  class CompletionPort { ... };
  enum class Operation { ... };
  struct IoContext { ... };
  class Session { ... };
  class SessionRegistry { ... };
  class Listener { ... };
  class TcpServer { ... };
  class ConsoleStopSignal { ... };

  void Connection::OpenSocket(std::uint16_t port_number) { ... }

}
```

I did that because you asked for the responsibilities to be split across the same namespace.
All components now live in `StreamBits::Net`, next to the public `Net::Connection` entry point,
so the layer reads as one unit. Only the small private helpers stay in an anonymous namespace,
since nothing outside this file needs them. The comment at the top is a map of the file.

---

## 6. New helpers: thread-safe `Log`, `Describe`, `LastSocketError`, `LastSystemError`

Old code

```cpp
std::cout << "Processed Bytes: " << bytes_transferred << " | Key: " << completion_key << "\n";
std::cout << "Not succeeded!!\n";
std::cout << "Received " << bytes_transferred << " bytes: " << io_context->buffer << "\n";
// errors were never read: WSAGetLastError()/GetLastError() were not called anywhere
```

New code

```cpp
namespace {

  std::mutex log_mutex;

  // One lock per line, so lines written by different threads never interleave.
  void Log(std::string_view message) {
    std::scoped_lock lock(log_mutex);
    std::cout << message << '\n';
  }

  std::string Describe(const std::error_code& error) {
    return std::format("{} (code {})", error.message(), error.value());
  }

  std::error_code LastSocketError() {
    return {WSAGetLastError(), std::system_category()};
  }

  std::error_code LastSystemError() {
    return {static_cast<int>(GetLastError()), std::system_category()};
  }

}
```

I did that because:
- Several worker threads wrote to `std::cout` with chained `<<`, so pieces of different lines
  could mix together. Now each message is formatted first and written under one lock.
- `std::error_code` with `std::system_category()` turns Win32/Winsock codes into readable
  text (`error.message()` calls `FormatMessage`), so failures can be reported instead of
  ignored. `Describe` always shows the numeric code too, because the text is localized.
- Winsock errors come from `WSAGetLastError()` and Win32 errors (IOCP) from `GetLastError()`,
  so there is one helper for each.

---

## 7. `WSAStartup` / `WSACleanup` moved into `WinsockRuntime` (RAII + version check)

Old code

```cpp
WSADATA wsa_data;
if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
  return;
}
// ...
WSACleanup();      // never reached (infinite accept loop above it)
CloseHandle(iocp);
```

New code

```cpp
class WinsockRuntime {
public:
  WinsockRuntime() = default;
  WinsockRuntime(const WinsockRuntime&) = delete;
  WinsockRuntime& operator=(const WinsockRuntime&) = delete;

  ~WinsockRuntime() {
    if (started_) {
      WSACleanup();
    }
  }

  std::expected<void, std::error_code> Start() {
    WSADATA wsa_data;
    // WSAStartup returns its error directly; WSAGetLastError is not usable before it succeeds.
    if (const int result = WSAStartup(MAKEWORD(2, 2), &wsa_data); result != 0) {
      return std::unexpected(std::error_code(result, std::system_category()));
    }

    if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion) != 2) {
      WSACleanup();
      return std::unexpected(std::error_code(WSAVERNOTSUPPORTED, std::system_category()));
    }

    started_ = true;
    return {};
  }

private:
  bool started_ = false;
};
```

I did that because:
- `WSACleanup` was never reached, and it was called **before** `CloseHandle(iocp)`, which is
  the wrong order. With RAII, `WinsockRuntime` is the first object created in `OpenSocket`, so
  it is destroyed last, after every socket and the IOCP are already closed.
- A failed `WSAStartup` used to return silently. Now its error code is returned and logged.
- The Winsock docs say to check that the version you got back is the one you asked for (2.2).
- It is non-copyable because a copy would call `WSACleanup` twice.

---

## 8. Removed the dead `WorkerThread` function

Old code

```cpp
void WorkerThread(HANDLE iocp){
  DWORD bytes_transferred;
  ULONG_PTR completion_key;
  LPOVERLAPPED overlapped;

  while (true) {
    BOOL success = GetQueuedCompletionStatus(
      iocp, &bytes_transferred, &completion_key, &overlapped, INFINITE
    );

    if (completion_key == 0 && overlapped == nullptr) {
      break;
    }

    if (success) {
      std::cout << "Processed Bytes: " << bytes_transferred << " | Key: " << completion_key << "\n";
    } else {
      std::cout << "Not succeeded!!\n";
    }
  }
}
```

New code

```cpp
// (removed — its stop-packet idea now lives in CompletionPort::RunWorker, change 10)
```

I did that because nothing called it. The workers ran a separate lambda that did **not** have
the stop check, so there were two worker loops, and the one in use was the one without a way
to stop.

---

## 9. New `CompletionHandler` interface

Old code

```cpp
// The worker cast the completion key straight to HandleContext*:
(PULONG_PTR)&handle_context
```

New code

```cpp
// Receives the completions of the handle it was associated with. Its address is the
// completion key, so the port never needs to know what the handler is.
class CompletionHandler {
public:
  virtual void OnCompletion(OVERLAPPED* overlapped, DWORD bytes_transferred, DWORD error) = 0;

protected:
  ~CompletionHandler() = default;
};
```

I did that because the I/O dispatch loop (`CompletionPort`) should not know what a client
connection is. It only knows "a handler is registered for this key". A future `AcceptEx`-based
listener, or any other IOCP user, can implement the same interface. The protected non-virtual
destructor stops anyone from deleting a handler through the interface: a `Session` controls
its own lifetime (change 15).

---

## 10. The IOCP and its workers moved into `CompletionPort`

Old code

```cpp
HANDLE iocp = CreateIoCompletionPort(
  INVALID_HANDLE_VALUE,
  nullptr,
  0, 0
);

uint_fast16_t thread_count = sys_info.dwNumberOfProcessors * 2;

std::vector<std::thread> workers;
workers.reserve(thread_count);

for (uint16_t i = 0; i < thread_count; i++) {
  workers.emplace_back([iocp]() {
    DWORD bytes_transferred;
    HandleContext* handle_context;
    IoContext* io_context;

    while (true) {

      BOOL success = GetQueuedCompletionStatus(
        iocp,
        &bytes_transferred,
        (PULONG_PTR)&handle_context,
        (LPOVERLAPPED*)&io_context,
        INFINITE
      );

      if (success && bytes_transferred == 0 && io_context != nullptr) {
        closesocket(handle_context->socket);
        delete handle_context;
        delete io_context;
        continue;
      }

      if (success && io_context != nullptr) {
        // ... handle read, re-post WSARecv ...
      }

    }

  });
}
// ...
CreateIoCompletionPort((HANDLE)client_socket, iocp, (ULONG_PTR)context, 0);
// ...
for (auto& worker : workers)
  worker.join();
// ...
CloseHandle(iocp);
```

New code

```cpp
class CompletionPort {
public:
  CompletionPort() = default;
  CompletionPort(const CompletionPort&) = delete;
  CompletionPort& operator=(const CompletionPort&) = delete;

  ~CompletionPort() {
    Stop();
    if (port_ != nullptr) {
      CloseHandle(port_);
    }
  }

  std::expected<void, std::error_code> Open() {
    // Concurrency 0: the kernel lets as many workers run at once as there are processors.
    port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (port_ == nullptr) {
      return std::unexpected(LastSystemError());
    }
    return {};
  }

  std::expected<void, std::error_code> Associate(SOCKET socket, CompletionHandler& handler) {
    const HANDLE result = CreateIoCompletionPort(
      reinterpret_cast<HANDLE>(socket),
      port_,
      reinterpret_cast<ULONG_PTR>(&handler), 0
    );
    if (result == nullptr) {
      return std::unexpected(LastSystemError());
    }
    return {};
  }

  void StartWorkers(std::size_t thread_count) {
    workers_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; i++) {
      workers_.emplace_back([this] { RunWorker(); });
    }
  }

  // Posts one stop packet per worker and waits for all of them to exit. Idempotent.
  void Stop() {
    for (std::size_t i = 0; i < workers_.size(); i++) {
      PostQueuedCompletionStatus(port_, 0, kStopKey, nullptr);
    }
    for (auto& worker : workers_) {
      worker.join();
    }
    workers_.clear();
  }

private:
  static constexpr ULONG_PTR kStopKey = 0;

  void RunWorker() {
    while (true) {
      DWORD bytes_transferred = 0;
      ULONG_PTR completion_key = 0;
      OVERLAPPED* overlapped = nullptr;

      const BOOL success = GetQueuedCompletionStatus(
        port_, &bytes_transferred, &completion_key, &overlapped, INFINITE
      );

      // No OVERLAPPED means no I/O completed: either our stop packet, or the port itself
      // failed. Both end the worker.
      if (overlapped == nullptr) {
        if (!success) {
          Log(std::format("GetQueuedCompletionStatus failed: {}", Describe(LastSystemError())));
        }
        return;
      }

      // With an OVERLAPPED, a FALSE result means that I/O failed (reset, abort, cancel...).
      // It still has to reach its handler, which owns the resources tied to it.
      const DWORD error = success ? ERROR_SUCCESS : GetLastError();
      reinterpret_cast<CompletionHandler*>(completion_key)->OnCompletion(overlapped, bytes_transferred, error);
    }
  }

  HANDLE port_ = nullptr;
  std::vector<std::thread> workers_;
};
```

I did that because:
- **Workers could not be stopped.** The lambda had no exit, so `join()` could never return.
  `Stop()` now posts one stop packet (key 0, no OVERLAPPED) per worker, and `RunWorker`
  returns when it gets one.
- **Failed completions were dropped.** When `GetQueuedCompletionStatus` returned `FALSE` with
  an OVERLAPPED (connection reset, I/O cancelled...), the old loop did nothing and leaked the
  socket and both contexts. Now the error code is always passed to the handler, which cleans up.
- **The `CreateIoCompletionPort` results were not checked.** `Open()` and `Associate()` return
  the error now.
- **The old casts were unsafe.** `(PULONG_PTR)&handle_context` and `(LPOVERLAPPED*)&io_context`
  made the kernel write into the addresses of pointers of other types. Now the worker receives
  the real types (`ULONG_PTR`, `OVERLAPPED*`) and converts them explicitly.
- **Locals are initialized.** If the call fails, nothing stale from a previous loop is read.
- **RAII.** The destructor stops the workers and only then closes the handle, so every exit
  path cleans up in the right order.

---

## 11. `Operation` became an `enum class`, and `IoContext` was modernized

Old code

```cpp
enum Operation {
  Read,
  Write
};

struct IoContext {
  WSAOVERLAPPED overlapped;
  WSABUF wsa_buf;
  char buffer [1024];
  Operation operation;
};

// and, repeated in two places:
ZeroMemory(&io_context->overlapped, sizeof(WSAOVERLAPPED));
io_context->operation = Read;
io_context->wsa_buf.buf = io_context->buffer;
io_context->wsa_buf.len = sizeof(io_context->buffer);
```

New code

```cpp
enum class Operation {
  Read,
  Write   // reserved: nothing posts writes yet
};

// State of one overlapped operation. It must stay alive until the operation completes,
// which is why it lives inside the Session and not on a stack.
struct IoContext {
  WSAOVERLAPPED overlapped{};
  WSABUF wsa_buf{};
  std::array<char, 1024> buffer{};
  Operation operation = Operation::Read;

  void PrepareRead() {
    overlapped = {};
    operation = Operation::Read;
    wsa_buf.buf = buffer.data();
    wsa_buf.len = static_cast<ULONG>(buffer.size());
  }
};
```

I did that because:
- A plain `enum` leaks `Read` and `Write` into the surrounding namespace, where they can
  clash with other names. `enum class` keeps them scoped.
- The "reset before a read" code was duplicated in two places. It now lives in one place,
  `PrepareRead()`.
- Every member is value-initialized, so a new `IoContext` never holds garbage.
- `std::array` knows its own size, so there is no `sizeof` on a raw array.
- I kept `Write` because you added it on purpose for the coming echo/response work. Nothing
  posts it yet, and the comment says so.

---

## 12. Fixed the out-of-bounds write (`buffer[bytes_transferred] = '\0'`)

Old code

```cpp
if (io_context->operation == Read) {
  io_context->buffer[bytes_transferred] = '\0';
  std::cout << "Received " << bytes_transferred << " bytes: " << io_context->buffer << "\n";
}
```

New code

```cpp
// in Session::OnCompletion:
OnDataReceived({context->buffer.data(), bytes_transferred});

// The one place where received bytes leave the I/O layer: the HTTP parser plugs in here.
void Session::OnDataReceived(std::span<const char> data) {
  Log(std::format("Received {} bytes: {}", data.size(), std::string_view(data.data(), data.size())));
}
```

I did that because when a read filled all 1024 bytes, `buffer[1024] = '\0'` wrote one byte
past the array, into `operation`, the next field. Instead of shrinking the buffer by one, the
data is now passed as **pointer + length** (`std::span`) and never null-terminated. That also
handles bytes equal to `'\0'` inside the data correctly. It is the zero-copy form the Phase 2
parser will need, and `OnDataReceived` is the single point where it will plug in. Tested by
sending exactly 1024 bytes: all of them arrive.

---

## 13. `HandleContext` replaced by `Session`

Old code

```cpp
struct HandleContext {
  SOCKET socket;
};

// accept loop:
auto* context = new HandleContext;
context->socket = client_socket;
// ...
IoContext* io_context = new IoContext;
```

New code

```cpp
class SessionRegistry;

// Lifetime rule: the reference count is the creator's reference plus one per operation in
// flight. The session deletes itself when it reaches zero. Close() never frees anything; it
// cancels the pending I/O, whose completions then drop the count to zero.
class Session final : public CompletionHandler {
public:
  Session(SOCKET socket, SessionRegistry& registry);
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // Associates the socket with the port, posts the first read, and drops the creator's
  // reference. The caller must not touch the session afterwards.
  void Start(CompletionPort& completion_port);

  // Thread-safe and idempotent.
  void Close();

  void OnCompletion(OVERLAPPED* overlapped, DWORD bytes_transferred, DWORD error) override;

private:
  // Private: only Release() may destroy a session.
  ~Session();

  void PostRead();
  void OnDataReceived(std::span<const char> data);

  void AddRef();
  void Release();

  const SOCKET socket_;
  SessionRegistry& registry_;
  IoContext read_context_;
  std::atomic<std::uint32_t> references_{1};
  std::atomic<bool> closed_{false};
};
```

I did that because one client connection was split across two separate heap objects
(`HandleContext` + `IoContext`) with no clear owner, and the worker loop handled its logic
inline. `Session` now owns everything for one client: the socket, the read context (embedded,
so one allocation instead of two), its lifetime and its behavior. The constructor registers it
in the `SessionRegistry` (change 18):

```cpp
Session::Session(SOCKET socket, SessionRegistry& registry)
  : socket_(socket), registry_(registry) {
  registry_.Add(*this);
}
```

---

## 14. `CONTAINING_RECORD` instead of relying on member order

Old code

```cpp
(LPOVERLAPPED*)&io_context   // works only because `overlapped` is the first member
```

New code

```cpp
const IoContext* context = CONTAINING_RECORD(overlapped, IoContext, overlapped);
```

I did that because casting an `OVERLAPPED*` straight to `IoContext*` silently breaks if a
field is ever added before `overlapped`. `CONTAINING_RECORD` is the documented Windows way to
get from a member back to its enclosing struct, whatever its position.

---

## 15. Session lifetime: reference counting, `CancelIoEx`, and `closesocket` only in the destructor

Old code

```cpp
if (success && bytes_transferred == 0 && io_context != nullptr) {
  closesocket(handle_context->socket);
  delete handle_context;
  delete io_context;
  continue;
}
```

New code

```cpp
Session::~Session() {
  // Closed only here: once the count is zero no thread can still be using the handle, so
  // a new connection cannot get the same value while an old operation still refers to it.
  closesocket(socket_);
}

void Session::Close() {
  if (closed_.exchange(true)) {
    return;
  }
  // Every pending operation completes with ERROR_OPERATION_ABORTED and releases its reference.
  CancelIoEx(reinterpret_cast<HANDLE>(socket_), nullptr);
}

void Session::AddRef() {
  references_.fetch_add(1, std::memory_order_relaxed);
}

void Session::Release() {
  if (references_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    registry_.Remove(*this);
    delete this;
  }
}
```

I did that because the old code freed the connection from inside **one** completion, assuming
nothing else was in flight. As soon as a read and a write can be pending together, a second
completion would use freed memory. The rule now:

- The count starts at 1 (the creator's reference). `PostRead` adds 1 per operation, and each
  completion removes 1.
- `Close()` frees nothing. It marks the session closed and cancels pending I/O. The cancelled
  operations complete with `ERROR_OPERATION_ABORTED`, release their references, and whichever
  thread drops the count to 0 deletes the session.
- `closesocket` moved to the destructor. If it ran in `Close()`, Windows could hand the same
  socket value to a new connection while a worker was still about to call `WSARecv` with the
  old value, which would read from the **wrong client**.
- `acq_rel` on the final decrement makes every other thread's writes visible before the
  `delete`. The increment can be `relaxed` because the caller already holds a reference.
- The destructor is private, so the session can only be destroyed through `Release()`.

---

## 16. Completion handling moved into `Session::OnCompletion`, now covering every outcome

Old code

```cpp
if (success && bytes_transferred == 0 && io_context != nullptr) {
  // close + delete
  continue;
}

if (success && io_context != nullptr) {
  if (io_context->operation == Read) {
    io_context->buffer[bytes_transferred] = '\0';
    std::cout << "Received " << bytes_transferred << " bytes: " << io_context->buffer << "\n";
  }
  // reset + WSARecv
}
// !success with io_context != nullptr: nothing happens -> leak
```

New code

```cpp
void Session::OnCompletion(OVERLAPPED* overlapped, DWORD bytes_transferred, DWORD error) {
  const IoContext* context = CONTAINING_RECORD(overlapped, IoContext, overlapped);

  if (error != ERROR_SUCCESS) {
    // Reset or aborted by the peer, or cancelled by Close(): the connection is over.
    Close();
  } else if (context->operation == Operation::Read) {
    if (bytes_transferred == 0) {
      // The peer closed its side (FIN).
      Close();
    } else {
      OnDataReceived({context->buffer.data(), bytes_transferred});
      // Posted before releasing this operation's reference, so the count cannot reach zero
      // in between.
      PostRead();
    }
  }

  Release();
}
```

I did that because the old loop handled only two of the three outcomes. A **failed** completion
(client reset, `ERROR_NETNAME_DELETED`, or a cancel) fell through and leaked the socket and both
contexts. Now:
- failed → close
- success with 0 bytes (graceful FIN) → close
- success with data → hand it up and post the next read

Every path ends with exactly one `Release()`, which balances the `AddRef()` done when the
operation was posted. Tested by sending a TCP RST (`SO_LINGER` with timeout 0): the session is
cleaned up.

---

## 17. `WSARecv` errors are now checked (`Session::PostRead`)

Old code

```cpp
DWORD flags = 0;
WSARecv(
  handle_context->socket,
  &(io_context->wsa_buf), 1,
  nullptr, &flags,
  &(io_context->overlapped), nullptr
);
// (the same unchecked call was duplicated in the accept loop)
```

New code

```cpp
void Session::PostRead() {
  if (closed_) {
    return;
  }

  read_context_.PrepareRead();
  AddRef();

  DWORD flags = 0;
  const int result = WSARecv(
    socket_,
    &read_context_.wsa_buf, 1,
    nullptr, &flags,
    &read_context_.overlapped, nullptr
  );

  if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
    // The read never started, so no completion will come to release its reference.
    Close();
    Release();
    return;
  }

  // If Close() ran between the check above and WSARecv, its CancelIoEx missed this read.
  if (closed_) {
    CancelIoEx(reinterpret_cast<HANDLE>(socket_), &read_context_.overlapped);
  }
}
```

I did that because:
- When `WSARecv` fails with anything other than `WSA_IO_PENDING`, no completion is ever queued,
  so the connection was orphaned forever. Now the reference taken for the read is released
  right away and the session closes.
- The two copies of the call (accept loop and worker) became one function.
- There is a race on shutdown: `Close()` can run on another thread just after the `closed_`
  check and just before `WSARecv`. Its `CancelIoEx` then finds nothing to cancel, and the new
  read could hang until the client sends something. Checking `closed_` again after posting
  closes that gap. Both are `seq_cst` atomics, so at least one side always sees the other.
- An immediate success (`result == 0`) still queues a completion (we do not set
  `FILE_SKIP_COMPLETION_PORT_ON_SUCCESS`), so the worker handles it and the reference stays
  balanced.

---

## 18. New `SessionRegistry` (live sessions, for shutdown)

Old code

```cpp
// (did not exist: live connections were not tracked anywhere, so they could never be
// closed on shutdown)
```

New code

```cpp
class SessionRegistry {
public:
  void Add(Session& session) {
    std::scoped_lock lock(mutex_);
    sessions_.insert(&session);
  }

  void Remove(Session& session) {
    std::scoped_lock lock(mutex_);
    sessions_.erase(&session);
    // Notified under the lock: once the waiter wakes up it may destroy this registry, so
    // nothing here may touch it after the lock is released.
    empty_.notify_all();
  }

  void CloseAll() {
    std::scoped_lock lock(mutex_);
    for (Session* session : sessions_) {
      session->Close();
    }
  }

  void WaitUntilEmpty() {
    std::unique_lock lock(mutex_);
    empty_.wait(lock, [this] { return sessions_.empty(); });
  }

private:
  std::mutex mutex_;
  std::condition_variable empty_;
  std::unordered_set<Session*> sessions_;
};
```

I did that because a clean shutdown has to close every open connection and wait for its
cancelled I/O to finish **before** stopping the workers (otherwise those completions would
never be processed and the sessions would leak). That needs to know which sessions exist.
Details:
- `notify_all` runs **inside** the lock on purpose. If it ran after unlocking, the waiting
  thread could wake up, return, and destroy the registry while `Remove` was still using the
  condition variable.
- `CloseAll` calls `Close()` under the lock, so a session cannot be deleted halfway through:
  its final `Release()` blocks in `Remove` until `CloseAll` finishes.
- Add/Remove are O(1) on average. The lock is taken only on connect and disconnect, never per
  read.

---

## 19. The listening socket moved into `Listener`, with every call checked

Old code

```cpp
SOCKET listener_socket = WSASocketW(
  AF_INET,
  SOCK_STREAM,
  IPPROTO_TCP,
  nullptr,
  0,
  WSA_FLAG_OVERLAPPED
);

sockaddr_in server_address = { 0 };
server_address.sin_family = AF_INET;
server_address.sin_addr.s_addr = INADDR_ANY;
server_address.sin_port = htons(port_number);

bind(listener_socket, (SOCKADDR*)&server_address, sizeof(server_address));
listen(listener_socket, SOMAXCONN);

// ... listener_socket was never closed
```

New code

```cpp
class Listener {
public:
  Listener() = default;
  Listener(const Listener&) = delete;
  Listener& operator=(const Listener&) = delete;

  ~Listener() {
    Close();
  }

  std::expected<void, std::error_code> Open(std::uint16_t port_number) {
    // WSA_FLAG_OVERLAPPED is inherited by the accepted sockets, which the IOCP requires.
    const SOCKET listener_socket = WSASocketW(
      AF_INET,
      SOCK_STREAM,
      IPPROTO_TCP,
      nullptr,
      0,
      WSA_FLAG_OVERLAPPED
    );
    if (listener_socket == INVALID_SOCKET) {
      return std::unexpected(LastSocketError());
    }
    // Stored right away, so the destructor closes it if any later step fails.
    socket_ = listener_socket;

    // On Windows, SO_REUSEADDR lets another process bind the same port and steal its
    // connections. SO_EXCLUSIVEADDRUSE forbids that.
    const BOOL exclusive = TRUE;
    if (setsockopt(listener_socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR) {
      return std::unexpected(LastSocketError());
    }

    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port_number);

    if (bind(listener_socket, reinterpret_cast<const sockaddr*>(&server_address), sizeof(server_address)) == SOCKET_ERROR) {
      return std::unexpected(LastSocketError());
    }

    if (listen(listener_socket, SOMAXCONN) == SOCKET_ERROR) {
      return std::unexpected(LastSocketError());
    }

    return {};
  }

  std::expected<SOCKET, std::error_code> Accept() {
    const SOCKET client_socket = accept(socket_.load(), nullptr, nullptr);
    if (client_socket == INVALID_SOCKET) {
      return std::unexpected(LastSocketError());
    }
    return client_socket;
  }

  // Thread-safe and idempotent. A blocked Accept() returns with an error.
  void Close() {
    if (const SOCKET listener_socket = socket_.exchange(INVALID_SOCKET); listener_socket != INVALID_SOCKET) {
      closesocket(listener_socket);
    }
  }

private:
  std::atomic<SOCKET> socket_{INVALID_SOCKET};
};
```

I did that because:
- **No error checks:** if `bind` failed (port already in use), the server went on to
  `accept` on an unbound socket, which failed instantly, and `continue` made that an endless
  100%-CPU loop. Now each step returns its error. Tested: a second instance on port 8080
  exits with `WSAEADDRINUSE` (code 10048).
- **`SO_EXCLUSIVEADDRUSE`:** on Windows, without it, another process can bind the same port
  (with `SO_REUSEADDR`) and take over connections. This is the Windows counterpart of the
  `SO_REUSEADDR` topic from Phase 1, but it does the opposite of the POSIX flag.
- **The listener was never closed.** The destructor now closes it, including when `Open`
  fails halfway (the socket is stored right after it is created).
- **`Close()` is thread-safe** (`atomic::exchange`), so `TcpServer::Stop()` can call it from
  the Ctrl+C thread to unblock `accept`.
- `sockaddr_in{}` instead of `= { 0 }`, and `reinterpret_cast` instead of the C-style
  `(SOCKADDR*)` cast. Same effect, but more idiomatic and easier to search for.

---

## 20. The accept loop moved into `TcpServer::AcceptLoop`, with error classification and a stop flag

Old code

```cpp
while (true) {

  SOCKET client_socket = accept(listener_socket, nullptr, nullptr);

  if (client_socket == INVALID_SOCKET) {
    continue;
  }

  auto* context = new HandleContext;
  context->socket = client_socket;

  CreateIoCompletionPort((HANDLE)client_socket, iocp, (ULONG_PTR)context, 0);

  IoContext* io_context = new IoContext;
  ZeroMemory(&(io_context->overlapped), sizeof(WSAOVERLAPPED));
  io_context->operation = Read;
  io_context->wsa_buf.buf = io_context->buffer;
  io_context->wsa_buf.len = sizeof(io_context->buffer);

  DWORD flags = 0;
  WSARecv(client_socket, &(io_context->wsa_buf), 1, nullptr, &flags, &(io_context->overlapped), nullptr);
}
```

New code

```cpp
std::expected<void, std::error_code> AcceptLoop() {
  while (!stopping_) {
    auto client_socket = listener_.Accept();

    if (!client_socket) {
      if (stopping_) {
        break;
      }

      // These come from one connection or from a temporary lack of resources; any other
      // error means the listener itself is broken.
      const int error = client_socket.error().value();
      if (error == WSAECONNRESET || error == WSAEMFILE || error == WSAENOBUFS) {
        Log(std::format("accept failed: {}", Describe(client_socket.error())));
        continue;
      }
      return std::unexpected(client_socket.error());
    }

    auto* session = new Session(*client_socket, sessions_);
    session->Start(completion_port_);
  }
  return {};
}

void Session::Start(CompletionPort& completion_port) {
  if (auto associated = completion_port.Associate(socket_, *this); !associated) {
    Log(std::format("Failed to associate client socket with the completion port: {}", Describe(associated.error())));
    Close();
  } else {
    PostRead();
  }
  Release();
}
```

I did that because:
- `continue` on **every** error turned a broken listener into an endless loop. Now only
  errors that can go away (one client reset before being accepted, or temporarily out of
  sockets/buffers) are logged and retried. Anything else ends the loop with that error.
- The `stopping_` flag tells "we closed the listener on purpose" apart from a real failure.
- Setting up a new client (associate + first read) moved into `Session::Start`, so the accept
  loop only accepts. If `Associate` fails, the session closes, and its creator reference is
  released either way, so nothing leaks.

---

## 21. Worker count: `dwNumberOfProcessors * 2` → `hardware_concurrency()`

Old code

```cpp
SYSTEM_INFO sys_info;
GetSystemInfo(&sys_info);
// ...
uint_fast16_t thread_count = sys_info.dwNumberOfProcessors * 2;
```

New code

```cpp
// The workers never block, so one per core is enough.
completion_port_.StartWorkers(std::max(1u, std::thread::hardware_concurrency()));
```

I did that because "2 × cores" is old advice for workers that **block**. Under this project's
"don't block the reactor" rule, the I/O workers never block, so extra threads only add context
switches. Heavy work will go to a separate pool later (Phase 3). `std::max(1u, ...)` covers
`hardware_concurrency()` returning 0 when it cannot tell. It is also portable: the
`SYSTEM_INFO` call is gone.

---

## 22. Graceful shutdown: `TcpServer::Run` and `TcpServer::Stop`

Old code

```cpp
while (true) { /* accept ... never exits */ }

for (auto& worker : workers)   // unreachable
  worker.join();

WSACleanup();                  // unreachable, and before CloseHandle
CloseHandle(iocp);             // unreachable
```

New code

```cpp
class TcpServer {
public:
  TcpServer() = default;
  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;

  // Blocks until Stop() is called or accepting fails for good.
  std::expected<void, std::error_code> Run(std::uint16_t port_number) {
    if (auto opened = completion_port_.Open(); !opened) {
      return opened;
    }
    if (auto opened = listener_.Open(port_number); !opened) {
      return opened;
    }

    // The workers never block, so one per core is enough.
    completion_port_.StartWorkers(std::max(1u, std::thread::hardware_concurrency()));

    Log(std::format("Listening on port {}", port_number));
    auto result = AcceptLoop();

    // Order matters: stop accepting, close live sessions, let their cancelled I/O drain
    // through the workers, and only then stop the workers.
    listener_.Close();
    sessions_.CloseAll();
    sessions_.WaitUntilEmpty();
    completion_port_.Stop();

    return result;
  }

  // Thread-safe.
  void Stop() {
    stopping_ = true;
    listener_.Close();
  }

private:
  // AcceptLoop() — change 20

  // Declaration order is destruction order in reverse: the completion port outlives the
  // sessions that post I/O to it.
  CompletionPort completion_port_;
  SessionRegistry sessions_;
  Listener listener_;
  std::atomic<bool> stopping_{false};
};
```

I did that because the server had no way to shut down. All the cleanup code sat after an
infinite loop. The shutdown sequence is now explicit and in the right order:

1. **Stop accepting.** Close the listener, which also unblocks `accept`.
2. **Close every session.** `CancelIoEx` makes each pending read complete with
   `ERROR_OPERATION_ABORTED`.
3. **Wait** until those completions have released every session (registry is empty).
4. **Only then stop the workers.** Stopping them first would leave the cancelled completions
   unprocessed and leak the sessions.

The member order is deliberate as well: members are destroyed in reverse order, so the
`CompletionPort` is destroyed last. The `"Opening socket"` message was replaced by
`"Listening on port N"`, printed only once the listener is actually ready. Tested with a client
connection left open during shutdown: its read is cancelled, and the process exits with code 0
instead of hanging.

---

## 23. New `ConsoleStopSignal` (Ctrl+C / Ctrl+Break → `Stop()`)

Old code

```cpp
// (did not exist: the only way to end the server was to kill the process)
```

New code

```cpp
class ConsoleStopSignal {
public:
  explicit ConsoleStopSignal(TcpServer& server) {
    {
      std::scoped_lock lock(mutex_);
      server_ = &server;
    }
    SetConsoleCtrlHandler(Handler, TRUE);
  }

  ConsoleStopSignal(const ConsoleStopSignal&) = delete;
  ConsoleStopSignal& operator=(const ConsoleStopSignal&) = delete;

  ~ConsoleStopSignal() {
    SetConsoleCtrlHandler(Handler, FALSE);
    // Cleared under the lock, so a handler already running finishes with the server before
    // it is destroyed.
    std::scoped_lock lock(mutex_);
    server_ = nullptr;
  }

private:
  // Called by Windows on a thread of its own.
  static BOOL WINAPI Handler(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) {
      return FALSE;
    }

    std::scoped_lock lock(mutex_);
    if (server_ == nullptr) {
      return FALSE;
    }
    Log("Stopping...");
    server_->Stop();
    return TRUE;
  }

  static inline std::mutex mutex_;
  static inline TcpServer* server_ = nullptr;
};
```

I did that because `OpenSocket` is a blocking call with no stop function in the public API,
so something has to trigger `TcpServer::Stop()`. Windows calls a console control handler on a
**separate thread**, so:
- The handler only calls `Stop()`, which is thread-safe (atomic flag + atomic socket exchange).
- The server pointer is guarded by a mutex. The destructor clears it under that mutex, so a
  handler that is already running cannot use a `TcpServer` that is being destroyed.
- RAII: the handler is registered only while the server is alive.
- `CTRL_CLOSE_EVENT` / logoff / shutdown are not handled (`FALSE`): Windows ends the process
  right after those handlers return anyway.

Tested with `CTRL_BREAK_EVENT`: the server logs `Stopping...` and exits with code 0.

---

## 24. `Connection::OpenSocket` is now a thin entry point

Old code

```cpp
void StreamBits::Net::Connection::OpenSocket(uint16_t port_number = 8080) {

  SYSTEM_INFO sys_info;
  GetSystemInfo(&sys_info);

  std::cout << "Opening socket\n";

  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    return;
  }

  // ... ~120 lines: IOCP, workers, listener, accept loop, cleanup ...
}
```

New code

```cpp
void Connection::OpenSocket(std::uint16_t port_number) {
  WinsockRuntime winsock;
  if (auto started = winsock.Start(); !started) {
    Log(std::format("WSAStartup failed: {}", Describe(started.error())));
    return;
  }

  TcpServer server;
  ConsoleStopSignal stop_signal(server);

  if (auto result = server.Run(port_number); !result) {
    Log(std::format("Server stopped with an error: {}", Describe(result.error())));
  }
}
```

I did that because:
- **SRP:** the function only composes the pieces. Local variables are destroyed in reverse
  order: `stop_signal` first (no more Ctrl+C callbacks), then `server`, then `winsock`, so
  `WSACleanup` runs last.
- **Default argument removed from the definition:** `= 8080` on a definition does nothing for
  callers, who only see the header. It now lives in the declaration (change 25).
- It is defined inside `namespace StreamBits::Net { ... }`, so the qualified name shrinks to
  `Connection::OpenSocket`, and it uses `std::uint16_t` like the rest of the file.
- Errors are logged instead of silently returning (fail-safe: the process does not crash, and
  it says why it stopped).

---

## 25. `include/net/connection.hpp`: default argument moved to the declaration

Old code

```cpp
#pragma once
#include <cstdint>


namespace StreamBits::Net::Connection {

  void OpenSocket(uint16_t port_number);

}
```

New code

```cpp
#pragma once
#include <cstdint>


namespace StreamBits::Net::Connection {

  // Blocks until Ctrl+C / Ctrl+Break.
  void OpenSocket(uint16_t port_number = 8080);

}
```

I did that because a default argument only helps callers who can see it, which means it has to
be in the header. The comment documents the new behavior: the call blocks and is stopped with
Ctrl+C. The example (`OpenSocket(8080)`) still compiles unchanged.

---

## 26. `CMakeLists.txt`: link `ws2_32` from the build system

Old code

```cmake
if (WIN32)
    target_sources(stream-bits PRIVATE
            src/net/connection_win32.cpp
    )
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
```

New code

```cmake
if (WIN32)
    target_sources(stream-bits PRIVATE
            src/net/connection_win32.cpp
    )
    target_link_libraries(stream-bits PRIVATE ws2_32)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
```

I did that because it replaces the `#pragma comment(lib, ...)` (change 4) and works with every
Windows toolchain (MSVC, clang-cl, clang++, MinGW). `PRIVATE` is enough: no public header
exposes Winsock. For a static build, CMake still passes `ws2_32` along to whatever links
against stream-bits.

---

## Verification

- Built with the `msvc-debug` preset (`/W4 /permissive-`): no warnings, no errors.
- Runtime tests against `bin/helloworld-server.exe`:
  - `hello` → `Received 5 bytes: hello`, then a graceful close.
  - exactly 1024 bytes → all 1024 received (the old out-of-bounds case).
  - abortive close (RST) → the session is cleaned up.
  - a connection left open, then Ctrl+Break → `Stopping...`, its read is cancelled, exit code 0.
  - a second instance on the same port → `WSAEADDRINUSE (code 10048)`, clean exit.
