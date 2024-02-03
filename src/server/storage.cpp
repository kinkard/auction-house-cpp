#include "storage.hpp"

#include <sqlite3.h>

#include <fmt/format.h>

tl::expected<Storage, std::string> Storage::open(std::string_view path) {
  // todo: ensure that there is a `\0` at the end of the string
  auto db = Sqlite3::open(path.data());
  if (!db) {
    return tl::make_unexpected(fmt::format("Failed to open database: {}", db.error()));
  }

  // Enable Write-Ahead Logging (WAL) mode for better performance and to enable concurrent reads and writes.
  // This pragma speeds up the database aprox 10x times.
  // https://www.sqlite.org/wal.html
  db->execute("PRAGMA journal_mode=WAL");

  // From SQLite documentation (https://www.sqlite.org/compile.html):
  // ```
  // For maximum database safety following a power loss, the setting of PRAGMA synchronous=FULL is
  // recommended. However, in WAL mode, complete database integrity is guaranteed with PRAGMA
  // synchronous=NORMAL. With PRAGMA synchronous=NORMAL in WAL mode, recent changes to the database
  // might be rolled back by a power loss, but the database will not be corrupted. Furthermore,
  // transaction commit is much faster in WAL mode using synchronous=NORMAL than with the default
  // synchronous=FULL. For these reasons, it is recommended that the synchronous setting be changed
  // from FULL to NORMAL when switching to WAL mode.
  //  ```
  // This pragma speeds up the database aprox 1.5x times on top of the gain from the WAL mode.
  // See https://www.sqlite.org/pragma.html#pragma_synchronous for more details
  db->execute("PRAGMA synchronous=NORMAL");

  auto result = db->execute(
      "CREATE TABLE IF NOT EXISTS users ("
      "id INTEGER PRIMARY KEY,"
      "username TEXT NOT NULL UNIQUE"
      ") STRICT");
  if (!result) {
    return tl::make_unexpected(fmt::format("Failed to create 'users' table: {}", result.error()));
  }

  result = db->execute(
      "CREATE TABLE IF NOT EXISTS items ("
      "id INTEGER PRIMARY KEY,"
      "name TEXT NOT NULL UNIQUE"
      ") STRICT");
  if (!result) {
    return tl::make_unexpected(fmt::format("Failed to create 'items' table: {}", result.error()));
  }

  // If there is no item called "funds", create it
  result = db->execute("INSERT OR IGNORE INTO items (name) VALUES (?1)", FUNDS_ITEM_NAME);
  if (!result) {
    return tl::make_unexpected(fmt::format("Failed to insert '{}' item: {}", FUNDS_ITEM_NAME, result.error()));
  }
  auto funds_item_id =
      db->query("SELECT id FROM items WHERE name = ?1", FUNDS_ITEM_NAME)
          .and_then([&](auto select) -> tl::expected<int, std::string> {
            int rc = sqlite3_step(select.inner);
            if (rc != SQLITE_ROW) {
              return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
            }
            return sqlite3_column_int(select.inner, 0);
          });
  if (!funds_item_id) {
    return tl::make_unexpected(fmt::format("Failed to get '{}' item id: {}", FUNDS_ITEM_NAME, funds_item_id.error()));
  }

  result = db->execute(
      "CREATE TABLE IF NOT EXISTS user_items ("
      "user_id INTEGER NOT NULL,"
      "item_id INTEGER NOT NULL,"
      "quantity INTEGER NOT NULL CHECK(quantity >= 0),"
      "FOREIGN KEY (user_id) REFERENCES users (id),"
      "FOREIGN KEY (item_id) REFERENCES items (id),"
      "PRIMARY KEY (user_id, item_id)"
      ") STRICT");
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
      ") STRICT");
  if (!result) {
    return tl::make_unexpected(fmt::format("Failed to create 'sell_orders' table: {}", result.error()));
  }
  // Create an index to speed up cancel_expired_sell_orders()
  result = db->execute("CREATE INDEX IF NOT EXISTS sell_orders_expiration_time ON sell_orders (expiration_time)");
  if (!result) {
    return tl::make_unexpected(fmt::format("Failed to create 'sell_orders_expiration_time' index: {}", result.error()));
  }

  return Storage(std::move(*db), *funds_item_id);
}

std::optional<UserId> Storage::get_user_id(std::string_view username) {
  auto user_id =
      this->_db.query("SELECT id FROM users WHERE username = ?1", username)
          .and_then([&](auto select) -> tl::expected<UserId, std::string> {
            int rc = sqlite3_step(select.inner);
            if (rc != SQLITE_ROW) {
              return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
            }
            return sqlite3_column_int(select.inner, 0);
          });
  if (!user_id) {
    return std::nullopt;
  }
  return *user_id;
}

tl::expected<UserId, std::string> Storage::create_user(std::string_view username) {
  auto user_inserted = this->_db.execute("INSERT INTO users (username) VALUES (?1)", username);
  if (!user_inserted) {
    return tl::make_unexpected(std::move(user_inserted.error()));
  }
  UserId user_id = this->_db.last_insert_rowid();

  auto insert_result = this->_db.execute("INSERT INTO user_items (user_id, item_id, quantity) VALUES (?1, ?2, 0)",
                                         user_id, this->_funds_item_id);
  if (!insert_result) {
    return tl::make_unexpected(std::move(insert_result.error()));
  }

  return user_id;
}

tl::expected<std::vector<std::pair<std::string, int>>, std::string> Storage::view_user_items(UserId user_id) {
  return this->_db
      .query(
          "SELECT items.name, user_items.quantity FROM user_items "
          "INNER JOIN items ON user_items.item_id = items.id "
          "WHERE user_items.user_id = ?1",
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

tl::expected<void, std::string> Storage::create_sell_order(SellOrder order) {
  return _db.execute(
      "INSERT INTO sell_orders (seller_id, item_id, quantity, price, expiration_time, buyer_id)"
      "VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
      order.seller_id, order.item_id, order.quantity, order.price, order.unix_expiration_time, order.buyer_id);
}

tl::expected<void, std::string> Storage::delete_sell_order(int order_id) {
  return _db.execute("DELETE FROM sell_orders WHERE id = ?1", order_id);
}

tl::expected<void, std::string> Storage::update_sell_order_buyer(int order_id, UserId buyer_id, int price) {
  return _db.execute("UPDATE sell_orders SET buyer_id = ?1, price = ?2 WHERE id = ?3", buyer_id, price, order_id);
}

tl::expected<std::vector<SellOrderInfo>, std::string> Storage::view_sell_orders() {
  return this->_db
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
          "INNER JOIN items ON sell_orders.item_id = items.id")
      .and_then([&](auto select) -> tl::expected<std::vector<SellOrderInfo>, std::string> {
        std::vector<SellOrderInfo> orders;
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

          orders.emplace_back(SellOrderInfo{ .id = id,
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
  auto transaction_guard = begin_transaction();
  if (!transaction_guard) {
    return tl::make_unexpected(fmt::format("Failed to start transaction: {}", transaction_guard.error()));
  }

  // get all executed auction orders
  auto executed_auction_orders =
      _db.query(
             "SELECT "
             "  id, "
             "  seller_id, "
             "  buyer_id, "
             "  item_id, "
             "  quantity, "
             "  price "
             "FROM sell_orders "
             "WHERE sell_orders.expiration_time <= ?1 AND buyer_id IS NOT NULL AND buyer_id != seller_id",
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
  auto update_result = _db.execute(
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
      "  AND user_items.item_id = aggregated_orders.item_id",
      unix_now, _funds_item_id);
  if (!update_result) {
    return tl::make_unexpected(fmt::format("Failed to cancel expired sell orders: {}", update_result.error()));
  }

  // Delete expired orders
  auto delete_result = _db.execute("DELETE FROM sell_orders WHERE expiration_time <= ?1", unix_now);
  if (!delete_result) {
    return tl::make_unexpected(fmt::format("Failed to delete expired sell orders: {}", delete_result.error()));
  }

  return transaction_guard->commit().and_then([executed = std::move(executed_auction_orders)]() { return executed; });
}

tl::expected<int, std::string> Storage::create_item(std::string_view item_name) {
  return this->_db.execute("INSERT INTO items (name) VALUES (?1)", item_name)
      .and_then([&]() -> tl::expected<int, std::string> { return this->_db.last_insert_rowid(); });
}

tl::expected<int, std::string> Storage::get_item_id(std::string_view item_name) {
  return this->_db.query("SELECT id FROM items WHERE name = ?1", item_name)
      .and_then([&](auto select) -> tl::expected<int, std::string> {
        int rc = sqlite3_step(select.inner);
        if (rc != SQLITE_ROW) {
          return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
        }
        return sqlite3_column_int(select.inner, 0);
      });
}

std::optional<int> Storage::get_user_items_quantity(UserId user_id, int item_id) {
  auto stmt = this->_db.query("SELECT quantity FROM user_items WHERE user_id = ?1 AND item_id = ?2", user_id, item_id);
  if (!stmt) {
    return std::nullopt;
  }
  int rc = sqlite3_step(stmt->inner);
  if (rc != SQLITE_ROW) {
    return std::nullopt;
  }
  return { sqlite3_column_int(stmt->inner, 0) };
}

tl::expected<void, std::string> Storage::add_user_item(UserId user_id, int item_id, int quantity) {
  return this->_db.execute(
      "INSERT INTO user_items (user_id, item_id, quantity) VALUES (?1, ?2, ?3) "
      "ON CONFLICT (user_id, item_id) DO UPDATE SET quantity = quantity + ?3",
      user_id, item_id, quantity);
}

tl::expected<void, std::string> Storage::sub_user_item(UserId user_id, int item_id, int quantity) {
  auto const user_item_quantity = get_user_items_quantity(user_id, item_id);
  if (!user_item_quantity || *user_item_quantity < quantity) {
    return tl::make_unexpected(fmt::format("Failed to withdraw {} items.", quantity));
  }

  tl::expected<void, std::string> reduce_result;
  if (item_id == _funds_item_id || user_item_quantity > quantity) {
    reduce_result = _db.execute("UPDATE user_items SET quantity = quantity - ?3 WHERE user_id = ?1 AND item_id = ?2",
                                user_id, item_id, quantity);
  } else {
    reduce_result = _db.execute("DELETE FROM user_items WHERE user_id = ?1 AND item_id = ?2", user_id, item_id);
  }
  return reduce_result;
}

std::optional<Storage::SellOrderInnerInfo> Storage::get_sell_order_info(int sell_order_id) {
  auto stmt = this->_db.query(
      "SELECT"
      "  sell_orders.seller_id,"
      "  sell_orders.item_id,"
      "  sell_orders.quantity,"
      "  sell_orders.price,"
      "  sell_orders.buyer_id "
      "FROM sell_orders "
      "WHERE sell_orders.id = ?1",
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

tl::expected<Storage::TransactionGuard, std::string> Storage::begin_transaction() {
  auto result = _db.execute("BEGIN");
  if (!result) {
    return tl::make_unexpected(std::move(result.error()));
  }
  return TransactionGuard(this);
}

void Storage::rollback_transaction() {
  _db.execute("ROLLBACK");
}

tl::expected<void, std::string> Storage::commit_transaction() {
  return _db.execute("COMMIT");
}
