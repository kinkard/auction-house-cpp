# The Auction House

This is a code test project for [Senior Online Programmer (New Game IP)](https://www.ubisoft.com/en-us/company/careers/search/743999932289413-senior-online-programmer-new-game-ip-) position at Ubisoft Stockholm.

## Build & Run

```sh
mkdir build && cd build
cmake .. && cmake --build . -j 10
./server 3000 db.sqlite transaction.log
```

Transaction log can be monitored via `tail -f transaction.log`.

## Client

This repo also contains a minimalistic client that sends everything you type in console to the server and prints everything server sends back. The telnet can be used instead.

```sh
$ ./client localhost:3000
> Welcome to Sundris Auction House, stranger! How can I call you?
Stepan
> Successfully logged in as Stepan
help
> Available commands:
- sell - Places an item for sale at a specified price. Format: 'sell [immediate|auction] <item_name> [<quantity>] <price>'
- view_sell_orders - Displays a list of all current sell orders from all users
- buy - Executes immediate sell order or places a bid on a auction sell order. Format: 'buy <sell_order_id> [<bid>]'
- view_items - Displays a list items for the current user
- whoami - Displays the username of the current user
- withdraw - Withdraws a specified amount from the user's account. Format: 'withdraw <item name> [<quantity>]'
- deposit - Deposits a specified amount into the user's account. Format: 'deposit <item name> [<quantity>]'
- help - Prints this help message about all available commands
- ping - Replies 'pong'
Usage: <command> [<args>], where `[]` annotates optional argumet(s)

```

## Dependencies

There are 5 external library used in this project

- [asio](deps/asio/) - for asynchronous runtime and network
- [sqlite3](deps/sqlite3/) - for state/storage. I've added my own C++ wrapper around it (see sqlite3.hpp/cpp) to reduce amount of boiler plate code. All queries live in storage.cpp and covered by tests in tests/storage_tests.cpp
- [fmt::fmt](deps/fmt/) - for nice and shiny formatting that would work with VS2019
- [tl::expected](deps/expected/) - C++11 compatible way to handle errors without throwing exceptions everywhere
- [gtest](deps/goodletest/) - for core logic tests

## VS2019 note

This Auction House implementation uses boost.asio (as a standalone library) and corutines, which are [supported by VS2019 from 16.8 version](https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance?view=msvc-170). Please ensure that you use the latest available version of the VS2019.
