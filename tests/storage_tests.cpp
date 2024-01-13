#include "gmock/gmock.h"
#include "storage.hpp"

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include <memory>

class StorageTest : public ::testing::Test {
protected:
  void SetUp() override {
    std::remove("test.db");
    auto storage = Storage::open("test.db");
    ASSERT_TRUE(storage) << storage.error();
    this->storage = std::make_unique<Storage>(std::move(storage.value()));
  }

  void TearDown() override { std::remove("test.db"); }

  std::unique_ptr<Storage> storage;
};

TEST_F(StorageTest, get_or_create_user) {
  ASSERT_EQ(storage->get_or_create_user("user1")->id, 1);
  ASSERT_EQ(storage->get_or_create_user("user2")->id, 2);
  ASSERT_EQ(storage->get_or_create_user("user3")->id, 3);

  ASSERT_EQ(storage->get_or_create_user("user1")->id, 1);
  ASSERT_EQ(storage->get_or_create_user("user2")->id, 2);
  ASSERT_EQ(storage->get_or_create_user("user3")->id, 3);
}

TEST_F(StorageTest, funds) {
  auto user = *storage->get_or_create_user("user1");
  // freshly created user always has 0 funds
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 0)));

  ASSERT_TRUE(storage->deposit(user.id, "funds", 10));
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 10)));

  ASSERT_TRUE(storage->withdraw(user.id, "funds", 5));
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 5)));

  // get_or_create_user should not create new funds
  user = *storage->get_or_create_user("user1");
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 5)));

  // withdraw more than we have
  ASSERT_FALSE(storage->withdraw(user.id, "funds", 10));

  // deposit negative amount
  ASSERT_FALSE(storage->deposit(user.id, "funds", -10));

  // withdraw negative amount
  ASSERT_FALSE(storage->withdraw(user.id, "funds", -10));

  // deposit to non-existing user
  ASSERT_FALSE(storage->deposit(100, "funds", 10));

  // withdraw from non-existing user
  ASSERT_FALSE(storage->withdraw(100, "funds", 10));

  // and finally check that we can deposit and withdraw from different users
  user = *storage->get_or_create_user("user2");
  ASSERT_TRUE(storage->deposit(user.id, "funds", 20));

  user = *storage->get_or_create_user("user3");
  ASSERT_TRUE(storage->deposit(user.id, "funds", 30));

  // user1 is still untouched
  user = *storage->get_or_create_user("user1");
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 5)));

  // user2 has 20 funds
  user = *storage->get_or_create_user("user2");
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 20)));

  // user3 has 30 funds
  user = *storage->get_or_create_user("user3");
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 30)));

  user = *storage->get_or_create_user("foo");
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 0)));
  ASSERT_TRUE(storage->deposit(user.id, "funds", 100500));
  ASSERT_TRUE(storage->withdraw(user.id, "funds", 100400));
}

TEST_F(StorageTest, items) {
  auto user = *storage->get_or_create_user("user1");
  // freshly created user always has 0 items
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 0)));

  ASSERT_TRUE(storage->deposit(user.id, "item1", 10));
  EXPECT_THAT(*storage->view_items(user.id),
              testing::ElementsAre(std::make_pair("funds", 0), std::make_pair("item1", 10)));

  ASSERT_TRUE(storage->deposit(user.id, "item2", 20));
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 0), std::make_pair("item1", 10), std::make_pair("item2", 20)));

  ASSERT_TRUE(storage->withdraw(user.id, "item1", 5));
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 0), std::make_pair("item1", 5), std::make_pair("item2", 20)));

  ASSERT_TRUE(storage->withdraw(user.id, "item2", 10));
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 0), std::make_pair("item1", 5), std::make_pair("item2", 10)));

  // get_or_create_user should not create new items
  user = *storage->get_or_create_user("user1");
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 0), std::make_pair("item1", 5), std::make_pair("item2", 10)));

  // withdraw more than we have
  ASSERT_FALSE(storage->withdraw(user.id, "item1", 10));

  // deposit negative amount
  ASSERT_FALSE(storage->deposit(user.id, "item1", -10));

  // withdraw negative amount
  ASSERT_FALSE(storage->withdraw(user.id, "item1", -10));

  // deposit to non-existing user
  ASSERT_FALSE(storage->deposit(100, "item1", 10));

  // withdraw from non-existing user
  ASSERT_FALSE(storage->withdraw(100, "item1", 10));
}

bool operator==(const SellOrder & lhs, const SellOrder & rhs) {
  return lhs.id == rhs.id && lhs.user_name == rhs.user_name && lhs.item_name == rhs.item_name &&
         lhs.quantity == rhs.quantity && lhs.price == rhs.price && lhs.expiration_time == rhs.expiration_time;
}

TEST_F(StorageTest, place_sell_order) {
  auto user = *storage->get_or_create_user("user");
  ASSERT_TRUE(storage->deposit(user.id, "item1", 10));
  ASSERT_TRUE(storage->deposit(user.id, "item2", 20));
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 0), std::make_pair("item1", 10), std::make_pair("item2", 20)));

  ASSERT_TRUE(storage->place_sell_order(user.id, "item1", 5, 10, "2021-01-01 00:00:00"));
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 0), std::make_pair("item1", 5), std::make_pair("item2", 20)));

  ASSERT_TRUE(storage->place_sell_order(user.id, "item2", 10, 20, "2021-01-01 00:00:00"));
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 0), std::make_pair("item1", 5), std::make_pair("item2", 10)));

  auto result = storage->view_sell_orders();
  ASSERT_TRUE(result) << result.error();
  EXPECT_THAT(result.value(), testing::ElementsAre(SellOrder{ 1, "user", "item1", 5, 10, "2021-01-01 00:00:00" },
                                                   SellOrder{ 2, "user", "item2", 10, 20, "2021-01-01 00:00:00" }));

  // cancel expired orders
  auto cancel_result = storage->cancel_expired_sell_orders("2021-01-01 00:00:00");
  ASSERT_TRUE(cancel_result) << cancel_result.error();
  EXPECT_THAT(*storage->view_sell_orders(), testing::IsEmpty());
  // check that items are returned back
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 0), std::make_pair("item1", 10), std::make_pair("item2", 20)));

  // check 5 min expiration
  // ASSERT_TRUE(storage->place_sell_order(user.id, "item1", 5, 10, "2021-01-01 00:00:00"));
  // ASSERT_TRUE(storage->place_sell_order(user.id, "item2", 10, 20, "2021-01-01 00:00:00"));

  // try to sell more than we have
  ASSERT_FALSE(storage->place_sell_order(user.id, "item1", 110, 10, "2021-01-01 00:00"));

  // try to sell negative amount
  ASSERT_FALSE(storage->place_sell_order(user.id, "item1", -10, 10, "2021-01-01 00:00"));

  // try to sell for negative price
  ASSERT_FALSE(storage->place_sell_order(user.id, "item1", 10, -10, "2021-01-01 00:00"));

  // try to sell non-existing item
  ASSERT_FALSE(storage->place_sell_order(user.id, "non existing item", 10, 10, "2021-01-01 00:00"));

  // try to sell to from non-existing user
  ASSERT_FALSE(storage->place_sell_order(100, "item1", 10, 10, "2021-01-01 00:00"));

  // cannot sell funds
  ASSERT_FALSE(storage->place_sell_order(user.id, "funds", 10, 10, "2021-01-01 00:00"));

  EXPECT_THAT(*storage->view_sell_orders(), testing::IsEmpty());
}
