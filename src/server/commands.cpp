#include "commands.hpp"

#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <charconv>
#include <chrono>
#include <optional>
#include <string>

namespace {
// Parses the last word as a quantity and if failed - uses the whole string as an item name
// Examples:
// - "arrow 5" -> {"arrow", 5}
// - "holy sword 1" -> {"holy sword", 1}
// - "arrow" -> {"arrow", 1}
std::pair<std::string_view, int> parse_item_name_and_count(std::string_view args) noexcept {
  // first, parse last word as a quantity and if failed - use the whole string as an item name
  int quantity = 1;
  std::size_t const space_pos = args.rfind(' ');
  if (space_pos != std::string_view::npos) {
    std::string_view const count_str = args.substr(space_pos + 1);
    auto const [_, ec] = std::from_chars(count_str.data(), count_str.data() + count_str.size(), quantity);
    if (ec == std::errc()) {
      args = args.substr(0, space_pos);
    }
  }
  return { args, quantity };
}

std::optional<std::tuple<std::string_view, int, int>> parse_sell_order(std::string_view args) noexcept {
  // first, find the price
  std::size_t const space_pos = args.rfind(' ');
  if (space_pos == std::string_view::npos) {
    return std::nullopt;
  }
  std::string_view const price_str = args.substr(space_pos + 1);  // price is the last word
  int price = 0;
  auto const [_, ec] = std::from_chars(price_str.data(), price_str.data() + price_str.size(), price);
  if (ec != std::errc()) {
    return std::nullopt;
  }
  args = args.substr(0, space_pos);

  auto const [item_name, quantity] = parse_item_name_and_count(args);
  return { { item_name, quantity, price } };
}
}  // namespace

namespace fmt {
template <>
struct formatter<SellOrder> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext & ctx) {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const SellOrder & order, FormatContext & ctx) const {
    if (order.quantity == 1) {
      return format_to(ctx.out(), "#{}: {} is selling a {} for {} funds until {}", order.id, order.user_name,
                       order.item_name, order.price, order.expiration_time);
    } else {
      return format_to(ctx.out(), "#{}: {} is selling {} {}(s) for {} funds until {}", order.id, order.user_name,
                       order.quantity, order.item_name, order.price, order.expiration_time);
    }
  }
};
}  // namespace fmt

namespace commands {

std::string ping(UserConnection & connection, std::string_view args) {
  return "pong";
}

std::string deposit(UserConnection & connection, std::string_view args) {
  auto const [item_name, quantity] = parse_item_name_and_count(args);

  auto result = connection.storage->deposit(connection.user.id, item_name, quantity);
  if (!result) {
    return fmt::format("Failed to deposit {} {}(s) with error: {}", quantity, item_name, result.error());
  }
  return fmt::format("Successfully deposited {} {}(s)", quantity, item_name);
}

std::string withdraw(UserConnection & connection, std::string_view args) {
  auto const [item_name, quantity] = parse_item_name_and_count(args);

  auto result = connection.storage->withdraw(connection.user.id, item_name, quantity);
  if (!result) {
    return fmt::format("Failed to withdraw {} {}(s) with error: {}", quantity, item_name, result.error());
  }
  return fmt::format("Successfully withdrawn {} {}(s)", quantity, item_name);
}

std::string view_items(UserConnection & connection, std::string_view) {
  auto result = connection.storage->view_items(connection.user.id);
  if (!result) {
    return fmt::format("Failed to view items with error: {}", result.error());
  }
  return fmt::format("Items: {}", fmt::join(result.value(), ", "));
}

// args should be in the format "item_name <quantity> <price>".
// Price is mandatory, quantity is optional and defaults to 1.
// Examples:
// - "arrow 5 10" -> {"arrow", .quantity=5, .price=10}
// - "holy sword 1 100" -> {"holy sword", .quantity=1, .price=100}
// - "arrow 10" -> {"arrow", .quantity=1, .price=10}
std::string sell(UserConnection & connection, std::string_view args) {
  auto const sell_order = parse_sell_order(args);
  if (!sell_order) {
    return "Failed to place sell order. Expected: 'sell <item_name> [quantity] <price>' (quantity defaults to 1)";
  }
  auto const & [item_name, quantity, price] = *sell_order;

  // expiration time is now + 5min
  namespace ch = std::chrono;
  constexpr auto const order_lifetime = ch::minutes(5);
  auto const expiration_time = ch::round<ch::seconds>(ch::system_clock::now() + order_lifetime);
  // format expiration time as "YYYY-MM-DD HH:MM:SS", like "2021-01-01 00:00:00"
  auto const expiration_time_str = fmt::format("{:%Y-%m-%d %H:%M:%S}", expiration_time);

  auto result =
      connection.storage->place_sell_order(connection.user.id, item_name, quantity, price, expiration_time_str);
  if (!result) {
    return fmt::format("Failed to place sell order for {} {}(s) with error: {}", quantity, item_name, result.error());
  }
  return fmt::format("Successfully placed sell order for {} {}(s)", quantity, item_name);
}

std::string view_sell_orders(UserConnection & connection, std::string_view args) {
  auto result = connection.storage->view_sell_orders();
  if (!result) {
    return fmt::format("Failed to view sell orders with error: {}", result.error());
  }
  std::string output = "Sell orders:\n";
  for (const auto & item : result.value()) {
    output += fmt::format("- {}\n", item);
  }
  return output;
}
}  // namespace commands
