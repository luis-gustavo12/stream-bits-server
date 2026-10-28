#include "../../include/net/stream_bits_tcp.hpp"

namespace StreamBits::Net {
  std::optional<TcpHandler> TcpHandler::Create(uint16_t port_number) noexcept {

    auto connection = Connection::Create(port_number);

    if (!connection) {
      // TODO: log general error message
      return std::nullopt;
    }

    return TcpHandler(std::move(connection.value()));

  }

  void TcpHandler::Run() {
    m_connection.RunSocket();
  }
}
