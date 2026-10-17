#pragma once
#include <cstdint>
#include <expected>


struct ServerApiError{




};




namespace StreamBits::Api{

  std::expected<bool, ServerApiError> OpenWebServer(uint16_t port);




}




