#include "commands.hpp"

#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <charconv>
#include <chrono>
#include <optional>
#include <string>

namespace {
struct UserCommand {
  // Stores a pointer to a function that takes UserConnection and args and returns a string
  std::string (*invoke)(UserConnection &, std::string_view);

  std::string_view description;
};

static std::unordered_map<std::string_view, UserCommand> const kAllCommands = {
  { "ping", { commands::ping, { "Replies 'pong'" } } },
  { "whoami", { commands::whoami, { "Displays the username of the current user" } } },
  { "help", { commands::help, { "Prints this help message about all available commands" } } },

  { "deposit",
    { commands::deposit,
      { "Deposits a specified amount into the user's account. Format: 'deposit <item name> [<quantity>]'" } } },
  { "withdraw",
    { commands::withdraw,
      { "Withdraws a specified amount from the user's account. Format: 'withdraw <item name> [<quantity>]'" } } },
  { "view_items", { commands::view_items, { "Displays a list items for the current user" } } },

  { "sell",
    { commands::sell,
      { "Places an item for sale at a specified price. Format: 'sell [immediate|auction] <item_name> [<quantity>] "
        "<price>'" } } },
  { "buy",
    { commands::buy,
      { "Executes immediate sell order or places a bid on a auction sell order. Format: 'buy <sell_order_id> "
        "[<bid>]'" } } },
  { "view_sell_orders",
    { commands::view_sell_orders, { "Displays a list of all current sell orders from all users" } } },
};

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

std::pair<std::string_view, std::string_view> parse_command(std::string_view request) noexcept {
  std::size_t const space_pos = request.find(' ');
  if (space_pos == std::string_view::npos) {
    return { request, {} };
  }
  return { request.substr(0, space_pos), request.substr(space_pos + 1) };
}

template <typename ValueT>
std::string print_keys(std::unordered_map<std::string_view, ValueT> const & commands) {
  std::string result;
  for (auto const & [command_name, _] : commands) {
    result += command_name;
    result += '|';
  }
  result.pop_back();  // remove the last '|'
  return result;
}
}  // namespace

namespace fmt {
template <>
struct formatter<SellOrderType> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext & ctx) {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const SellOrderType & order_type, FormatContext & ctx) const {
    return ::fmt::format_to(ctx.out(), "{}", to_string(order_type));
  }
};

template <>
struct formatter<SellOrder> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext & ctx) {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const SellOrder & order, FormatContext & ctx) const {
    std::string_view const order_type_str = order.type == SellOrderType::Auction ? "on auction " : "";

    if (order.quantity == 1) {
      return ::fmt::format_to(ctx.out(), "#{}: {} is selling a {} for {} funds {}until {}", order.id,
                                   order.seller_name,
                       order.item_name, order.price, order_type_str, order.expiration_time);
    } else {
      return ::fmt::format_to(ctx.out(), "#{}: {} is selling {} {}(s) for {} funds {}until {}", order.id,
                              order.seller_name,
                       order.quantity, order.item_name, order.price, order_type_str, order.expiration_time);
    }
  }
};
}  // namespace fmt

namespace commands {

std::string ping(UserConnection &, std::string_view) {
  return "pong";
}

std::string whoami(UserConnection & connection, std::string_view) {
  return fmt::format("{}", connection.user.username);
}

std::string help(UserConnection &, std::string_view) {
  std::string output = "Available commands:\n";
  for (auto const & [command_name, command] : kAllCommands) {
    output += fmt::format("- {} - {}\n", command_name, command.description);
  }
  output += fmt::format("Usage: <command> [<args>], where `[]` annotates optional argumet(s)\n");
  return output;
}

std::string deposit(UserConnection & connection, std::string_view args) {
  auto const [item_name, quantity] = parse_item_name_and_count(args);

  auto result = connection.shared_state->storage.deposit(connection.user.id, item_name, quantity);
  if (!result) {
    return fmt::format("Failed to deposit {} {}(s) with error: {}", quantity, item_name, result.error());
  }

  connection.shared_state->transaction_log.log(
      connection.user.id, fmt::format("deposited .item_id={} .quantity={}", result->item_id, quantity));
  return fmt::format("Successfully deposited {} {}(s)", quantity, item_name);
}

std::string withdraw(UserConnection & connection, std::string_view args) {
  auto const [item_name, quantity] = parse_item_name_and_count(args);

  auto result = connection.shared_state->storage.withdraw(connection.user.id, item_name, quantity);
  if (!result) {
    return fmt::format("Failed to withdraw {} {}(s) with error: {}", quantity, item_name, result.error());
  }
  connection.shared_state->transaction_log.log(
      connection.user.id, fmt::format("withdrawn .item_id={} .quantity={}", result->item_id, quantity));
  return fmt::format("Successfully withdrawn {} {}(s)", quantity, item_name);
}

std::string view_items(UserConnection & connection, std::string_view) {
  auto result = connection.shared_state->storage.view_items(connection.user.id);
  if (!result) {
    return fmt::format("Failed to view items with error: {}", result.error());
  }
  return fmt::format("Items: [{}]", fmt::join(result.value(), ", "));
}

// args should be in the format "[immediate|auction] <item_name> [quantity] <price>".
// Price is mandatory, quantity is optional and defaults to 1.
// Examples:
// - "arrow 5 10" -> {"arrow", .quantity=5, .price=10, .type=Immediate}
// - "holy sword 1 100" -> {"holy sword", .quantity=1, .price=100, .type=Immediate}
// - "arrow 10" -> {"arrow", .quantity=1, .price=10, .type=Immediate}
// - "immidiate arrow 10 5" -> {"arrow", .quantity=10, .price=5, .type=Immediate}
// - "auction arrow 10 5" -> {"arrow", .quantity=10, .price=5, .type=Auction}
std::string sell(UserConnection & connection, std::string_view args) {
  // parse optional order type, if none - use immediate
  SellOrderType order_type = SellOrderType::Immediate;
  std::size_t const space_pos = args.find(' ');
  if (space_pos != std::string_view::npos) {
    if (auto const parsed = parse_SellOrderType(args.substr(0, space_pos))) {
      order_type = *parsed;
      args = args.substr(space_pos + 1);
    }
  }

  auto const sell_order = parse_sell_order(args);
  if (!sell_order) {
    return "Failed to place sell order. Expected: 'sell [immediate|auction] <item_name> [<quantity>] <price>'. "
           "Default type is 'immediate' and default quantity is 1";
  }
  auto const & [item_name, quantity, price] = *sell_order;

  // expiration time is now + 5min
  constexpr auto const order_lifetime = std::chrono::minutes(5);
  int64_t const unix_expiration_time = (std::chrono::seconds(std::time(NULL)) + order_lifetime).count();

  auto result = connection.shared_state->storage.place_sell_order(order_type, connection.user.id, item_name, quantity,
                                                                  price, unix_expiration_time);
  if (!result) {
    return fmt::format("Failed to place {} sell order for {} {}(s) with error: {}", order_type, quantity, item_name,
                       result.error());
  }
  connection.shared_state->transaction_log.log(
      connection.user.id, fmt::format("payed fee .item_id={} .quantity={}", result->item_id, quantity));
  return fmt::format("Successfully placed {} sell order for {} {}(s)", order_type, quantity, item_name);
}

std::string buy(UserConnection & connection, std::string_view args) {
  std::optional<int> bid;
  std::size_t const space_pos = args.find(' ');
  if (space_pos != std::string_view::npos) {
    std::string_view const bid_str = args.substr(space_pos + 1);
    int parsed = 0;
    auto const [_, ec] = std::from_chars(bid_str.data(), bid_str.data() + bid_str.size(), parsed);
    if (ec == std::errc()) {
      bid = parsed;
      args = args.substr(0, space_pos);
    }
  }

  int sell_order_id = 1;
  auto const [_, ec] = std::from_chars(args.data(), args.data() + args.size(), sell_order_id);
  if (ec != std::errc()) {
    return "Failed to buy. Expected: 'buy <sell_order_id> [<bid>]'";
  }

  if (bid) {
    auto result =
        connection.shared_state->storage.place_bid_on_auction_sell_order(connection.user.id, sell_order_id, *bid);
    if (!result) {
      return fmt::format("Failed to place a bid on #{} auction sell order with error: {}", sell_order_id,
                         result.error());
    }
    return fmt::format("Successfully placed a bid on #{} auction sell order", sell_order_id);
  } else {
    auto result = connection.shared_state->storage.execute_immediate_sell_order(connection.user.id, sell_order_id);
    if (!result) {
      return fmt::format("Failed to execute #{} sell order with error: {}", sell_order_id, result.error());
    }
    result->save_to(connection.shared_state->transaction_log);

    connection.shared_state->notifications.push(std::make_pair(
        result->seller_id, fmt::format("Your sell order #{} was executed for {}", result->id, result->price)));

    return fmt::format("Successfully executed #{} sell order", sell_order_id);
  }
}

std::string view_sell_orders(UserConnection & connection, std::string_view) {
  auto result = connection.shared_state->storage.view_sell_orders();
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

std::string process_request(UserConnection & connection, std::string_view request) {
  auto [command, args] = parse_command(request);
  if (auto const it = kAllCommands.find(command); it != kAllCommands.end()) {
    return it->second.invoke(connection, args);
  }
  auto help_str = commands::help(connection, {});
  return fmt::format("Failed to execute unknown command '{}'. {}", command, help_str);
}
