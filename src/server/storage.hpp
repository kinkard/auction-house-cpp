#pragma once

#include <string_view>

struct sqlite3;

struct UserId {
  int id;
};

class Storage final {
public:
  Storage(char const * path);
  ~Storage();

  // This class cannot be copied, but can be moved
  Storage(Storage const &) = delete;
  Storage & operator=(Storage const &) = delete;
  Storage(Storage &&) = default;
  Storage & operator=(Storage &&) = default;

  // Returns user id of the already existing or newly created user
  UserId get_or_create_user(std::string_view username);

private:
  sqlite3 * db;
};
