#include "auction_service.hpp"

#include "storage.hpp"

int AuctionService::sell_order_fee(int price) const {
  // Fee is 5% of the price + 1 fixed fee
  return price / 20 + 1;
}

tl::expected<ItemOperationInfo, std::string> AuctionService::deposit(UserId user_id, std::string_view item_name,
                                                                     int quantity) {
  if (quantity < 0) {
    return tl::make_unexpected("Cannot deposit negative amount");
  }

  return storage->get_item_id(item_name)
      .or_else([&](auto &&) { return storage->create_item(item_name); })
      .and_then([&](int item_id) {
        return storage->add_user_item(user_id, item_id, quantity).map([&]() {
          return ItemOperationInfo{ .item_id = item_id, .quantity = quantity };
        });
      });
}

tl::expected<ItemOperationInfo, std::string> AuctionService::withdraw(UserId user_id, std::string_view item_name,
                                                                      int quantity) {
  if (quantity < 0) {
    return tl::make_unexpected("Cannot withdraw negative amount");
  }

  return storage->get_item_id(item_name)
      .and_then([&](int item_id) {
        return storage->sub_user_item(user_id, item_id, quantity).map([&]() {
          return ItemOperationInfo{ .item_id = item_id, .quantity = quantity };
        });
      })
      .map_error([&](auto &&) { return fmt::format("Not enough {}(s) to withdraw", item_name); });
}

tl::expected<ItemOperationInfo, std::string> AuctionService::place_sell_order(SellOrderType order_type,
                                                                              UserId seller_id,
                                                                              std::string_view item_name, int quantity,
                                                                              int price, int64_t unix_expiration_time) {
  if (quantity < 0) {
    return tl::make_unexpected("Cannot sell negative amount");
  }
  if (price < 0) {
    return tl::make_unexpected("Cannot sell for negative price");
  }
  if (item_name == storage->funds_item_name()) {
    return tl::make_unexpected(fmt::format("Cannot sell {0} for {0}, it's a speculation!", storage->funds_item_name()));
  }

  // As mentioned earlier, buyer_id is special stores an information about the order type and state.
  // For immediate orders, buyer_id is equal to the seller_id.
  // For auction orders, buyer_id is null untill someone places a bid.
  std::optional<int> buyer_id;
  if (order_type == SellOrderType::Immediate) {
    buyer_id = seller_id;
  }

  auto transaction_guard = storage->begin_transaction();
  if (!transaction_guard) {
    return tl::make_unexpected(fmt::format("Failed to start transaction: {}", transaction_guard.error()));
  }

  int const fee = sell_order_fee(price);

  return storage
      ->get_item_id(item_name)
      // First, take items from the seller
      .and_then([&](int item_id) {
        return storage->sub_user_item(seller_id, item_id, quantity).map([&]() { return item_id; });
      })
      .map_error([&](auto &&) { return fmt::format("Not enough {}(s) to sell", item_name); })

      // Then we want to take a fee from the seller
      .and_then([&](int item_id) {
        return storage->sub_user_item(seller_id, storage->funds_item_id(), fee)
            .map([&]() { return item_id; })
            .map_error(
                [&](auto &&) { return fmt::format("Not enough funds to pay {} funds fee (which is 5% + 1)", fee); });
      })

      // Second, insert the order
      .and_then([&](int item_id) {
        return storage->create_sell_order(Storage::SellOrder{
            .seller_id = seller_id,
            .item_id = item_id,
            .quantity = quantity,
            .price = price,
            .unix_expiration_time = unix_expiration_time,
            .buyer_id = buyer_id,
        });
      })
      .and_then([&]() { return transaction_guard->commit(); })
      .map([&]() { return ItemOperationInfo{ .item_id = storage->funds_item_id(), .quantity = fee }; });
}

tl::expected<SellOrderExecutionInfo, std::string> AuctionService::execute_immediate_sell_order(UserId buyer_id,
                                                                                               int sell_order_id) {
  auto order = storage->get_sell_order_info(sell_order_id);
  if (!order) {
    return tl::make_unexpected(fmt::format("Immediate sell order #{} doesn't exist", sell_order_id));
  }
  if (order->type() != SellOrderType::Immediate) {
    return tl::make_unexpected(fmt::format("Sell order #{} is not an immediate sell order", sell_order_id));
  }
  if (buyer_id == order->seller_id) {
    return tl::make_unexpected(fmt::format("You can't buy your own items"));
  }

  auto order_execution_info = SellOrderExecutionInfo{
    .id = sell_order_id,
    .seller_id = order->seller_id,
    .buyer_id = buyer_id,
    .item_id = order->item_id,
    .quantity = order->quantity,
    .price = order->price,
  };

  // Now we should transfer funds, items and delete the order
  auto transaction_guard = storage->begin_transaction();
  if (!transaction_guard) {
    return tl::make_unexpected(fmt::format("Failed to start transaction: {}", transaction_guard.error()));
  }

  // First, deduce funds from the buyer
  return storage->sub_user_item(buyer_id, storage->funds_item_id(), order->price)
      .map_error([&](auto &&) { return fmt::format("Not enough funds to buy"); })
      // Second, add funds to the seller
      .and_then([&]() { return storage->add_user_item(order->seller_id, storage->funds_item_id(), order->price); })
      // Third, transfer item to the buyer
      .and_then([&]() { return storage->add_user_item(buyer_id, order->item_id, order->quantity); })
      // Finally, delete the order
      .and_then([&]() { return storage->delete_sell_order(sell_order_id); })
      // And of course, commit the transaction
      .and_then([&]() { return transaction_guard->commit(); })
      // And return the execution info
      .map([&]() { return order_execution_info; });
}

tl::expected<void, std::string> AuctionService::place_bid_on_auction_sell_order(UserId buyer_id, int sell_order_id,
                                                                                int bid) {
  auto order = storage->get_sell_order_info(sell_order_id);
  if (!order) {
    return tl::make_unexpected(fmt::format("Sell order #{} doesn't exist", sell_order_id));
  }
  if (order->type() != SellOrderType::Auction) {
    return tl::make_unexpected(fmt::format("Sell order #{} is not an auction sell order", sell_order_id));
  }
  if (buyer_id == order->seller_id) {
    return tl::make_unexpected("You cannot bid on your own auction orders");
  }
  if (bid <= order->price) {
    return tl::make_unexpected("Bid must be greater than the current price");
  }

  auto transaction_guard = storage->begin_transaction();
  if (!transaction_guard) {
    return tl::make_unexpected(fmt::format("Failed to start transaction: {}", transaction_guard.error()));
  }

  if (order->buyer_id) {
    // If there is already a bid, then we should return funds to the previous buyer
    auto return_funds_result = storage->add_user_item(*order->buyer_id, storage->funds_item_id(), order->price);
    if (!return_funds_result) {
      return tl::make_unexpected(
          fmt::format("Failed to return funds to the previous buyer: {}", return_funds_result.error()));
    }
  }

  // First, deduce funds from the buyer
  return storage->sub_user_item(buyer_id, storage->funds_item_id(), bid)
      .map_error([&](auto &&) { return fmt::format("Not enough funds to buy"); })
      // Second, update order price and buyer_id
      .and_then([&]() { return storage->update_sell_order_buyer(sell_order_id, buyer_id, bid); })
      // And of course, commit the transaction
      .and_then([&]() { return transaction_guard->commit(); });
}
