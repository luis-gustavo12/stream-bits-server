

#include "net/connection.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>


#pragma comment(lib, "Ws2_32.lib")

constexpr char RESPONSE_MESSAGE[] =
  "HTTP/1.1 200 OK\r\n"
  "Content-Type: text/plain\r\n"
  "Content-Length: 17\r\n"
  "Connection: close\r\n"
  "\r\n"
  "Hello from Server";

constexpr size_t RESPONSE_MESSAGE_SIZE = sizeof(RESPONSE_MESSAGE) - 1;

namespace {


  enum Operation {
    Read,
    Write
  };

  struct IoContext {
    WSAOVERLAPPED overlapped;
    WSABUF wsa_buf;
    char buffer [1024];
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

  void ProcessRequest(HANDLE iocp, uint16_t thread_number) {

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
        std::cout.write(io_context->buffer, bytes_transferred);
        std::cout << "\n";

        memcpy(io_context->buffer, RESPONSE_MESSAGE, RESPONSE_MESSAGE_SIZE);
        io_context->bytes_total = RESPONSE_MESSAGE_SIZE;
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
      io_context->wsa_buf.buf = io_context->buffer + io_context->bytes_sent;
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

void StreamBits::Net::Connection::OpenSocket(uint16_t port_number = 8080) {

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

  uint_fast16_t thread_count = sys_info.dwNumberOfProcessors * 2;

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
  server_address.sin_port = htons(port_number);

  if (bind(listener_socket, (SOCKADDR*)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
    int error = WSAGetLastError();
    std::cerr << "bind failed on port " << port_number << ": " << error << "\n";
    closesocket(listener_socket);
    CloseHandle(iocp);
    WSACleanup();
    return;
  }

  listen(listener_socket, SOMAXCONN);

  for (uint16_t i = 0; i < thread_count; i++) {
    workers.emplace_back(ProcessRequest, iocp, i);
  }

  while (true) {

    SOCKET client_socket = accept(listener_socket, nullptr, nullptr);

    if (client_socket == INVALID_SOCKET) {
      int error = WSAGetLastError();
      std::cerr << "bind failed on port " << port_number << ": " << error << "\n";
      closesocket(listener_socket);
      CloseHandle(iocp);
      WSACleanup();
      break;
    }

    auto* context = new HandleContext;

    context->socket = client_socket;
    IoContext* io_context = new IoContext;

    HANDLE handle = CreateIoCompletionPort(
      (HANDLE)client_socket,
      iocp,
      (ULONG_PTR)context, 0
    );

    if (!handle) {
      CloseConnection(context, io_context);
      continue;;
    }

    ZeroMemory(&(io_context->overlapped), sizeof(WSAOVERLAPPED));
    io_context->operation = Read;
    io_context->wsa_buf.buf = io_context->buffer;
    io_context->wsa_buf.len = sizeof(io_context->buffer);

    DWORD flags = 0;
    auto result = WSARecv(
      client_socket,
      &(io_context->wsa_buf), 1,
      nullptr, &flags,
      &(io_context->overlapped), nullptr
    );

    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
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
