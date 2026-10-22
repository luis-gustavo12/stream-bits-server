#pragma once
#include <cstdint>

#include "io_handler.hpp"


namespace StreamBits::Net {

  class TcpHandler {

  public:
    explicit TcpHandler(uint16_t port_number) : m_handler(port_number) {

    }

    void Run();

  private:

    IoHandler m_handler;


  };





}



