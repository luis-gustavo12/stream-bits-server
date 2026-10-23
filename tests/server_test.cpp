#include <gtest/gtest.h>
#include <stream-bits/stream_bits.hpp>

TEST(Server, InitReturnsServer) {
  StreamBits::Server::Server server;

  auto result = server.Init(8080);

  EXPECT_TRUE(result.has_value());
}
