#pragma once

#include "shared_state.hpp"

struct CommandsProcessor final {
  User user;
  std::shared_ptr<SharedState> shared_state;

private:
  using HandlerType = std::string (CommandsProcessor::*)(std::string_view);
  struct UserCommand {
    // Stores a pointer to a function that takes UserConnection and args and returns a string
    HandlerType invoke;

    // Description of the command to be shown in help
    std::string_view description;
  };
  static std::unordered_map<std::string_view, UserCommand> const commands;

public:
  CommandsProcessor(User user, std::shared_ptr<SharedState> shared_state)
      : user(std::move(user)), shared_state(std::move(shared_state)) {}

  std::string process_request(std::string_view request);

private:
  std::string ping(std::string_view args);
  std::string whoami(std::string_view args);
  std::string help(std::string_view args);

  std::string deposit(std::string_view args);
  std::string withdraw(std::string_view args);
  std::string view_items(std::string_view);

  std::string sell(std::string_view args);
  std::string buy(std::string_view args);
  std::string view_sell_orders(std::string_view args);
};
