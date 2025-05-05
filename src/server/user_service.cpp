#include "user_service.hpp"
#include "storage.hpp"

tl::expected<User, std::string> UserService::login(std::string_view username) {
  if (username.empty()) {
    return tl::make_unexpected("Username cannot be empty");
  }

  auto user_id = storage->get_user_id(username);
  if (user_id) {
    return User{ .id = *user_id, .username = std::string(username) };
  }

  // simply create a new user as we have no concept of registration or passwords
  return storage->create_user(username)
      .map([&](UserId user_id) { return User{ .id = user_id, .username = std::string(username) }; })
      .and_then([&](User user) {
        // create a "funds" item for the user to store the balance
        return storage->add_user_item(user.id, storage->funds_item_id(), 0).map([&]() { return user; });
      });
}
