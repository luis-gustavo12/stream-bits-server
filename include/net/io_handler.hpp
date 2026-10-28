#pragma once
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <expected>

#include "http.hpp"
#include "socket.hpp"


namespace StreamBits::Net {

  using MessageCallback = std::function<void (char* msg, std::size_t msg_size)>;

  constexpr char RESPONSE_MESSAGE[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 17\r\n"
    "Connection: close\r\n"
    "\r\n"
    "Hello from Server";

  constexpr size_t RESPONSE_MESSAGE_SIZE = sizeof(RESPONSE_MESSAGE) - 1;

  constexpr uint16_t DEFAULT_TCP_PORT = 8080;

  class IoFacadeIoHandler {

  public:

    static std::expected<IoFacadeIoHandler, Socket::Error> Create(std::uint16_t port_number = 8080, std::size_t default_buffer_size = Http::DEFAULT_BUFFER_SIZE);
    void RunSocket();
    void SetOnReceive(MessageCallback&& msg);
    void SetOnResponse(MessageCallback&& msg);
    ~IoFacadeIoHandler();
    IoFacadeIoHandler(const IoFacadeIoHandler&) = delete;
    IoFacadeIoHandler& operator=(const IoFacadeIoHandler&) = delete;

    IoFacadeIoHandler(IoFacadeIoHandler&& io) noexcept;
    IoFacadeIoHandler& operator=(IoFacadeIoHandler&& io) noexcept;

  private:

    explicit IoFacadeIoHandler(std::uint16_t port_number = DEFAULT_TCP_PORT,
                       std::size_t default_buffer_size = Http::DEFAULT_BUFFER_SIZE) :
      m_port_number(port_number),
      m_buffer_size(default_buffer_size),
      m_buffer(std::make_unique<char[]>(default_buffer_size)) {

    }

    void Release() noexcept;

    std::expected<void, Socket::Error> OpenSocket();
    std::expected<void, Socket::Error> ConfigureSocket();

    std::uint16_t m_port_number;
    std::size_t m_buffer_size;
    std::unique_ptr<char[]> m_buffer;
    MessageCallback m_on_received_message; // callback for when the message arrives
    MessageCallback m_on_response_message; // callback for responding the request


  };



}
