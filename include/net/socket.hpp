#pragma once
#include <string>


namespace StreamBits::Net::Socket {

  inline thread_local std::string error_message;

  enum class Error {
    SocketInitializationError,
    InvalidSocketError,
    SocketBindingError,
  };

}
