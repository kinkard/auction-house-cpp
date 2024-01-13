#include "storage.hpp"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
#include <asio/write.hpp>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <charconv>
#include <cstring>
#include <iterator>
#include <string_view>
#include <unordered_set>

using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;
using asio::ip::tcp;

std::pair<std::string_view, std::string_view> parse_command(std::string_view request) noexcept {
  std::size_t const space_pos = request.find(' ');
  if (space_pos == std::string_view::npos) {
    return { request, {} };
  }
  return { request.substr(0, space_pos), request.substr(space_pos + 1) };
}

awaitable<void> handle_client(tcp::socket socket, std::shared_ptr<Storage> storage) {
  char buffer[256];

  std::unordered_set<std::string_view> const commands = { "ping", "deposit", "withdraw",   "view_items",
                                                          "sell", "buy",     "view_orders" };

  try {
    // first, validate the greating message
    std::size_t n = co_await socket.async_read_some(asio::buffer(buffer), use_awaitable);
    auto [command, username] = parse_command({ buffer, n });
    if (command != "login") {
      std::string response = "Failed to login. Expected: login <username>\n";
      co_await async_write(socket, asio::buffer(response), use_awaitable);
      co_return;  // it will close the socket as well
    }

    auto user_id = storage->get_or_create_user(username);
    if (!user_id) {
      std::string response = fmt::format("Failed to login as '{}': {}", username, user_id.error());
      co_await async_write(socket, asio::buffer(response), use_awaitable);
      co_return;  // it will close the socket as well
    }

    std::string response =
        fmt::format("Successfully logged in as {}. Now you can use the following commands: {}", username, commands);
    co_await async_write(socket, asio::buffer(response), use_awaitable);
    fmt::println("User {} logged in with id {}", username, user_id->id);

    // Keep reading commands from the socket and executing them
    for (;;) {
      std::size_t n = co_await socket.async_read_some(asio::buffer(buffer), use_awaitable);
      auto [command, args] = parse_command({ buffer, n });
      if (commands.find(command) == commands.end()) {
        std::string response =
            fmt::format("Failed to execute unknown command '{}'. Available commands are {}", command, commands);
        co_await async_write(socket, asio::buffer(response), use_awaitable);
        continue;
      }

      co_await async_write(socket, asio::buffer(buffer, n), use_awaitable);
    }
  } catch (std::exception & e) {
    fmt::println("Connection closed by client: {}", e.what());
  }
}

awaitable<void> listener(uint16_t port, std::shared_ptr<Storage> storage) {
  auto executor = co_await asio::this_coro::executor;
  tcp::acceptor acceptor(executor, { tcp::v4(), port });
  fmt::println("Listening on port {}", port);
  for (;;) {
    tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
    co_spawn(executor, handle_client(std::move(socket), storage), detached);
  }
}

int main(int argc, char * argv[]) {
  if (argc != 3) {
    fmt::println("Usage: server <port> <path_to_db>");
    fmt::println("Example: server 3000 db.sqlite");
    return 1;
  }

  uint16_t port;
  std::from_chars_result result = std::from_chars(argv[1], std::strchr(argv[1], '\0'), port);
  if (result.ec != std::errc()) {
    fmt::println("Invalid argument '{}'. Port must be in range [1, 65535]", argv[1]);
    return 1;
  }

  auto storage = Storage::open(argv[2]);
  if (!storage) {
    fmt::println("Failed to open database: {}", storage.error());
    return 1;
  }

  try {
    asio::io_context io_context(1);

    // Graceful shutdown
    asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) {
      fmt::println("Shutting down...");
      io_context.stop();
    });

    co_spawn(io_context, listener(port, std::make_shared<Storage>(std::move(*storage))), detached);

    io_context.run();
  } catch (std::exception & e) {
    fmt::println("Exception: {}", e.what());
  }
}
