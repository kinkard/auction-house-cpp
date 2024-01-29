#include "cli.hpp"

#include <fmt/format.h>

#include <charconv>

tl::expected<Cli, std::string> Cli::parse(int argc, char * argv[]) {
  if (argc != 4) {
    return tl::make_unexpected(
        "Invalid number of arguments\n"
        "Usage: server <port> <path_to_db> <path_to_transaction_log>\n"
        "Example: server 3000 db.sqlite transaction.log");
  }

  uint16_t port;
  std::from_chars_result result = std::from_chars(argv[1], std::strchr(argv[1], '\0'), port);
  if (result.ec != std::errc()) {
    return tl::make_unexpected(fmt::format("Invalid port '{}'. Port must be in range [1, 65535]", argv[1]));
  }

  return Cli{
    .port = port,
    .db_path = argv[2],
    .transaction_log_path = argv[3],
  };
}
