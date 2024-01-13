#include "storage.hpp"

#include <sqlite3.h>

#include <fmt/format.h>

#include <stdexcept>
#include <string>

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
  result = db->execute("INSERT OR IGNORE INTO items (name) VALUES ('funds');");
  if (!result) {
    return tl::make_unexpected(fmt::format("Failed to insert 'funds' item: {}", result.error()));
  }
  auto funds_item_id =
      db->query("SELECT id FROM items WHERE name = 'funds';")
          .and_then([&](auto select) -> tl::expected<int, std::string> {
            int rc = sqlite3_step(select.inner);
            if (rc != SQLITE_ROW) {
              return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
            }
            return sqlite3_column_int(select.inner, 0);
          });
  if (!funds_item_id) {
    return tl::make_unexpected(fmt::format("Failed to get 'funds' item id: {}", funds_item_id.error()));
  }

  result = db->execute(
      "CREATE TABLE IF NOT EXISTS user_items ("
      "user_id INTEGER NOT NULL,"
      "item_id INTEGER NOT NULL,"
      "count INTEGER NOT NULL DEFAULT 1,"
      "FOREIGN KEY (user_id) REFERENCES users (id),"
      "FOREIGN KEY (item_id) REFERENCES items (id),"
      "PRIMARY KEY (user_id, item_id)"
      ");");
  if (!result) {
    return tl::make_unexpected(fmt::format("Failed to create 'user_items' table: {}", result.error()));
  }

  return Storage(std::move(*db), *funds_item_id);
}

tl::expected<User, std::string> Storage::get_or_create_user(std::string_view username) {
  // We always do INSERT to make sure that the user exists
  auto inserted =
      this->db.query("INSERT INTO users (username) VALUES (?1);", username)
          .and_then([&](auto insert) -> tl::expected<void, std::string> {
            int rc = sqlite3_step(insert.inner);
            if (rc != SQLITE_DONE) {
              return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
            }
            return {};
          });

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
  if (inserted) {
    auto insert_funds =
        this->db
            .query("INSERT INTO user_items (user_id, item_id, count) VALUES (?1, ?2, 0);", user_id.value().id,
                   this->funds_item_id)
            .and_then([&](auto insert) -> tl::expected<void, std::string> {
              int rc = sqlite3_step(insert.inner);
              if (rc != SQLITE_DONE) {
                return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
              }
              return {};
            });
    if (!insert_funds) {
      return tl::make_unexpected(insert_funds.error());
    }
  }
  return user_id;
}

tl::expected<void, std::string> Storage::deposit(UserId user_id, std::string_view item_name, int count) {
  if (count < 0) {
    return tl::make_unexpected("Cannot deposit negative amount");
  }

  if (!this->is_valid_user(user_id)) {
    return tl::make_unexpected(fmt::format("User with id={} doesn't exist", user_id));
  }

  // We do INSERT OR IGNORE to make sure that the item exists
  auto insert =
      this->db.query("INSERT OR IGNORE INTO items (name) VALUES (?1);", item_name)
          .and_then([&](auto insert) -> tl::expected<void, std::string> {
            int rc = sqlite3_step(insert.inner);
            if (rc != SQLITE_DONE) {
              return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
            }
            return {};
          });
  if (!insert) {
    return tl::make_unexpected(insert.error());
  }

  auto item_id =
      this->db.query("SELECT id FROM items WHERE name = ?1;", item_name)
          .and_then([&](auto select) -> tl::expected<int, std::string> {
            int rc = sqlite3_step(select.inner);
            if (rc != SQLITE_ROW) {
              return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
            }
            return sqlite3_column_int(select.inner, 0);
          });
  if (!item_id) {
    return tl::make_unexpected(item_id.error());
  }

  auto update =
      this->db
          .query(
              "INSERT INTO user_items (user_id, item_id, count) VALUES (?1, ?2, ?3) "
              "ON CONFLICT (user_id, item_id) DO UPDATE SET count = count + ?3;",
              user_id, item_id.value(), count)
          .and_then([&](auto insert) -> tl::expected<void, std::string> {
            int rc = sqlite3_step(insert.inner);
            if (rc != SQLITE_DONE) {
              return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
            }
            return {};
          });
  return update;
}

tl::expected<void, std::string> Storage::withdraw(UserId user_id, std::string_view item_name, int count) {
  if (count < 0) {
    return tl::make_unexpected("Cannot withdraw negative amount");
  }

  if (!this->is_valid_user(user_id)) {
    return tl::make_unexpected(fmt::format("User with id={} doesn't exist", user_id));
  }

  // if item doesn't exist then it cannot be withdrawn
  auto select =
      this->db.query("SELECT id FROM items WHERE name = ?1;", item_name)
          .and_then([&](auto select) -> tl::expected<int, std::string> {
            int rc = sqlite3_step(select.inner);
            if (rc != SQLITE_ROW) {
              return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
            }
            return sqlite3_column_int(select.inner, 0);
          });
  if (!select) {
    return tl::make_unexpected(select.error());
  }
  int item_id = select.value();

  // if user doesn't have enough items then it cannot be withdrawn
  auto user_item_count =
      this->db.query("SELECT count FROM user_items WHERE user_id = ?1 AND item_id = ?2;", user_id, item_id)
          .and_then([&](auto select) -> tl::expected<int, std::string> {
            int rc = sqlite3_step(select.inner);
            if (rc != SQLITE_ROW) {
              return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
            }
            return sqlite3_column_int(select.inner, 0);
          });
  if (!user_item_count) {
    return tl::make_unexpected(user_item_count.error());
  }
  if (user_item_count.value() < count) {
    return tl::make_unexpected(fmt::format("User doesn't have enough items to withdraw: {}", user_item_count.value()));
  }

  auto update =
      this->db
          .query(
              "INSERT INTO user_items (user_id, item_id, count) VALUES (?1, ?2, ?3) "
              "ON CONFLICT (user_id, item_id) DO UPDATE SET count = count - ?3;",
              user_id, item_id, count)
          .and_then([&](auto insert) -> tl::expected<void, std::string> {
            int rc = sqlite3_step(insert.inner);
            if (rc != SQLITE_DONE) {
              return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
            }
            return {};
          });
  return update;
}

tl::expected<std::vector<std::pair<std::string, int>>, std::string> Storage::view_items(UserId user_id) {
  return this->db
      .query(
          "SELECT items.name, user_items.count FROM user_items "
          "INNER JOIN items ON user_items.item_id = items.id "
          "WHERE user_items.user_id = ?1;",
          user_id)
      .and_then([&](auto select) -> tl::expected<std::vector<std::pair<std::string, int>>, std::string> {
        std::vector<std::pair<std::string, int>> items;
        int rc;
        while ((rc = sqlite3_step(select.inner)) == SQLITE_ROW) {
          std::string item_name = reinterpret_cast<char const *>(sqlite3_column_text(select.inner, 0));
          int count = sqlite3_column_int(select.inner, 1);
          items.emplace_back(std::move(item_name), count);
        }
        if (rc != SQLITE_DONE) {
          return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
        }
        return items;
      });
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
