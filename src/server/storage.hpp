#pragma once

#include "sqlite3.hpp"
#include "tl/expected.hpp"

struct UserId {
  int id;
};

class Storage final {
  Sqlite3 db;

  // constructor is private, use `open` instead
  Storage(Sqlite3 && db) noexcept : db(std::move(db)){};

public:
  tl::expected<Storage, std::string> static open(char const * path);
  ~Storage() = default;

  // This class cannot be copied, but can be moved
  Storage(Storage const &) = delete;
  Storage & operator=(Storage const &) = delete;
  Storage(Storage &&) = default;
  Storage & operator=(Storage &&) = default;

  // Returns user id of the already existing or newly created user
  tl::expected<UserId, std::string> get_or_create_user(std::string_view username);

private:
};
