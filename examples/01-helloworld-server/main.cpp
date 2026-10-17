#include <stream-bits/stream_bits.hpp>

#include "net/connection.hpp"

int main() {
  StreamBits::Print();

  StreamBits::Net::Connection::OpenSocket(8080);

  return 0;
}
