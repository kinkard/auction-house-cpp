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
      ");");
  if (!result) {
    return tl::make_unexpected(fmt::format("Failed to create 'users' table: {}", result.error()));
  }

  result = db->execute(
      "CREATE TABLE IF NOT EXISTS items ("
      "id INTEGER PRIMARY KEY,"
      "name TEXT NOT NULL UNIQUE"
      ");");
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
      "quantity INTEGER NOT NULL DEFAULT 1,"
      "FOREIGN KEY (user_id) REFERENCES users (id),"
      "FOREIGN KEY (item_id) REFERENCES items (id),"
      "PRIMARY KEY (user_id, item_id)"
      ");");
  if (!result) {
    return tl::make_unexpected(fmt::format("Failed to create 'user_items' table: {}", result.error()));
  }

  result = db->execute(
      "CREATE TABLE IF NOT EXISTS sell_orders ("
      "id INTEGER PRIMARY KEY,"
      "seller_id INTEGER NOT NULL,"
      "item_id INTEGER NOT NULL,"
      "quantity INTEGER NOT NULL,"
      "price INTEGER NOT NULL,"
      "expiration_time TEXT NOT NULL,"
      // buyer_id is special
      // - equal to the seller_id for immediate orders
      // - NULL for aution orders without bid
      // - Not NULL and not equal to the seller_id for auction orders with bid
      // so you can't buy your own items
      "buyer_id INTEGER,"
      "FOREIGN KEY (seller_id) REFERENCES users (id),"
      "FOREIGN KEY (buyer_id) REFERENCES users (id),"
      "FOREIGN KEY (item_id) REFERENCES items (id)"
      ");");
  if (!result) {
    return tl::make_unexpected(fmt::format("Failed to create 'sell_orders' table: {}", result.error()));
  }

  return Storage(std::move(*db), *funds_item_id);
}

tl::expected<User, std::string> Storage::get_or_create_user(std::string_view username) {
  // We always do INSERT to make sure that the user exists
  auto user_inserted = this->db.execute("INSERT INTO users (username) VALUES (?1);", username);

  auto user_id =
      this->db.query("SELECT id FROM users WHERE username = ?1;", username)
          .and_then([&](auto select) -> tl::expected<User, std::string> {
            int rc = sqlite3_step(select.inner);
            if (rc != SQLITE_ROW) {
              throw std::runtime_error(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
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

tl::expected<void, std::string> Storage::deposit(UserId user_id, std::string_view item_name, int quantity) {
  if (quantity < 0) {
    return tl::make_unexpected("Cannot deposit negative amount");
  }

  if (!this->is_valid_user(user_id)) {
    return tl::make_unexpected(fmt::format("User with id={} doesn't exist", user_id));
  }

  // We do INSERT OR IGNORE to make sure that the item exists
  auto item_inserted = this->db.execute("INSERT OR IGNORE INTO items (name) VALUES (?1);", item_name);
  if (!item_inserted) {
    return tl::make_unexpected(item_inserted.error());
  }

  auto const item_id = get_item_id(item_name);
  if (!item_id) {
    return tl::make_unexpected("Failed to deposit item. Item doesn't exists");
  }

  return this->db.execute(
      "INSERT INTO user_items (user_id, item_id, quantity) VALUES (?1, ?2, ?3) "
      "ON CONFLICT (user_id, item_id) DO UPDATE SET quantity = quantity + ?3;",
      user_id, *item_id, quantity);
}

tl::expected<void, std::string> Storage::withdraw(UserId user_id, std::string_view item_name, int quantity) {
  if (quantity < 0) {
    return tl::make_unexpected("Cannot withdraw negative amount");
  }

  if (!this->is_valid_user(user_id)) {
    return tl::make_unexpected(fmt::format("User with id={} doesn't exist", user_id));
  }

  auto const item_id = get_item_id(item_name);
  if (!item_id) {
    return tl::make_unexpected("Failed to withdraw item. Item doesn't exists");
  }

  // if user doesn't have enough items then it cannot be withdrawn
  auto select_user_item_count =
      this->db.query("SELECT quantity FROM user_items WHERE user_id = ?1 AND item_id = ?2;", user_id, *item_id)
          .and_then([&](auto select) -> tl::expected<int, std::string> {
            int rc = sqlite3_step(select.inner);
            if (rc != SQLITE_ROW) {
              return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
            }
            return sqlite3_column_int(select.inner, 0);
          });
  if (!select_user_item_count) {
    return tl::make_unexpected(select_user_item_count.error());
  }
  if (select_user_item_count.value() < quantity) {
    return tl::make_unexpected(
        fmt::format("User doesn't have enough items to withdraw: {}", select_user_item_count.value()));
  }

  return this->db.execute("UPDATE user_items SET quantity = ?3 WHERE user_id = ?1 AND item_id = ?2;", user_id, *item_id,
                          quantity);
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

tl::expected<void, std::string> Storage::place_sell_order(SellOrderType order_type, UserId seller_id,
                                                          std::string_view item_name, int quantity, int price,
                                                          std::string_view expiration_time) {
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

  auto const item_id = get_item_id(item_name);
  if (!item_id) {
    return tl::make_unexpected("Failed to sell item. Item doesn't exists");
  }

  auto items_quantity =
      db.query(
            "SELECT quantity FROM user_items "
            "WHERE user_id = ?1 AND item_id = ?2;",
            seller_id, *item_id)
          .and_then([&](auto select) -> tl::expected<int, std::string> {
            int rc = sqlite3_step(select.inner);
            if (rc != SQLITE_ROW) {
              return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
            }
            return sqlite3_column_int(select.inner, 0);
          });
  if (!items_quantity) {
    return tl::make_unexpected(fmt::format("Failed to get items for user {}: {}", seller_id, items_quantity.error()));
  }
  if (*items_quantity < quantity) {
    return tl::make_unexpected(fmt::format("User doesn't have enough {}(s) to sell", item_name));
  }

  auto begin_result = db.execute("BEGIN TRANSACTION;");
  if (!begin_result) {
    return tl::make_unexpected(fmt::format("Failed to start transaction: {}", begin_result.error()));
  }

  // first, reduce the user's items and remove the whole row if quantity is 0
  tl::expected<void, std::string> reduce_result;
  if (*items_quantity > quantity) {
    reduce_result = db.execute("UPDATE user_items SET quantity = quantity - ?3 WHERE user_id = ?1 AND item_id = ?2;",
                               seller_id, *item_id, quantity);
  } else {
    reduce_result = db.execute("DELETE FROM user_items WHERE user_id = ?1 AND item_id = ?2;", seller_id, *item_id);
  }
  if (!reduce_result) {
    db.execute("ROLLBACK;");
    return tl::make_unexpected(fmt::format("Failed to reduce user items: {}", reduce_result.error()));
  }

  // Second, insert a new sell order.
  // As mentioned earlier, buyer_id is special stores an information about the order type and state.
  // For immediate orders, buyer_id is equal to the seller_id.
  // For auction orders, buyer_id is null untill someone places a bid.
  std::optional<int> buyer_id;
  if (order_type == SellOrderType::Immediate) {
    buyer_id = seller_id;
  }

  auto result = db.execute(
      "INSERT INTO sell_orders (seller_id, item_id, quantity, price, expiration_time, buyer_id)"
      "VALUES (?1, ?2, ?3, ?4, ?5, ?6);",
      seller_id, *item_id, quantity, price, expiration_time, buyer_id);
  if (!result) {
    db.execute("ROLLBACK;");
    return tl::make_unexpected(fmt::format("Failed to place sell order: {}", result.error()));
  }

  auto commit_result = db.execute("COMMIT;");
  if (!commit_result) {
    return tl::make_unexpected(fmt::format("Failed to commit transaction: {}", commit_result.error()));
  }

  return {};
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
          "  sell_orders.expiration_time,"
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

tl::expected<void, std::string> Storage::cancel_expired_sell_orders(std::string_view now) {
  // Start transaction
  auto begin_result = db.execute("BEGIN TRANSACTION;");
  if (!begin_result) {
    return tl::make_unexpected(fmt::format("Failed to start transaction: {}", begin_result.error()));
  }

  // Combine similar (by user_id and item_id) orders and add them to user_items
  auto update_result = db.execute(
      // Aggregate orders that sells the same item to the same user
      "WITH aggregated_orders AS ("
      "  SELECT seller_id, item_id, SUM(quantity) as total_quantity "
      "  FROM sell_orders "
      "  WHERE sell_orders.expiration_time <= ?1 "
      "  GROUP BY seller_id, item_id "
      ") "
      "INSERT OR REPLACE INTO user_items (user_id, item_id, quantity) "
      // Combine aggregated orders with user_items
      "SELECT "
      "  aggregated_orders.seller_id, "
      "  aggregated_orders.item_id, "
      "  IFNULL(user_items.quantity, 0) + aggregated_orders.total_quantity "
      "FROM aggregated_orders "
      "LEFT JOIN user_items ON user_items.user_id = aggregated_orders.seller_id "
      "  AND user_items.item_id = aggregated_orders.item_id;",
      now);
  if (!update_result) {
    db.execute("ROLLBACK;");
    return tl::make_unexpected(fmt::format("Failed to cancel expired sell orders: {}", update_result.error()));
  }

  // Delete expired orders
  auto delete_result = db.execute("DELETE FROM sell_orders WHERE expiration_time <= ?1;", now);
  if (!delete_result) {
    db.execute("ROLLBACK;");
    return tl::make_unexpected(fmt::format("Failed to delete expired sell orders: {}", delete_result.error()));
  }

  return db.execute("COMMIT;");
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

std::optional<int> Storage::get_item_id(std::string_view item_name) {
  auto stmt = this->db.query("SELECT id FROM items WHERE name = ?1;", item_name);
  if (!stmt) {
    return std::nullopt;
  }
  int rc = sqlite3_step(stmt->inner);
  if (rc != SQLITE_ROW) {
    return std::nullopt;
  }
  return { sqlite3_column_int(stmt->inner, 0) };
}
