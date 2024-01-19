#pragma once

#include "shared_state.hpp"

struct CommandsProcessor final {
  User user;
  std::shared_ptr<SharedState> shared_state;

private:
  using CommandHandlerType = std::string (CommandsProcessor::*)(std::string_view);
  static std::unordered_map<std::string_view, CommandHandlerType> const commands;

public:
  CommandsProcessor(User user, std::shared_ptr<SharedState> shared_state)
      : user(std::move(user)), shared_state(std::move(shared_state)) {}

  // parses and executes a command
  std::string process_request(std::string_view request);

private:
  // responsds with "pong"
  std::string ping(std::string_view args);
  // responds with username of the current user
  std::string whoami(std::string_view args);
  // prints help message with all available commands and their description
  std::string help(std::string_view args);

  // deposits an item with optional quantity
  std::string deposit(std::string_view args);
  // withdraws an item with optional quantity
  std::string withdraw(std::string_view args);
  // lists all items in the inventory for the current user
  std::string view_items(std::string_view);

  // places a sell order
  std::string sell(std::string_view args);
  // executes immediate sell order or places a bid on an auction sell order
  std::string buy(std::string_view args);
  // lists all sell orders from all users
  std::string view_sell_orders(std::string_view args);
};
