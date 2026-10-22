#include <stream-bits/stream_bits.hpp>

#include "api/server_api.hpp"

int main() {
  StreamBits::Print();

  auto result = StreamBits::Api::OpenWebServer(8080);

  if (result.has_value()) {
    auto server = std::move(*result);
    server.Run();
  }


  return 0;
}
