#include "commands.hpp"
#include "storage.hpp"

#include <gtest/gtest.h>

TEST(Ping, Smoke) {
  // arg is ignored
  auto result = commands::Ping::parse({});
  ASSERT_TRUE(result);
  ASSERT_EQ(result->execute({}, {}), "pong");
}

TEST(Whoami, Smoke) {
  // arg is ignored
  auto result = commands::Whoami::parse({});
  ASSERT_TRUE(result);
  ASSERT_EQ(result->execute({ .username = "test" }, {}), "test");
}

TEST(Help, Smoke) {
  // arg is ignored
  auto result = commands::Help::parse({});
  ASSERT_TRUE(result);
  auto help_str = result->execute({}, {});
  ASSERT_EQ(help_str.find("Available commands:"), 0);
  ASSERT_NE(help_str.find("ping"), std::string::npos);
  ASSERT_NE(help_str.find("whoami"), std::string::npos);
  ASSERT_NE(help_str.find("help"), std::string::npos);
  ASSERT_NE(help_str.find("deposit"), std::string::npos);
  ASSERT_NE(help_str.find("withdraw"), std::string::npos);
  ASSERT_NE(help_str.find("view_items"), std::string::npos);
  ASSERT_NE(help_str.find("sell"), std::string::npos);
  ASSERT_NE(help_str.find("buy"), std::string::npos);
  ASSERT_NE(help_str.find("view_sell_orders"), std::string::npos);
}

TEST(Deposit, Parse) {
  auto result = commands::Deposit::parse("funds");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "funds");
  ASSERT_EQ(result->quantity, 1);

  result = commands::Deposit::parse("funds 10");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "funds");
  ASSERT_EQ(result->quantity, 10);

  result = commands::Deposit::parse("my amazing sword");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "my amazing sword");
  ASSERT_EQ(result->quantity, 1);

  result = commands::Deposit::parse("my amazing sword 5");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "my amazing sword");
  ASSERT_EQ(result->quantity, 5);

  // Parser is simplistic so only the last number is considered as quantity if it is a number
  result = commands::Deposit::parse("my amazing sword 5 10");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "my amazing sword 5");
  ASSERT_EQ(result->quantity, 10);

  // Negative quantity still should be parsed
  result = commands::Deposit::parse("my amazing sword -5");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "my amazing sword");
  ASSERT_EQ(result->quantity, -5);

  // Strange names also should be parsed
  result = commands::Deposit::parse("-5");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "-5");
  ASSERT_EQ(result->quantity, 1);
}

TEST(Withdraw, Parse) {
  auto result = commands::Withdraw::parse("funds");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "funds");
  ASSERT_EQ(result->quantity, 1);

  result = commands::Withdraw::parse("funds 10");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "funds");
  ASSERT_EQ(result->quantity, 10);

  result = commands::Withdraw::parse("my amazing sword");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "my amazing sword");
  ASSERT_EQ(result->quantity, 1);

  result = commands::Withdraw::parse("my amazing sword 5");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "my amazing sword");
  ASSERT_EQ(result->quantity, 5);

  // Parser is simplistic so only the last number is considered as quantity if it is a number
  result = commands::Withdraw::parse("my amazing sword 5 10");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "my amazing sword 5");
  ASSERT_EQ(result->quantity, 10);

  // Negative quantity still should be parsed
  result = commands::Withdraw::parse("my amazing sword -5");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "my amazing sword");
  ASSERT_EQ(result->quantity, -5);

  // Strange names also should be parsed
  result = commands::Withdraw::parse("-5");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "-5");
  ASSERT_EQ(result->quantity, 1);
}

TEST(ViewItems, Parse) {
  // arg is ignored
  auto result = commands::ViewItems::parse({});
  ASSERT_TRUE(result);
}

TEST(ViewSellOrders, Parse) {
  // arg is ignored
  auto result = commands::ViewSellOrders::parse({});
  ASSERT_TRUE(result);
}

TEST(Sell, Parse) {
  auto result = commands::Sell::parse("funds 10 11");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "funds");
  ASSERT_EQ(result->quantity, 10);
  ASSERT_EQ(result->price, 11);
  ASSERT_EQ(result->order_type, SellOrderType::Immediate);

  result = commands::Sell::parse("my amazing sword 123");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "my amazing sword");
  ASSERT_EQ(result->quantity, 1);
  ASSERT_EQ(result->price, 123);
  ASSERT_EQ(result->order_type, SellOrderType::Immediate);

  result = commands::Sell::parse("my amazing sword 123 10");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "my amazing sword");
  ASSERT_EQ(result->quantity, 123);
  ASSERT_EQ(result->price, 10);
  ASSERT_EQ(result->order_type, SellOrderType::Immediate);

  result = commands::Sell::parse("immediate my amazing sword 123 10");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "my amazing sword");
  ASSERT_EQ(result->quantity, 123);
  ASSERT_EQ(result->price, 10);
  ASSERT_EQ(result->order_type, SellOrderType::Immediate);

  result = commands::Sell::parse("auction my amazing sword 123 10");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->item_name, "my amazing sword");
  ASSERT_EQ(result->quantity, 123);
  ASSERT_EQ(result->price, 10);
  ASSERT_EQ(result->order_type, SellOrderType::Auction);

  // price is mandatory
  result = commands::Sell::parse("my amazing sword");
  ASSERT_FALSE(result);
}

TEST(Buy, Parse) {
  auto result = commands::Buy::parse("123");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->sell_order_id, 123);
  ASSERT_FALSE(result->bid);

  result = commands::Buy::parse("123 10");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->sell_order_id, 123);
  ASSERT_EQ(*result->bid, 10);

  // sell_order_id is mandatory
  result = commands::Buy::parse("");
  ASSERT_FALSE(result);

  // and it should be a number
  result = commands::Buy::parse("abc");
  ASSERT_FALSE(result);

  // bid should be a number
  result = commands::Buy::parse("123 abc");
  ASSERT_FALSE(result);

  // negatives are fine
  result = commands::Buy::parse("-123 -10");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->sell_order_id, -123);
  ASSERT_EQ(*result->bid, -10);
}
