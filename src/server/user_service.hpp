#pragma once

#include "types.hpp"

#include <tl/expected.hpp>

#include <memory>

class Storage;

class UserService final {
  std::shared_ptr<Storage> storage;

public:
  UserService(std::shared_ptr<Storage> storage) : storage(std::move(storage)) {}

  // Logs in the user
  tl::expected<User, std::string> login(std::string_view username);
};
