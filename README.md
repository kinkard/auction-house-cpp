# The Auction House

Auction House is a test project that implements a TCP server and client according to the [Code Test - The Auction House](docs/Online%20Code%20Test%20-%20Auction%20House.pdf). See also <https://github.com/kinkard/auction-house> for a reference implementation in Rust.

## Supported functionality

- Users can log in using `client` or telnet, once `server` is launched
- Users can deposit or withdraw items, using the following command: `deposit/withdraw <item name> [quantity]`. For example, `deposit funds 100`
- Users can see their own items via `view_items`
- Users can create immediate or auction sell orders using `sell [immediate|auction] <item_name> [<quantity>] <price>` command. For example, `sell Sword 1 100` will create an immediate sell order for 1 Sword for 100 funds. 5% + 1 fund will be taken as a fee
- Users can see all sell orders via `view_sell_orders`
- Users can buy an item that is on sale or make a bid on an auction order. Sell orders are referred to by id. For example, `buy 20` will buy order #20, while `buy 20 200` will make a bid on the order #20 with 200 funds. Users will see errors if the order is not matched, if the bid is smaller than the current price, and so on
- Users will see notifications (if they are still connected) once their sell order is executed, either immediate or auction
- All transactions are available in the transaction log

Technical details:

- State is managed by sqlite3 via transactions, that guarantee that the server will never go into an incorrect state
- Each user is processed in an asynchronous manner (powered by boost.asio, which is included in the project as a standalone library), effectively utilizing CPU and memory
- Supported platforms: MacOS, Linux (tested on Ubuntu 22.04 LTS), Windows (VS2019)

## Build & Run

```sh
mkdir build && cd build
cmake .. && cmake --build . -j 10
./server 3000 db.sqlite transaction.log
```

The transaction log can be monitored via `tail -f transaction.log`.

Alternatively, VS2019 project files can be generated using `cmake .. -G "Visual Studio 16 2019"`.

## Client

This repo also contains a minimalistic client that sends everything you type in the console to the server and prints everything the server sends back. Telnet can be used instead.

```sh
$ ./client localhost:3000
> Welcome to Sundris Auction House, stranger! How can I call you?
Stepan
> Successfully logged in as Stepan
help
> Available commands:
- whoami: Displays the username of the current user
- ping: Replies 'pong'
- help: Prints this help message about all available commands

- deposit: Deposits a specified amount into the user's account. Format: 'deposit <item name> [<quantity>]'.
  'fund' is a special item name that can be used to deposit funds into the user's account
  Example: 'deposit funds 100' - deposits 100 funds, 'deposit Sword' - deposits 1 Sword
- withdraw: Withdraws a specified amount from the user's account. Format: 'withdraw <item name> [<quantity>]'
  Example: 'withdraw arrow 5' - withdraws 5 arrows, 'withdraw Sword' - withdraws 1 Sword
- view_items: Displays a list items for the current user

- view_sell_orders: Displays a list of all sell orders from all users
- sell: Places an item for sale at a specified price. Format: 'sell [immediate|auction] <item_name> [<quantity>] <price>'
  - immediate sell order - will be executed immediately once someone buys it. Otherwise it will expire in 5 minutes
    and items will be returned to the seller, but not the fee, which is `5% of the price + 1` funds
  - auction sell order - will be executed once it expires if someone placed a bid on it
- buy: Executes immediate sell order or places a bid on a auction sell order. Format: 'buy <sell_order_id> [<bid>]'
  - no bid - executes immediate sell order
  - bid - places a bid on a auction sell order
  
Usage: <command> [<args>], where `[]` annotates optional argumet(s)
```

## Dependencies

There are 5 external libraries used in this project

- [asio](https://github.com/chriskohlhoff/asio) - for asynchronous runtime and network
- [sqlite3](deps/sqlite3/) (the amalgamation from <https://www.sqlite.org/download.html>) - for state/storage. I've added my own C++ wrapper around it (see sqlite3.hpp/cpp) to reduce the amount of boilerplate code. All queries live in storage.cpp and are covered by tests in tests/storage_tests.cpp
- [fmt::fmt](https://github.com/fmtlib/fmt) - for nice and shiny formatting that works with VS2019
- [tl::expected](https://github.com/TartanLlama/expected) - A C++11 compatible way to handle errors without throwing exceptions everywhere
- [gtest](https://github.com/google/googletest) - for core logic tests

## VS2019 note

This Auction House implementation uses boost.asio (as a standalone library) and coroutines, which are [supported by VS2019 from 16.8 version](https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance?view=msvc-170). Please ensure that you use the latest available version of VS2019.
