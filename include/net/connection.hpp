#pragma once
#include "http.hpp"
#include "io_handler.hpp"


namespace StreamBits::Net {
  /**
   * This is the class that will wrap both the OS abstraction and HTTP parser.
   */
  class Connection {



  private:
    Http::Parser m_parser;
    IoHandler m_io_handler;


  };



}
