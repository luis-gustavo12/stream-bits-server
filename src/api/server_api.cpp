#include "api/server_api.hpp"


std::expected<StreamBits::Net::TcpHandler, ServerApiError> StreamBits::Api::OpenWebServer(uint16_t port){

  auto tcp = Net::TcpHandler(port);

  return tcp;

}
