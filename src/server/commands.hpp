#pragma once

#include "types.hpp"

#include <memory>
#include <string_view>

struct SharedState;

namespace commands {

// responsds with "pong"
struct Ping {
  static std::optional<Ping> parse(std::string_view) { return Ping{}; }
  std::string execute(User const &, std::shared_ptr<SharedState> const &) { return "pong"; }
};

// responds with username of the current user
struct Whoami {
  static std::optional<Whoami> parse(std::string_view) { return Whoami{}; }
  std::string execute(User const & user, std::shared_ptr<SharedState> const &) { return user.username; }
};

// prints help message with all available commands and their description
struct Help {
  static std::optional<Help> parse(std::string_view) { return Help{}; }
  std::string execute(User const &, std::shared_ptr<SharedState> const &);
};

// deposits an item with optional quantity
struct Deposit {
  std::string_view item_name;
  int quantity;

  static std::optional<Deposit> parse(std::string_view args);
  std::string execute(User const & user, std::shared_ptr<SharedState> const & shared_state);
};

// withdraws an item with optional quantity
struct Withdraw {
  std::string_view item_name;
  int quantity;

  static std::optional<Withdraw> parse(std::string_view args);
  std::string execute(User const & user, std::shared_ptr<SharedState> const & shared_state);
};

// lists all items in the inventory for the current user
struct ViewItems {
  static std::optional<ViewItems> parse(std::string_view) { return ViewItems{}; }
  std::string execute(User const & user, std::shared_ptr<SharedState> const & shared_state);
};

// places a sell order
struct Sell {
  SellOrderType order_type;
  std::string_view item_name;
  int quantity;
  int price;

  // args should be in the format "[immediate|auction] <item_name> [quantity] <price>".
  // Price is mandatory, quantity is optional and defaults to 1.
  // Examples:
  // - "arrow 5 10" -> {"arrow", .quantity=5, .price=10, .type=Immediate}
  // - "holy sword 1 100" -> {"holy sword", .quantity=1, .price=100, .type=Immediate}
  // - "arrow 10" -> {"arrow", .quantity=1, .price=10, .type=Immediate}
  // - "immidiate arrow 10 5" -> {"arrow", .quantity=10, .price=5, .type=Immediate}
  // - "auction arrow 10 5" -> {"arrow", .quantity=10, .price=5, .type=Auction}
  static std::optional<Sell> parse(std::string_view args);
  std::string execute(User const & user, std::shared_ptr<SharedState> const & shared_state);
};

// executes immediate sell order or places a bid on an auction sell order
struct Buy {
  int sell_order_id;
  std::optional<int> bid;

  static std::optional<Buy> parse(std::string_view args);
  std::string execute(User const & user, std::shared_ptr<SharedState> const & shared_state);
};

// lists all sell orders from all users
struct ViewSellOrders {
  static std::optional<ViewSellOrders> parse(std::string_view) { return ViewSellOrders{}; }
  std::string execute(User const &, std::shared_ptr<SharedState> const & shared_state);
};

struct Quit {
  static std::optional<Quit> parse(std::string_view) { return Quit{}; }
  std::string execute(User const &, std::shared_ptr<SharedState> const &);
};

}  // namespace commands
