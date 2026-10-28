#pragma once
#include <cstdint>
#include <optional>
#include "connection.hpp"
#include "io_handler.hpp"


namespace StreamBits::Net {

  class TcpHandler {

  public:

    static std::optional<TcpHandler> Create(uint16_t port_number) noexcept;

    // explicit TcpHandler(uint16_t port_number) : m_handler(IoFacadeIoHandler::Create(port_number).value()) {
    // }

    void Run();

    TcpHandler(Connection&) = delete;

  private:

    explicit TcpHandler(Connection&& connection) : m_connection(std::move(connection)) {}

    Connection m_connection;



  };





}



