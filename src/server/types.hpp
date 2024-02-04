#pragma once

#include <optional>
#include <string>

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

// A record for a transaction log
struct ItemOperationInfo {
  int item_id;
  int quantity;
};

// Internal struct that represents a sell order close to how it is stored in the database
struct SellOrderExecutionInfo {
  int id;
  UserId seller_id;
  UserId buyer_id;
  int item_id;
  int quantity;
  int price;
};

// A record for a sell order
struct SellOrderInfo {
  int id;
  std::string seller_name;
  std::string item_name;
  int quantity;
  int price;
  std::string expiration_time;
  SellOrderType type;
};
