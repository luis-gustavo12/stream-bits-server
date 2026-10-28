

#include "net/io_handler.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdint>
#include <iostream>
#include <system_error>
#include <thread>
#include <vector>


#pragma comment(lib, "Ws2_32.lib")

namespace StreamBits::Net {

namespace {

  enum Operation {
    Read,
    Write
  };

  struct IoContext {

    explicit IoContext(std::uint32_t size)
      : buffer(std::make_unique<char[]>(size)), buffer_size(size) {}

    WSAOVERLAPPED overlapped { 0 };
    WSABUF wsa_buf { 0 };
    std::unique_ptr<char[]> buffer;
    std::uint32_t buffer_size;
    Operation operation;
    DWORD bytes_total{};
    DWORD bytes_sent{};
  };

  struct HandleContext {
    SOCKET socket;
  };

  void CloseConnection(HandleContext* context, IoContext* io_context) {
    closesocket(context->socket);
    delete context;
    delete io_context;
  }

}

void IoFacadeIoHandler::ProcessRequest() {

  auto thread_id = std::this_thread::get_id();

  while (true) {

    HandleContext* handle_context = nullptr;
    IoContext* io_context = nullptr;

    DWORD bytes_transferred = 0;
    ULONG_PTR completion_key = 0;
    LPOVERLAPPED overlapped = nullptr;

    BOOL success = GetQueuedCompletionStatus(
      m_iocp,
      &bytes_transferred,
      &completion_key,
      &overlapped,
      INFINITE
    );

    const DWORD error = success ? ERROR_SUCCESS : GetLastError();

    if (overlapped == nullptr) {
      if (success) {
        break;
      }

      std::cerr << "Thread number " << thread_id
                << ": GetQueuedCompletionStatus failed without an I/O packet, error "
                << error << "\n";
      break;
    }

    handle_context = reinterpret_cast<HandleContext*>(completion_key);
    io_context = CONTAINING_RECORD(overlapped, IoContext, overlapped);

    if (!success || (bytes_transferred == 0 && io_context->operation == Read)) {
      DWORD error = success ? 0 : GetLastError();

      std::cout << "Thread number " << thread_id << " closing connection: ";
      if (!success)
        std::cout << (io_context->operation == Read ? "read" : "write")
                  << " failed, error " << error << "\n";
      else
        std::cout << "client closed the connection (0 bytes read)\n";

      CloseConnection(handle_context, io_context);
      continue;
    }

    std::cout << "Thread number " << thread_id << " being awaken";

    if (io_context->operation == Read)
      std::cout << " to read something\n";
    else
      std::cout << " to write something\n";

    if (io_context->operation == Read) {
      std::cout << "Received |" << bytes_transferred << "|\n";
      std::cout.write(io_context->buffer.get(), bytes_transferred);
      std::cout << "\n";

      auto response_size = static_cast<std::size_t>(RESPONSE_MESSAGE_SIZE);

      if (response_size > io_context->buffer_size) {
        std::cerr << "Thread number " << thread_id
                  << " closing connection: response does not fit in the I/O buffer\n";

        CloseConnection(handle_context, io_context);
        continue;
      }

      std::memcpy(io_context->buffer.get(), RESPONSE_MESSAGE, RESPONSE_MESSAGE_SIZE);
      io_context->bytes_total = static_cast<DWORD>(RESPONSE_MESSAGE_SIZE);
      io_context->bytes_sent = 0;
      io_context->operation = Write;

    } else {
      std::cout << "Writing operation!\n";
      io_context->bytes_sent += bytes_transferred;
      if (io_context->bytes_sent == io_context->bytes_total) {
        CloseConnection(handle_context, io_context);
        continue;
      }
    }

    ZeroMemory(&io_context->overlapped, sizeof(WSAOVERLAPPED));

    io_context->wsa_buf.buf = io_context->buffer.get() + io_context->bytes_sent;
    io_context->wsa_buf.len = io_context->bytes_total - io_context->bytes_sent;

    // DWORD flags = 0;
    // int send_result = WSARecv(
    //   handle_context->socket,
    //   &(io_context->wsa_buf), 1,
    //   nullptr, &flags,
    //   &(io_context->overlapped), nullptr
    // );

    int send_result = WSASend(
      handle_context->socket,
      &(io_context->wsa_buf), 1,
      nullptr, 0,
      &(io_context->overlapped), nullptr
    );

    if (send_result == SOCKET_ERROR) {
      int error = WSAGetLastError();
      if (error != WSA_IO_PENDING) {
        std::cout << "Thread number " << thread_id
                  << " closing connection: WSASend failed immediately, error " << error << "\n";
        CloseConnection(handle_context, io_context);
      }
    }

  }

}


std::expected<IoFacadeIoHandler, Socket::Error> IoFacadeIoHandler::Create(std::uint16_t port_number,
  std::size_t default_buffer_size) {

  if (default_buffer_size == 0 || default_buffer_size > std::numeric_limits<std::uint32_t>::max()) {
    return std::unexpected(Socket::Error::SocketInitializationError);
  }

  IoFacadeIoHandler io_handler(port_number, default_buffer_size);

  auto init_result = io_handler.OpenSocket();

  if (!init_result) {
    return std::unexpected(Socket::Error::SocketInitializationError);
  }

  auto configure_result = io_handler.ConfigureSocket();

  if (!configure_result)
    return std::unexpected(Socket::Error::SocketBindingError);

  return io_handler;

}

IoFacadeIoHandler::~IoFacadeIoHandler() {
  Release();
}

IoFacadeIoHandler::IoFacadeIoHandler(IoFacadeIoHandler&& io) noexcept
  : m_port_number(io.m_port_number),
    m_buffer_size(std::exchange(io.m_buffer_size, 0)),
    m_buffer(std::move(io.m_buffer)),
    m_on_received_message(std::move(io.m_on_received_message)),
    m_on_response_message(std::move(io.m_on_response_message)),
    m_listener_socket(std::exchange(io.m_listener_socket, INVALID_SOCKET)),
    m_iocp(std::exchange(io.m_iocp, nullptr)),
    m_wsa_data(io.m_wsa_data),
    m_wsa_started(std::exchange(io.m_wsa_started, FALSE)) {}


IoFacadeIoHandler& IoFacadeIoHandler::operator=(IoFacadeIoHandler&& io) noexcept {
  if (this != &io) {
    Release();

    m_port_number         = io.m_port_number;
    m_buffer_size         = std::exchange(io.m_buffer_size, 0);
    m_buffer              = std::move(io.m_buffer);
    m_on_received_message = std::move(io.m_on_received_message);
    m_on_response_message = std::move(io.m_on_response_message);
    m_listener_socket     = std::exchange(io.m_listener_socket, INVALID_SOCKET);
    m_iocp                = std::exchange(io.m_iocp, nullptr);
    m_wsa_data            = io.m_wsa_data;
    m_wsa_started         = std::exchange(io.m_wsa_started, FALSE);
  }
  return *this;
}

void IoFacadeIoHandler::Release() noexcept {

  if (m_listener_socket != INVALID_SOCKET) {
    closesocket(m_listener_socket);
    m_listener_socket = INVALID_SOCKET;
  }

  if (m_iocp != nullptr) {
    CloseHandle(m_iocp);
    m_iocp = nullptr;
  }

  if (m_wsa_started) {
    WSACleanup();
    m_wsa_started = FALSE;
  }
}

std::expected<void, Socket::Error> IoFacadeIoHandler::OpenSocket() {

  auto startup_result = WSAStartup(MAKEWORD(2, 2), &m_wsa_data);
  if (startup_result != 0) {
    Socket::error_message = std::system_category().message(startup_result);
    return std::unexpected(Socket::Error::SocketInitializationError);
  }

  m_wsa_started = true;
  return {};
}

std::expected<void, Socket::Error> IoFacadeIoHandler::ConfigureSocket() {

  m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
  if (m_iocp == nullptr) {
    WSACleanup();
    return std::unexpected(Socket::Error::SocketInitializationError);
  }

  m_listener_socket = WSASocketW(
    AF_INET,
    SOCK_STREAM,
    IPPROTO_TCP,
    nullptr,
    0,
    WSA_FLAG_OVERLAPPED
  );

  if (m_listener_socket == INVALID_SOCKET) {
    Release();
    return std::unexpected(Socket::Error::InvalidSocketError);
  }

  BOOL exclusive = TRUE;
  if (setsockopt(m_listener_socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                 reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR) {
    closesocket(m_listener_socket);
    CloseHandle(m_iocp);
    WSACleanup();
    return std::unexpected(Socket::Error::SocketBindingError);
                 }

  if (m_listener_socket == INVALID_SOCKET) {
    CloseHandle(m_iocp);
    WSACleanup();
    return std::unexpected(Socket::Error::InvalidSocketError);
  }

  sockaddr_in server = { 0 };
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(m_port_number);

  if ( bind(m_listener_socket, reinterpret_cast<SOCKADDR*>(&server), sizeof(server)) == SOCKET_ERROR ) {
    closesocket(m_listener_socket);
    CloseHandle(m_iocp);
    WSACleanup();
    return std::unexpected(Socket::Error::SocketBindingError);
  }

  if (listen(m_listener_socket, SOMAXCONN) == SOCKET_ERROR) {
    closesocket(m_listener_socket);
    CloseHandle(m_iocp);
    WSACleanup();
    return std::unexpected(Socket::Error::SocketBindingError);
  }

  return {};
}

void IoFacadeIoHandler::RunSocket() {

  SYSTEM_INFO sys_info;
  GetSystemInfo(&sys_info);

  auto thread_count = sys_info.dwNumberOfProcessors * 2;
  std::vector<std::thread> workers;
  workers.reserve(thread_count);

  for (std::uint16_t i = 0; i < thread_count; i++) {
    workers.emplace_back(&IoFacadeIoHandler::ProcessRequest, this);
  }

  while (true) {

    SOCKET client_socket = accept(m_listener_socket, nullptr, nullptr);

    if (client_socket == INVALID_SOCKET) {
      auto error_code = WSAGetLastError();
      std::string error_msg = std::system_category().message(error_code);

      std::cerr << "Failed to accept incoming connection on port " << m_port_number
                << " (Error " << error_code << "): " << error_msg << "\n";


      if (error_code == WSAECONNRESET)
        continue;

      if (error_code == WSAEMFILE || error_code == WSAENOBUFS) {
        continue;
      }

      closesocket(m_listener_socket);
      m_listener_socket = INVALID_SOCKET;
      break;
    }

    auto* io_context = new IoContext(static_cast<std::uint32_t>(m_buffer_size));
    auto* context = new HandleContext;

    context->socket = client_socket;

    HANDLE handle = CreateIoCompletionPort(
      (HANDLE)client_socket,
      m_iocp,
      (ULONG_PTR)context, 0
    );

    if (!handle) {
      CloseConnection(context, io_context);
      continue;
    }

    ZeroMemory(&(io_context->overlapped), sizeof(WSAOVERLAPPED));
    io_context->operation = Read;
    io_context->wsa_buf.buf = io_context->buffer.get();
    io_context->wsa_buf.len = io_context->buffer_size;

    DWORD flags = 0;
    auto result = WSARecv(
      client_socket,
      &(io_context->wsa_buf), 1,
      nullptr, &flags,
      &(io_context->overlapped), nullptr
    );

    if (result == SOCKET_ERROR) {
      const int error = WSAGetLastError();

      if (error != WSA_IO_PENDING) {
        std::cout << "Error: " << error << "\n";
        CloseConnection(context, io_context);
        continue;
      }
    }

  } // while (true)

  for (size_t i = 0; i < workers.size(); ++i)
    PostQueuedCompletionStatus(m_iocp, 0, 0, nullptr);

  for (auto& worker : workers)
    worker.join();

  Release();
}

void IoFacadeIoHandler::SetOnReceive(MessageCallback&& msg) {
  m_on_received_message = std::move(msg);
}

void IoFacadeIoHandler::SetOnResponse(MessageCallback&& msg) {
  m_on_response_message = std::move(msg);
}
}
