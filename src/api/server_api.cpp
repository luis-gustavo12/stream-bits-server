#include "api/server_api.hpp"


std::expected<StreamBits::Net::TcpHandler, ServerApiError> StreamBits::Api::OpenWebServer(uint16_t port){

  auto tcp = Net::TcpHandler::Create(port);

  if (!tcp)
    return std::unexpected(ServerApiError::GeneralError);

  return std::move(tcp.value());
}
