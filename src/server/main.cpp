#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
#include <asio/write.hpp>

#include <charconv>
#include <cstdio>
#include <cstring>
#include <iterator>

using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;
using asio::ip::tcp;

awaitable<void> handle_client(tcp::socket socket) {
  try {
    // just read whaterver the client sends us and send it back
    for (;;) {
      char data[128];
      std::size_t n = co_await socket.async_read_some(asio::buffer(data), use_awaitable);
      co_await async_write(socket, asio::buffer(data, n), use_awaitable);
    }
  } catch (std::exception & e) {
    std::printf("echo Exception: %s\n", e.what());
  }
}

awaitable<void> listener(uint16_t port) {
  auto executor = co_await asio::this_coro::executor;
  tcp::acceptor acceptor(executor, { tcp::v4(), port });
  std::printf("Listening on port %u\n", port);
  for (;;) {
    tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
    co_spawn(executor, handle_client(std::move(socket)), detached);
  }
}

int main(int argc, char * argv[]) {
  if (argc != 2) {
    std::printf("Usage: server <port>\n");
    std::printf("Example: server 3000\n");
    return 1;
  }

  uint16_t port;
  std::from_chars_result result = std::from_chars(argv[1], argv[1] + strlen(argv[1]), port);
  if (result.ec != std::errc()) {
    std::printf("Invalid argument '%s'. Port must be in range [1, 65535]\n", argv[1]);
    return 1;
  }

  try {
    asio::io_context io_context(1);

    // Graceful shutdown
    asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) {
      printf("Shutting down...\n");
      io_context.stop();
    });

    co_spawn(io_context, listener(port), detached);

    io_context.run();
  } catch (std::exception & e) {
    std::printf("Exception: %s\n", e.what());
  }
}
