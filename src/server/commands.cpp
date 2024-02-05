#include "commands.hpp"
#include "shared_state.hpp"

#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <charconv>
#include <chrono>
#include <optional>
#include <string>

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
struct formatter<SellOrderInfo> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext & ctx) {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const SellOrderInfo & order, FormatContext & ctx) const {
    std::string_view const order_type_str = order.type == SellOrderType::Auction ? "on auction " : "";

    if (order.quantity == 1) {
      return ::fmt::format_to(ctx.out(), "#{}: {} is selling a {} for {} funds {}until {}", order.id, order.seller_name,
                              order.item_name, order.price, order_type_str, order.expiration_time);
    } else {
      return ::fmt::format_to(ctx.out(), "#{}: {} is selling {} {}(s) for {} funds {}until {}", order.id,
                              order.seller_name, order.quantity, order.item_name, order.price, order_type_str,
                              order.expiration_time);
    }
  }
};
}  // namespace fmt

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

constexpr std::string_view kHelpString = R"(Available commands:
- whoami: Displays the username of the current user
- ping: Replies 'pong'
- help: Prints this help message about all available commands
- quit: Ask the server to close the connection. Alternatively, the client can just close the connection (e.g. Ctrl+C)

- deposit: Deposits a specified amount into the user's account. Format: 'deposit <item name> [<quantity>]'.
  'fund' is a special item name that can be used to deposit funds into the user's account
  Example: 'deposit funds 100' - deposits 100 funds, 'deposit Sword' - deposits 1 Sword
- withdraw: Withdraws a specified amount from the user's account. Format: 'withdraw <item name> [<quantity>]'
  Example: 'withdraw arrow 5' - withdraws 5 arrows, 'withdraw Sword' - withdraws 1 Sword
- view_items: Displays a list items for the current user

- view_sell_orders: Displays a list of all sell orders from all users
- sell: Places an item for sale at a specified price. Format: 'sell [immediate|auction] <item_name> [<quantity>] <price>'
  - immediate sell order - will be executed immediately once someone buys it. Otherwise it will expire in 5 minutes
    and items will be returned to the seller, but not the fee, which is `5% of the price + 1` funds
  - auction sell order - will be executed once it expires if someone placed a bid on it
- buy: Executes immediate sell order or places a bid on a auction sell order. Format: 'buy <sell_order_id> [<bid>]'
  - no bid - executes immediate sell order
  - bid - places a bid on a auction sell order
  
Usage: <command> [<args>], where `[]` annotates optional argumet(s))";
}  // namespace

namespace commands {

std::string Help::execute(User const &, std::shared_ptr<SharedState> const &) {
  return std::string(kHelpString);
}

std::optional<Deposit> Deposit::parse(std::string_view args) {
  auto const [item_name, quantity] = parse_item_name_and_count(args);
  return Deposit{ .item_name = item_name, .quantity = quantity };
}

std::string Deposit::execute(User const & user, std::shared_ptr<SharedState> const & shared_state) {
  auto result = shared_state->auction_service.deposit(user.id, item_name, quantity);
  if (!result) {
    return fmt::format("Failed to deposit {} {}(s) with error: {}", quantity, item_name, result.error());
  }

  shared_state->transaction_log.save(user.id, "deposited", *result);
  return fmt::format("Successfully deposited {} {}(s)", quantity, item_name);
}

std::optional<Withdraw> Withdraw::parse(std::string_view args) {
  auto const [item_name, quantity] = parse_item_name_and_count(args);
  return Withdraw{ .item_name = item_name, .quantity = quantity };
}

std::string Withdraw::execute(User const & user, std::shared_ptr<SharedState> const & shared_state) {
  auto result = shared_state->auction_service.withdraw(user.id, item_name, quantity);
  if (!result) {
    return fmt::format("Failed to withdraw {} {}(s) with error: {}", quantity, item_name, result.error());
  }

  shared_state->transaction_log.save(user.id, "withdrawn", *result);
  return fmt::format("Successfully withdrawn {} {}(s)", quantity, item_name);
}

std::string ViewItems::execute(User const & user, std::shared_ptr<SharedState> const & shared_state) {
  auto result = shared_state->storage->view_user_items(user.id);
  if (!result) {
    return fmt::format("Failed to view items with error: {}", result.error());
  }
  return fmt::format("Items: [{}]", fmt::join(result.value(), ", "));
}

std::optional<Sell> Sell::parse(std::string_view args) {
  // parse optional order type, if none - use immediate
  SellOrderType order_type = SellOrderType::Immediate;
  std::size_t space_pos = args.find(' ');
  if (space_pos != std::string_view::npos) {
    if (auto const parsed = parse_SellOrderType(args.substr(0, space_pos))) {
      order_type = *parsed;
      args = args.substr(space_pos + 1);
    }
  }

  // then, find the price - it should be the last word
  space_pos = args.rfind(' ');
  if (space_pos == std::string_view::npos) {
    return std::nullopt;
  }
  std::string_view const price_str = args.substr(space_pos + 1);
  int price = 0;
  auto const [_, ec] = std::from_chars(price_str.data(), price_str.data() + price_str.size(), price);
  if (ec != std::errc()) {
    return std::nullopt;
  }
  args = args.substr(0, space_pos);

  auto const [item_name, quantity] = parse_item_name_and_count(args);

  return Sell{ .order_type = order_type, .item_name = item_name, .quantity = quantity, .price = price };
}

std::string Sell::execute(User const & user, std::shared_ptr<SharedState> const & shared_state) {
  // expiration time is now + 5min
  constexpr auto const order_lifetime = std::chrono::minutes(5);
  int64_t const unix_expiration_time = (std::chrono::seconds(std::time(NULL)) + order_lifetime).count();

  auto result = shared_state->auction_service.place_sell_order(order_type, user.id, item_name, quantity, price,
                                                               unix_expiration_time);
  if (!result) {
    return fmt::format("Failed to place {} sell order for {} {}(s) with error: {}", order_type, quantity, item_name,
                       result.error());
  }

  shared_state->transaction_log.save(user.id, "payed fee", *result);
  return fmt::format("Successfully placed {} sell order for {} {}(s)", order_type, quantity, item_name);
}

std::optional<Buy> Buy::parse(std::string_view args) {
  std::optional<int> bid;
  std::size_t const space_pos = args.find(' ');
  if (space_pos != std::string_view::npos) {
    std::string_view const bid_str = args.substr(space_pos + 1);
    int parsed = 0;
    auto const [_, ec] = std::from_chars(bid_str.data(), bid_str.data() + bid_str.size(), parsed);
    if (ec != std::errc()) {
      return std::nullopt;
    }

    bid = parsed;
    args = args.substr(0, space_pos);
  }

  int sell_order_id = 1;
  auto const [_, ec] = std::from_chars(args.data(), args.data() + args.size(), sell_order_id);
  if (ec != std::errc()) {
    return std::nullopt;
  }
  return Buy{ .sell_order_id = sell_order_id, .bid = bid };
}

std::string Buy::execute(User const & user, std::shared_ptr<SharedState> const & shared_state) {
  if (bid) {
    auto result = shared_state->auction_service.place_bid_on_auction_sell_order(user.id, sell_order_id, *bid);
    if (!result) {
      return fmt::format("Failed to place a bid on #{} auction sell order with error: {}", sell_order_id,
                         result.error());
    }
    return fmt::format("Successfully placed a bid on #{} auction sell order", sell_order_id);
  } else {
    auto result = shared_state->auction_service.execute_immediate_sell_order(user.id, sell_order_id);
    if (!result) {
      return fmt::format("Failed to execute #{} sell order with error: {}", sell_order_id, result.error());
    }
    shared_state->transaction_log.save(*result);
    shared_state->notifications.push(result->seller_id,
                                     ExecutedSellOrder{ .order_id = result->id, .price = result->price });

    return fmt::format("Successfully executed #{} sell order", sell_order_id);
  }
}

std::string ViewSellOrders::execute(User const &, std::shared_ptr<SharedState> const & shared_state) {
  auto result = shared_state->storage->view_sell_orders();
  if (!result) {
    return fmt::format("Failed to view sell orders with error: {}", result.error());
  }
  std::string output = "Sell orders:\n";
  for (const auto & item : result.value()) {
    output += fmt::format("- {}\n", item);
  }
  return output;
}

std::string Quit::execute(User const &, std::shared_ptr<SharedState> const &) {
  // throw an exception to close the connection with the client
  throw std::runtime_error("Quit command received");
}

}  // namespace commands
