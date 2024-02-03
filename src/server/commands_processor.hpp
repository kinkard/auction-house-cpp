#pragma once

#include "shared_state.hpp"

struct CommandsProcessor final {
  User user;
  std::shared_ptr<SharedState> shared_state;

public:
  CommandsProcessor(User user, std::shared_ptr<SharedState> shared_state)
      : user(std::move(user)), shared_state(std::move(shared_state)) {}

  // parses and executes a command
  std::string process_request(std::string_view request);
};
