#pragma once

#include <expected>



namespace StreamBits{

  void Print();

}

namespace StreamBits::Server{


  enum class ServerError{
    InvalidPort,
    MessageFailed,
    Other
  };

  class Server{

  public:

    Server() = default;

    std::expected<Server, ServerError> Init([[maybe_unused]] int port){

      return Server();
    }

  };


}


