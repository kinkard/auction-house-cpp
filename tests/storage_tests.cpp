#include "storage.hpp"

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include <memory>

TEST(Storage, get_or_create_user) {
  auto storage = Storage::open("test.db");
  ASSERT_TRUE(storage) << storage.error();
  auto deferred = std::shared_ptr<void>(nullptr, [](...) { std::remove("test.db"); });

  ASSERT_EQ(storage->get_or_create_user("user1")->id, 1);
  ASSERT_EQ(storage->get_or_create_user("user2")->id, 2);
  ASSERT_EQ(storage->get_or_create_user("user3")->id, 3);

  ASSERT_EQ(storage->get_or_create_user("user1")->id, 1);
  ASSERT_EQ(storage->get_or_create_user("user2")->id, 2);
  ASSERT_EQ(storage->get_or_create_user("user3")->id, 3);
}

TEST(Storage, funds) {
  auto storage = Storage::open("test.db");
  ASSERT_TRUE(storage) << storage.error();
  auto deferred = std::shared_ptr<void>(nullptr, [](...) { std::remove("test.db"); });

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

TEST(Storage, items) {
  auto storage = Storage::open("test.db");
  ASSERT_TRUE(storage) << storage.error();
  auto deferred = std::shared_ptr<void>(nullptr, [](...) { std::remove("test.db"); });

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
