#pragma once
#include <cstdint>
#include <expected>

#include "net/io_handler.hpp"
#include "net/stream_bits_tcp.hpp"


struct ServerApiError{




};




namespace StreamBits::Api{

  std::expected<Net::TcpHandler, ServerApiError> OpenWebServer(uint16_t port);

}




