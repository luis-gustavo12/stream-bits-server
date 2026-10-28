#pragma once
#include <optional>

#include "http.hpp"
#include "io_handler.hpp"


namespace StreamBits::Net {
  /**
   * This is the class that will wrap both the OS abstraction and HTTP parser.
   */
  class Connection {

    Http::Parser m_parser;
    IoFacadeIoHandler m_io_handler;

  public:
    static std::optional<Connection> Create(uint16_t port_number = DEFAULT_TCP_PORT,
      uint16_t default_buffer_size = Http::DEFAULT_BUFFER_SIZE) noexcept;

    void RunSocket() {
      m_io_handler.RunSocket();
    }

  private:

    explicit Connection(Http::Parser&& parser, IoFacadeIoHandler&& io_handler) :
      m_parser(std::move(parser)), m_io_handler(std::move(io_handler)) {
    }

  };



}
