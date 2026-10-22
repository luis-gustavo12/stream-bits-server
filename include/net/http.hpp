#pragma once
#include <string>
#include <cstdint>

namespace StreamBits::Http {

  constexpr uint32_t DEFAULT_BUFFER_SIZE = 1024;


  struct Config {

    uint16_t buffer_size;

  };

  /*
   *
   *   This is the http parser. Since both Linux, Windows and even Unix don't give the entire
   * String from request, it's needed to keep "polling" it continuously, until it sees the end
   * of it. So, to know the end of the request, its known that it must finish with specific
   * characters. But what if it doesn't have them? That's why this application will have two conditions
   * to finish the parser, which are time and space. I'll pick the space to be configured by the developers
   *
   */
  class Parser {

  public:

    enum State {
      Begin,
    };

    explicit Parser(uint32_t initial_size = DEFAULT_BUFFER_SIZE) {
      m_buffer.reserve(initial_size);
    }

    void Increment(char* buffer) noexcept;
    void Increment(std::string buffer) noexcept;

  private:

    std::string m_buffer;
    State m_parser_state = Begin;

  };




}
