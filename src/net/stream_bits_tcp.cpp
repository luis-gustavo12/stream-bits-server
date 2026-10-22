
#include "../../include/net/stream_bits_tcp.hpp"

namespace StreamBits::Net {

  void TcpHandler::Run() {
    m_handler.RunSocket();
  }
}
