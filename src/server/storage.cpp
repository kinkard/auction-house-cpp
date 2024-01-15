#include "storage.hpp"

#include <sqlite3.h>

#include <fmt/format.h>

// Use magic of the C++ string literal concatenation to make the code more readable
#define FUNDS_ITEM_NAME "funds"

tl::expected<Storage, std::string> Storage::open(const char * path) {
  auto db = Sqlite3::open(path);
  if (!db) {
    return tl::make_unexpected(fmt::format("Failed to open database: {}", db.error()));
  }

  auto result = db->execute(
      "CREATE TABLE IF NOT EXISTS users ("
      "id INTEGER PRIMARY KEY,"
      "username TEXT NOT NULL UNIQUE"
      ") STRICT;");
  if (!result) {
    return tl::make_unexpected(fmt::format("Failed to create 'users' table: {}", result.error()));
  }

  result = db->execute(
      "CREATE TABLE IF NOT EXISTS items ("
      "id INTEGER PRIMARY KEY,"
      "name TEXT NOT NULL UNIQUE"
      ") STRICT;");
  if (!result) {
    return tl::make_unexpected(fmt::format("Failed to create 'items' table: {}", result.error()));
  }

  // If there is no item called "funds", create it
  result = db->execute("INSERT OR IGNORE INTO items (name) VALUES ('" FUNDS_ITEM_NAME "');");
  if (!result) {
    return tl::make_unexpected(fmt::format("Failed to insert '" FUNDS_ITEM_NAME "' item: {}", result.error()));
  }
  auto funds_item_id =
      db->query("SELECT id FROM items WHERE name = '" FUNDS_ITEM_NAME "';")
          .and_then([&](auto select) -> tl::expected<int, std::string> {
            int rc = sqlite3_step(select.inner);
            if (rc != SQLITE_ROW) {
              return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
            }
            return sqlite3_column_int(select.inner, 0);
          });
  if (!funds_item_id) {
    return tl::make_unexpected(fmt::format("Failed to get '" FUNDS_ITEM_NAME "' item id: {}", funds_item_id.error()));
  }

  result = db->execute(
      "CREATE TABLE IF NOT EXISTS user_items ("
      "user_id INTEGER NOT NULL,"
      "item_id INTEGER NOT NULL,"
      "quantity INTEGER NOT NULL CHECK(quantity >= 0),"
      "FOREIGN KEY (user_id) REFERENCES users (id),"
      "FOREIGN KEY (item_id) REFERENCES items (id),"
      "PRIMARY KEY (user_id, item_id)"
      ") STRICT;");
  if (!result) {
    return tl::make_unexpected(fmt::format("Failed to create 'user_items' table: {}", result.error()));
  }

  result = db->execute(
      "CREATE TABLE IF NOT EXISTS sell_orders ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "seller_id INTEGER NOT NULL,"
      "item_id INTEGER NOT NULL,"
      "quantity INTEGER NOT NULL CHECK(quantity > 0),"
      "price INTEGER NOT NULL CHECK(price > 0),"
      // Unix timestamp in seconds
      // Use SELECT DATETIME(1793956207, 'unixepoch'); to convert to human readable format
      // and std::chrono::seconds(std::time(NULL)); to get current time
      "expiration_time INTEGER NOT NULL,"
      // buyer_id is special
      // - equal to the seller_id for immediate orders
      // - NULL for aution orders without bid
      // - Not NULL and not equal to the seller_id for auction orders with bid
      // so you can't buy your own items
      "buyer_id INTEGER,"
      "FOREIGN KEY (seller_id) REFERENCES users (id),"
      "FOREIGN KEY (buyer_id) REFERENCES users (id),"
      "FOREIGN KEY (item_id) REFERENCES items (id)"
      ") STRICT;");
  if (!result) {
    return tl::make_unexpected(fmt::format("Failed to create 'sell_orders' table: {}", result.error()));
  }
  // Create an index to speed up cancel_expired_sell_orders()
  result = db->execute("CREATE INDEX IF NOT EXISTS sell_orders_expiration_time ON sell_orders (expiration_time);");
  if (!result) {
    return tl::make_unexpected(fmt::format("Failed to create 'sell_orders_expiration_time' index: {}", result.error()));
  }

  return Storage(std::move(*db), *funds_item_id);
}

tl::expected<User, std::string> Storage::get_or_create_user(std::string_view username) {
  if (username.empty()) {
    return tl::make_unexpected("Username cannot be empty");
  }

  // We always do INSERT to make sure that the user exists
  auto user_inserted = this->db.execute("INSERT INTO users (username) VALUES (?1);", username);

  auto user_id =
      this->db.query("SELECT id FROM users WHERE username = ?1;", username)
          .and_then([&](auto select) -> tl::expected<User, std::string> {
            int rc = sqlite3_step(select.inner);
            if (rc != SQLITE_ROW) {
              return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
            }

            int user_id = sqlite3_column_int(select.inner, 0);
            return User{ .id = user_id, .username = std::string(username) };
          });
  if (!user_id) {
    return tl::make_unexpected(user_id.error());
  }

  // If user just created, then it has 0 funds
  if (user_inserted) {
    auto funds_added = this->db.execute("INSERT INTO user_items (user_id, item_id, quantity) VALUES (?1, ?2, 0);",
                                        user_id.value().id, this->funds_item_id);
    if (!funds_added) {
      return tl::make_unexpected(funds_added.error());
    }
  }
  return user_id;
}

tl::expected<ItemOperationInfo, std::string> Storage::deposit(UserId user_id, std::string_view item_name,
                                                              int quantity) {
  if (quantity < 0) {
    return tl::make_unexpected("Cannot deposit negative amount");
  }

  if (!this->is_valid_user(user_id)) {
    return tl::make_unexpected(fmt::format("No such user with id={}", user_id));
  }

  // We do INSERT OR IGNORE to make sure that the item exists
  auto item_inserted = this->db.execute("INSERT OR IGNORE INTO items (name) VALUES (?1);", item_name);
  if (!item_inserted) {
    return tl::make_unexpected(item_inserted.error());
  }

  return get_item_id(item_name).and_then([&](int item_id) {
    return deposit_inner(user_id, item_id, quantity).map([&]() {
      return ItemOperationInfo{ .item_id = item_id, quantity };
    });
  });
}

tl::expected<ItemOperationInfo, std::string> Storage::withdraw(UserId user_id, std::string_view item_name,
                                                               int quantity) {
  if (quantity < 0) {
    return tl::make_unexpected("Cannot withdraw negative amount");
  }

  if (!this->is_valid_user(user_id)) {
    return tl::make_unexpected(fmt::format("No such user with id={}", user_id));
  }

  return get_item_id(item_name)
      .and_then([&](int item_id) {
        return withdraw_inner(user_id, item_id, quantity).map([&]() {
          return ItemOperationInfo{ .item_id = item_id, quantity };
        });
      })
      .map_error([&](auto &&) { return fmt::format("Not enough {}(s) to withdraw", item_name); });
}

tl::expected<std::vector<std::pair<std::string, int>>, std::string> Storage::view_items(UserId user_id) {
  return this->db
      .query(
          "SELECT items.name, user_items.quantity FROM user_items "
          "INNER JOIN items ON user_items.item_id = items.id "
          "WHERE user_items.user_id = ?1;",
          user_id)
      .and_then([&](auto select) -> tl::expected<std::vector<std::pair<std::string, int>>, std::string> {
        std::vector<std::pair<std::string, int>> items;
        int rc;
        while ((rc = sqlite3_step(select.inner)) == SQLITE_ROW) {
          std::string item_name = reinterpret_cast<char const *>(sqlite3_column_text(select.inner, 0));
          int quantity = sqlite3_column_int(select.inner, 1);
          items.emplace_back(std::move(item_name), quantity);
        }
        if (rc != SQLITE_DONE) {
          return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
        }
        return items;
      });
}

tl::expected<ItemOperationInfo, std::string> Storage::place_sell_order(SellOrderType order_type, UserId seller_id,
                                                                       std::string_view item_name, int quantity,
                                                                       int price, int64_t unix_expiration_time) {
  if (quantity < 0) {
    return tl::make_unexpected("Cannot sell negative amount");
  }
  if (price < 0) {
    return tl::make_unexpected("Cannot sell for negative price");
  }
  if (item_name == FUNDS_ITEM_NAME) {
    return tl::make_unexpected(
        fmt::format("Cannot sell " FUNDS_ITEM_NAME " for " FUNDS_ITEM_NAME ", it's a speculation!"));
  }

  // As mentioned earlier, buyer_id is special stores an information about the order type and state.
  // For immediate orders, buyer_id is equal to the seller_id.
  // For auction orders, buyer_id is null untill someone places a bid.
  std::optional<int> buyer_id;
  if (order_type == SellOrderType::Immediate) {
    buyer_id = seller_id;
  }

  // Fee is 5% of the price + 1 fixed fee
  int const fee = price / 20 + 1;

  auto transaction_guard = db.begin_transaction();
  if (!transaction_guard) {
    return tl::make_unexpected(fmt::format("Failed to start transaction: {}", transaction_guard.error()));
  }

  return get_item_id(item_name)
      // First, take items from the seller
      .and_then(
          [&](int item_id) { return withdraw_inner(seller_id, item_id, quantity).map([&]() { return item_id; }); })
      .map_error([&](auto &&) { return fmt::format("Not enough {}(s) to sell", item_name); })

      // Then we want to take a fee from the seller
      .and_then([&](int item_id) {
        return withdraw_inner(seller_id, funds_item_id, fee).map([&]() { return item_id; }).map_error([&](auto &&) {
          return fmt::format("Not enough funds to pay {} fee (which is 5% + 1)", item_name, fee);
        });
      })

      // Second, insert the order
      .and_then([&](int item_id) {
        return db.execute(
            "INSERT INTO sell_orders (seller_id, item_id, quantity, price, expiration_time, buyer_id)"
            "VALUES (?1, ?2, ?3, ?4, ?5, ?6);",
            seller_id, item_id, quantity, price, unix_expiration_time, buyer_id);
      })
      .and_then([&]() { return transaction_guard->commit(); })
      .map([&]() { return ItemOperationInfo{ .item_id = funds_item_id, .quantity = fee }; });
}

tl::expected<std::vector<SellOrder>, std::string> Storage::view_sell_orders() {
  return this->db
      .query(
          "SELECT"
          "  sell_orders.id,"
          "  users.username,"
          "  items.name,"
          "  sell_orders.quantity,"
          "  sell_orders.price,"
          "  DATETIME(sell_orders.expiration_time, 'unixepoch'),"
          "  sell_orders.seller_id,"
          "  sell_orders.buyer_id "
          "FROM sell_orders "
          "INNER JOIN users ON sell_orders.seller_id = users.id "
          "INNER JOIN items ON sell_orders.item_id = items.id;")
      .and_then([&](auto select) -> tl::expected<std::vector<SellOrder>, std::string> {
        std::vector<SellOrder> orders;
        int rc;
        while ((rc = sqlite3_step(select.inner)) == SQLITE_ROW) {
          int const id = sqlite3_column_int(select.inner, 0);
          std::string user_name = reinterpret_cast<char const *>(sqlite3_column_text(select.inner, 1));
          std::string item_name = reinterpret_cast<char const *>(sqlite3_column_text(select.inner, 2));
          int const quantity = sqlite3_column_int(select.inner, 3);
          int const price = sqlite3_column_int(select.inner, 4);
          std::string expiration_time = reinterpret_cast<char const *>(sqlite3_column_text(select.inner, 5));

          int const seller_id = sqlite3_column_int(select.inner, 6);
          int const buyer_id = sqlite3_column_int(select.inner, 7);
          SellOrderType const type = (seller_id == buyer_id) ? SellOrderType::Immediate : SellOrderType::Auction;

          orders.emplace_back(SellOrder{ .id = id,
                                         .seller_name = std::move(user_name),
                                         .item_name = std::move(item_name),
                                         .quantity = quantity,
                                         .price = price,
                                         .expiration_time = std::move(expiration_time),
                                         .type = type });
        }
        if (rc != SQLITE_DONE) {
          return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
        }
        return orders;
      });
}

tl::expected<std::vector<SellOrderExecutionInfo>, std::string> Storage::process_expired_sell_orders(int64_t unix_now) {
  // Start transaction
  auto transaction_guard = db.begin_transaction();
  if (!transaction_guard) {
    return tl::make_unexpected(fmt::format("Failed to start transaction: {}", transaction_guard.error()));
  }

  // get all executed auction orders
  auto executed_auction_orders =
      db.query(
            "SELECT "
            "  id, "
            "  seller_id, "
            "  buyer_id, "
            "  item_id, "
            "  quantity, "
            "  price "
            "FROM sell_orders "
            "WHERE sell_orders.expiration_time <= ?1 AND buyer_id IS NOT NULL AND buyer_id != seller_id;",
            unix_now)
          .and_then([&](auto select) -> tl::expected<std::vector<SellOrderExecutionInfo>, std::string> {
            std::vector<SellOrderExecutionInfo> orders;
            int rc;
            while ((rc = sqlite3_step(select.inner)) == SQLITE_ROW) {
              orders.emplace_back(SellOrderExecutionInfo{
                  .id = sqlite3_column_int(select.inner, 0),
                  .seller_id = sqlite3_column_int(select.inner, 1),
                  .buyer_id = sqlite3_column_int(select.inner, 2),
                  .item_id = sqlite3_column_int(select.inner, 3),
                  .quantity = sqlite3_column_int(select.inner, 4),
                  .price = sqlite3_column_int(select.inner, 5),
              });
            }
            if (rc != SQLITE_DONE) {
              return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
            }
            return orders;
          });

  // Combine similar (by user_id and item_id) orders and add them to user_items
  auto update_result = db.execute(
      // Aggregate orders that sells the same item to the same user
      "WITH aggregated_orders AS ("
      // this select combines items from all orders that are expired to the same user
      "  SELECT "
      // for immediate order and auction order without bid we return items to the seller
      // for auction order with bid we move items to the buyer
      "    CASE "
      "      WHEN buyer_id IS NULL OR buyer_id = seller_id THEN seller_id "
      "      ELSE buyer_id "
      "    END as user_id, "
      "    item_id, "
      "    SUM(quantity) as total_quantity "
      "  FROM sell_orders "
      "  WHERE sell_orders.expiration_time <= ?1 "
      "  GROUP BY user_id, item_id "
      // and for auction orders with bid we also add funds to the seller
      "  UNION ALL "
      "  SELECT "
      "    seller_id as user_id, "
      "    ?2 as item_id, "
      "    SUM(price) as total_quantity "
      "  FROM sell_orders "
      "  WHERE sell_orders.expiration_time <= ?1 AND buyer_id IS NOT NULL AND buyer_id != seller_id "
      "  GROUP BY seller_id "
      ") "
      "INSERT OR REPLACE INTO user_items (user_id, item_id, quantity) "
      // Combine aggregated orders with user_items
      "SELECT "
      "  aggregated_orders.user_id, "
      "  aggregated_orders.item_id, "
      "  IFNULL(user_items.quantity, 0) + aggregated_orders.total_quantity "
      "FROM aggregated_orders "
      "LEFT JOIN user_items ON user_items.user_id = aggregated_orders.user_id "
      "  AND user_items.item_id = aggregated_orders.item_id;",
      unix_now, funds_item_id);
  if (!update_result) {
    return tl::make_unexpected(fmt::format("Failed to cancel expired sell orders: {}", update_result.error()));
  }

  // Delete expired orders
  auto delete_result = db.execute("DELETE FROM sell_orders WHERE expiration_time <= ?1;", unix_now);
  if (!delete_result) {
    return tl::make_unexpected(fmt::format("Failed to delete expired sell orders: {}", delete_result.error()));
  }

  return transaction_guard->commit().and_then([executed = std::move(executed_auction_orders)]() { return executed; });
}

tl::expected<SellOrderExecutionInfo, std::string> Storage::execute_immediate_sell_order(UserId buyer_id,
                                                                                        int sell_order_id) {
  if (!this->is_valid_user(buyer_id)) {
    return tl::make_unexpected(fmt::format("No such user with id={}", buyer_id));
  }

  auto order = get_sell_order_info(sell_order_id);
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
  auto transaction_guard = db.begin_transaction();
  if (!transaction_guard) {
    return tl::make_unexpected(fmt::format("Failed to start transaction: {}", transaction_guard.error()));
  }

  // First, deduce funds from the buyer
  return withdraw_inner(buyer_id, funds_item_id, order->price)
      .map_error([&](auto &&) { return fmt::format("Not enough funds to buy"); })
      // Second, add funds to the seller
      .and_then([&]() { return deposit_inner(order->seller_id, funds_item_id, order->price); })
      // Third, transfer item to the buyer
      .and_then([&]() { return deposit_inner(buyer_id, order->item_id, order->quantity); })
      // Finally, delete the order
      .and_then([&]() { return db.execute("DELETE FROM sell_orders WHERE id = ?1;", sell_order_id); })
      // And of course, commit the transaction
      .and_then([&]() { return transaction_guard->commit(); })
      // And return the execution info
      .map([&]() { return order_execution_info; });
}

tl::expected<void, std::string> Storage::place_bid_on_auction_sell_order(UserId buyer_id, int sell_order_id, int bid) {
  if (!this->is_valid_user(buyer_id)) {
    return tl::make_unexpected(fmt::format("No such user with id={}", buyer_id));
  }

  auto order = get_sell_order_info(sell_order_id);
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

  auto transaction_guard = db.begin_transaction();
  if (!transaction_guard) {
    return tl::make_unexpected(fmt::format("Failed to start transaction: {}", transaction_guard.error()));
  }

  if (order->buyer_id) {
    // If there is already a bid, then we should return funds to the previous buyer
    auto return_funds_result = deposit_inner(*order->buyer_id, funds_item_id, order->price);
    if (!return_funds_result) {
      return tl::make_unexpected(
          fmt::format("Failed to return funds to the previous buyer: {}", return_funds_result.error()));
    }
  }

  // First, deduce funds from the buyer
  return withdraw_inner(buyer_id, funds_item_id, bid)
      .map_error([&](auto &&) { return fmt::format("Not enough funds to buy"); })
      // Second, update order price and buyer_id
      .and_then([&]() {
        return db.execute("UPDATE sell_orders SET price = ?1, buyer_id = ?2 WHERE id = ?3;", bid, buyer_id,
                          sell_order_id);
      })
      // And of course, commit the transaction
      .and_then([&]() { return transaction_guard->commit(); });
}

bool Storage::is_valid_user(UserId user_id) {
  return this->db.query("SELECT id FROM users WHERE id = ?1;", user_id)
      .and_then([](auto select) -> tl::expected<void, std::string> {
        int rc = sqlite3_step(select.inner);
        if (rc != SQLITE_ROW) {
          return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
        }
        return {};
      })
      .has_value();
}

tl::expected<int, std::string> Storage::get_item_id(std::string_view item_name) {
  return this->db.query("SELECT id FROM items WHERE name = ?1;", item_name)
      .and_then([&](auto select) -> tl::expected<int, std::string> {
        int rc = sqlite3_step(select.inner);
        if (rc != SQLITE_ROW) {
          return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
        }
        return sqlite3_column_int(select.inner, 0);
      });
}

std::optional<int> Storage::get_items_quantity(UserId user_id, int item_id) {
  auto stmt = this->db.query("SELECT quantity FROM user_items WHERE user_id = ?1 AND item_id = ?2;", user_id, item_id);
  if (!stmt) {
    return std::nullopt;
  }
  int rc = sqlite3_step(stmt->inner);
  if (rc != SQLITE_ROW) {
    return std::nullopt;
  }
  return { sqlite3_column_int(stmt->inner, 0) };
}

tl::expected<void, std::string> Storage::deposit_inner(UserId user_id, int item_id, int quantity) {
  return this->db.execute(
      "INSERT INTO user_items (user_id, item_id, quantity) VALUES (?1, ?2, ?3) "
      "ON CONFLICT (user_id, item_id) DO UPDATE SET quantity = quantity + ?3;",
      user_id, item_id, quantity);
}

tl::expected<void, std::string> Storage::withdraw_inner(UserId user_id, int item_id, int quantity) {
  auto const user_item_quantity = get_items_quantity(user_id, item_id);
  if (!user_item_quantity || *user_item_quantity < quantity) {
    return tl::make_unexpected(fmt::format("Failed to withdraw {} items.", quantity));
  }

  tl::expected<void, std::string> reduce_result;
  if (item_id == funds_item_id || user_item_quantity > quantity) {
    reduce_result = db.execute("UPDATE user_items SET quantity = quantity - ?3 WHERE user_id = ?1 AND item_id = ?2;",
                               user_id, item_id, quantity);
  } else {
    reduce_result = db.execute("DELETE FROM user_items WHERE user_id = ?1 AND item_id = ?2;", user_id, item_id);
  }
  return reduce_result;
}

std::optional<Storage::SellOrderInnerInfo> Storage::get_sell_order_info(int sell_order_id) {
  auto stmt = this->db.query(
      "SELECT"
      "  sell_orders.seller_id,"
      "  sell_orders.item_id,"
      "  sell_orders.quantity,"
      "  sell_orders.price,"
      "  sell_orders.buyer_id "
      "FROM sell_orders "
      "WHERE sell_orders.id = ?1;",
      sell_order_id);
  if (!stmt) {
    return std::nullopt;
  }
  int rc = sqlite3_step(stmt->inner);
  if (rc != SQLITE_ROW) {
    return std::nullopt;
  }

  std::optional<int> buyer_id;
  if (sqlite3_column_type(stmt->inner, 4) == SQLITE_INTEGER) {
    buyer_id = sqlite3_column_int(stmt->inner, 4);
  }

  return Storage::SellOrderInnerInfo{
    .seller_id = sqlite3_column_int(stmt->inner, 0),
    .item_id = sqlite3_column_int(stmt->inner, 1),
    .quantity = sqlite3_column_int(stmt->inner, 2),
    .price = sqlite3_column_int(stmt->inner, 3),
    .buyer_id = buyer_id,
  };
}
