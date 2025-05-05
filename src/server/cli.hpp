#pragma once

#include <tl/expected.hpp>

#include <cstdint>
#include <string_view>

// Command line arguments for the server
struct Cli {
  // port to listen on
  uint16_t port;
  // path to the sqlite3 database file
  std::string_view db_path;
  // path to the transaction log file
  std::string_view transaction_log_path;

  static tl::expected<Cli, std::string> parse(int argc, char * argv[]);
};
