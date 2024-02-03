#pragma once

#include "types.hpp"

#include <tl/expected.hpp>

#include <memory>
#include <string>

class Storage;

class AuctionService final {
  std::shared_ptr<Storage> storage;

public:
  AuctionService(std::shared_ptr<Storage> storage) : storage(std::move(storage)) {}

  int sell_order_fee(int price) const;

  // Deposint item to the user. "funds" item is used to store the balance
  tl::expected<ItemOperationInfo, std::string> deposit(UserId user_id, std::string_view item_name, int quantity);

  // Withdraws item from the user. "funds" item is used to store the balance
  tl::expected<ItemOperationInfo, std::string> withdraw(UserId user_id, std::string_view item_name, int quantity);

  // Place a sell order
  tl::expected<ItemOperationInfo, std::string> place_sell_order(SellOrderType order_type, UserId user_id,
                                                                std::string_view item_name, int quantity, int price,
                                                                int64_t unix_expiration_time);

  // Execute a buy order
  tl::expected<SellOrderExecutionInfo, std::string> execute_immediate_sell_order(UserId buyer_id, int sell_order_id);

  // Place a bid on an auction sell order. The order will be executed when order expiration time is reached
  tl::expected<void, std::string> place_bid_on_auction_sell_order(UserId buyer_id, int sell_order_id, int bid);

  // Cancel expired sell orders
  tl::expected<std::vector<SellOrderExecutionInfo>, std::string> process_expired_sell_orders(int64_t unix_now);
};
