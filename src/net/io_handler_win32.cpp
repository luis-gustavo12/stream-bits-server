

#include "net/io_handler.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdint>
#include <iostream>
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

    WSAOVERLAPPED overlapped;
    WSABUF wsa_buf;
    std::unique_ptr<char[]> buffer;
    std::uint32_t buffer_size;
    Operation operation;
    DWORD bytes_total;
    DWORD bytes_sent;
  };

  struct HandleContext {
    SOCKET socket;
  };

  void CloseConnection(HandleContext* context, IoContext* io_context) {
    closesocket(context->socket);
    delete context;
    delete io_context;
  }

  void ProcessRequest(HANDLE iocp, std::uint16_t thread_number) {

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

      if (io_context == nullptr)
        break;

      if (!success || (bytes_transferred == 0 && io_context->operation == Read)) {
        DWORD error = success ? 0 : GetLastError();

        std::cout << "Thread number " << thread_number << " closing connection: ";
        if (!success)
          std::cout << (io_context->operation == Read ? "read" : "write")
                    << " failed, error " << error << "\n";
        else
          std::cout << "client closed the connection (0 bytes read)\n";

        CloseConnection(handle_context, io_context);
        continue;
      }

      std::cout << "Thread number " << thread_number << " being awaken";

      if (io_context->operation == Read)
        std::cout << " to read something\n";
      else
        std::cout << " to write something\n";

      if (io_context->operation == Read) {
        std::cout << "Received |" << bytes_transferred << "|\n";
        std::cout.write(io_context->buffer.get(), bytes_transferred);
        std::cout << "\n";

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

      DWORD flags = 0;
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
          std::cout << "Thread number " << thread_number
                    << " closing connection: WSASend failed immediately, error " << error << "\n";
          CloseConnection(handle_context, io_context);
        }
      }

    }

  }

}

void IoHandler::RunSocket() {

  SYSTEM_INFO sys_info;
  GetSystemInfo(&sys_info);

  std::cout << "Opening socket\n";

  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    return;
  }

  HANDLE iocp = CreateIoCompletionPort(
    INVALID_HANDLE_VALUE,
    nullptr,
    0, 0
  );

  std::uint_fast16_t thread_count = sys_info.dwNumberOfProcessors * 2;

  std::vector<std::thread> workers;
  workers.reserve(thread_count);

  SOCKET listener_socket = WSASocketW(
    AF_INET,
    SOCK_STREAM,
    IPPROTO_TCP,
    nullptr,
    0,
    WSA_FLAG_OVERLAPPED
  );

  if (listener_socket == INVALID_SOCKET) {
    CloseHandle(iocp);
    WSACleanup();
    return;
  }

  sockaddr_in server_address = { 0 };
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(m_port_number);

  if (bind(listener_socket, (SOCKADDR*)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
    int error = WSAGetLastError();
    std::cerr << "bind failed on port " << m_port_number << ": " << error << "\n";
    closesocket(listener_socket);
    CloseHandle(iocp);
    WSACleanup();
    return;
  }

  listen(listener_socket, SOMAXCONN);

  for (std::uint16_t i = 0; i < thread_count; i++) {
    workers.emplace_back(ProcessRequest, iocp, i);
  }

  while (true) {

    SOCKET client_socket = accept(listener_socket, nullptr, nullptr);

    if (client_socket == INVALID_SOCKET) {
      int error = WSAGetLastError();
      std::cerr << "bind failed on port " << m_port_number << ": " << error << "\n";
      closesocket(listener_socket);
      CloseHandle(iocp);
      WSACleanup();
      break;
    }

    IoContext* io_context = new IoContext(static_cast<std::uint32_t>(m_buffer_size));
    auto* context = new HandleContext;

    context->socket = client_socket;

    HANDLE handle = CreateIoCompletionPort(
      (HANDLE)client_socket,
      iocp,
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

    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
      std::cout << "Error: " << WSAGetLastError() << "\n";
      CloseConnection(context, io_context);
    }

  }

  for (size_t i = 0; i < workers.size(); ++i)
    PostQueuedCompletionStatus(iocp, 0, 0, nullptr);

  for (auto& worker : workers)
    worker.join();

  WSACleanup();

  CloseHandle(iocp);
}

}
