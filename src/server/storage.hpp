#pragma once

#include "sqlite3.hpp"
#include "transaction_log.hpp"

#include <fmt/format.h>

using UserId = int;

struct User {
  UserId id;
  std::string username;
};

enum class SellOrderType : int {
  // Order will be immediately executed if there is a matching buy order
  Immediate = 1,
  // Order will be executed only after the auction is over
  Auction = 2,
};

inline std::string_view to_string(SellOrderType order_type) {
  switch (order_type) {
  case SellOrderType::Immediate: return "immediate";
  case SellOrderType::Auction: return "auction";
  }
  return "unknown";
}

inline std::optional<SellOrderType> parse_SellOrderType(std::string_view str) {
  if (str == "immediate") {
    return SellOrderType::Immediate;
  } else if (str == "auction") {
    return SellOrderType::Auction;
  }
  return std::nullopt;
}

struct SellOrder {
  int id;
  std::string seller_name;
  std::string item_name;
  int quantity;
  int price;
  std::string expiration_time;
  SellOrderType type;
};

struct SellOrderExecutionInfo {
  UserId seller_id;
  UserId buyer_id;
  int item_id;
  int quantity;
  int price;

  void save_to(TransactionLog & transaction_log) const {
    transaction_log.log(seller_id, fmt::format("sold .item_id={} .quantity={} .price={}", item_id, quantity, price));
    transaction_log.log(buyer_id, fmt::format("bought .item_id={} .quantity={} .price={}", item_id, quantity, price));
  }
};

struct ItemOperationInfo {
  int item_id;
  int quantity;
};

class Storage final {
  Sqlite3 db;
  int funds_item_id;

  // constructor is private, use `open` instead
  Storage(Sqlite3 && db, int funds_item_id) noexcept : db(std::move(db)), funds_item_id(funds_item_id) {}

public:
  tl::expected<Storage, std::string> static open(char const * path);
  ~Storage() = default;

  // This class cannot be copied, but can be moved
  Storage(Storage const &) = delete;
  Storage & operator=(Storage const &) = delete;
  Storage(Storage &&) = default;
  Storage & operator=(Storage &&) = default;

  // Returns user id of the already existing or newly created user
  tl::expected<User, std::string> get_or_create_user(std::string_view username);

  // Deposint item to the user. "funds" item is used to store the balance
  tl::expected<ItemOperationInfo, std::string> deposit(UserId user_id, std::string_view item_name, int quantity);

  // Withdraws item from the user. "funds" item is used to store the balance
  tl::expected<ItemOperationInfo, std::string> withdraw(UserId user_id, std::string_view item_name, int quantity);

  // List all user items
  tl::expected<std::vector<std::pair<std::string, int>>, std::string> view_items(UserId user_id);

  // Place a sell order
  tl::expected<void, std::string> place_sell_order(SellOrderType order_type, UserId user_id, std::string_view item_name,
                                                   int quantity, int price, int64_t unix_expiration_time);

  // View all sell orders
  tl::expected<std::vector<SellOrder>, std::string> view_sell_orders();

  // Cancel expired sell orders
  tl::expected<std::vector<SellOrderExecutionInfo>, std::string> process_expired_sell_orders(int64_t unix_now);

  // Execute a buy order
  tl::expected<SellOrderExecutionInfo, std::string> execute_immediate_sell_order(UserId buyer_id, int sell_order_id);

  // Place a bid on an auction sell order. The order will be executed when order expiration time is reached
  tl::expected<void, std::string> place_bid_on_auction_sell_order(UserId buyer_id, int sell_order_id, int bid);

private:
  bool is_valid_user(UserId user_id);
  tl::expected<int, std::string> get_item_id(std::string_view item_name);
  std::optional<int> get_items_quantity(UserId user_id, int item_id);

  tl::expected<void, std::string> deposit_inner(UserId user_id, int item_id, int quantity);
  tl::expected<void, std::string> withdraw_inner(UserId user_id, int item_id, int quantity);

  // Inner struct that represents a sell order
  struct SellOrderInnerInfo {
    int seller_id;
    int item_id;
    int quantity;
    int price;
    std::optional<int> buyer_id;

    SellOrderType type() const { return buyer_id == seller_id ? SellOrderType::Immediate : SellOrderType::Auction; }
  };
  std::optional<SellOrderInnerInfo> get_sell_order_info(int sell_order_id);
};
