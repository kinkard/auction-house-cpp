#include "commands.hpp"

#include <fmt/format.h>

#include <charconv>
#include <string>
#include "fmt/ranges.h"

namespace {
// Parses the last word as a count and if failed - uses the whole string as an item name
// Examples:
// - "arrow 5" -> {"arrow", 5}
// - "holy sword 1" -> {"holy sword", 1}
// - "arrow" -> {"arrow", 1}
std::pair<std::string_view, int> parse_item_name_and_count(std::string_view args) noexcept {
  // first, parse last word as a count and if failed - use the whole string as an item name
  int count = 1;
  std::size_t const space_pos = args.rfind(' ');
  if (space_pos != std::string_view::npos) {
    std::string_view const count_str = args.substr(space_pos + 1);
    auto const [_, ec] = std::from_chars(count_str.data(), count_str.data() + count_str.size(), count);
    if (ec == std::errc()) {
      args = args.substr(0, space_pos);
    }
  }
  return { args, count };
}
}  // namespace

namespace commands {

std::string ping(UserConnection & connection, std::string_view args) {
  return "pong";
}

std::string deposit(UserConnection & connection, std::string_view args) {
  auto const [item_name, count] = parse_item_name_and_count(args);

  auto result = connection.storage->deposit(connection.user.id, item_name, count);
  if (!result) {
    return fmt::format("Failed to deposit {} {}(s) with error {}", count, item_name, result.error());
  }
  return fmt::format("Successfully deposited {} {}(s)", count, item_name);
}

std::string withdraw(UserConnection & connection, std::string_view args) {
  auto const [item_name, count] = parse_item_name_and_count(args);

  auto result = connection.storage->withdraw(connection.user.id, item_name, count);
  if (!result) {
    return fmt::format("Failed to withdraw {} {}(s) with error {}", count, item_name, result.error());
  }
  return fmt::format("Successfully withdrawn {} {}(s)", count, item_name);
}

std::string view_items(UserConnection & connection, std::string_view) {
  auto result = connection.storage->view_items(connection.user.id);
  if (!result) {
    return fmt::format("Failed to view items with error {}", result.error());
  }
  return fmt::format("Items: {}", fmt::join(result.value(), ", "));
}

}  // namespace commands
