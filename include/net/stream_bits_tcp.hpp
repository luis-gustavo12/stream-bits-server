#pragma once
#include <cstdint>
#include <optional>
#include "connection.hpp"
#include "io_handler.hpp"


namespace StreamBits::Net {

  class TcpHandler {

  public:
    explicit TcpHandler(uint16_t port_number) : m_handler(IoFacadeIoHandler::Create(port_number).value()) {
    }

    void Run();

  private:

    IoFacadeIoHandler m_handler;


  };





}



