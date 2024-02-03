#include "commands_processor.hpp"
#include "commands.hpp"

#include <utility>

namespace {

// Takes the first word as a command name and the rest as arguments
std::pair<std::string_view, std::string_view> parse_command_name(std::string_view request) noexcept {
  std::size_t const space_pos = request.find(' ');
  if (space_pos == std::string_view::npos) {
    return { request, {} };
  }
  return { request.substr(0, space_pos), request.substr(space_pos + 1) };
}

using Command = std::variant<commands::Ping, commands::Whoami, commands::Help, commands::Deposit, commands::Withdraw,
                             commands::ViewItems, commands::Sell, commands::Buy, commands::ViewSellOrders>;

template <typename T>
std::optional<Command> parse(std::string_view args) {
  if (auto const parsed = T::parse(args); parsed) {
    return *parsed;
  }
  return std::nullopt;
}

std::unordered_map<std::string_view, std::optional<Command> (*)(std::string_view)> kCommandParsers{
  { "ping", parse<commands::Ping> },
  { "whoami", parse<commands::Whoami> },
  { "help", parse<commands::Help> },
  { "deposit", parse<commands::Deposit> },
  { "withdraw", parse<commands::Withdraw> },
  { "view_items", parse<commands::ViewItems> },
  { "sell", parse<commands::Sell> },
  { "buy", parse<commands::Buy> },
  { "view_sell_orders", parse<commands::ViewSellOrders> },
};

}  // namespace

std::string CommandsProcessor::process_request(std::string_view request) {
  auto const [command_name, args] = parse_command_name(request);
  auto const it = kCommandParsers.find(command_name);
  if (it == kCommandParsers.end()) {
    auto const help_str = commands::Help{}.execute(user, shared_state);
    return fmt::format("Failed to execute unknown command '{}'. {}", command_name, help_str);
  }

  auto command = std::invoke(it->second, args);
  if (!command) {
    return fmt::format("Failed to parse arguments for command '{}'", command_name);
  }

  return std::visit([this](auto & command) { return command.execute(user, shared_state); }, *command);
}
