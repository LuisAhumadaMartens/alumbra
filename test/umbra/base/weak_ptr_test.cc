#include "umbra/base/weak_ptr.h"

#include <memory>

#include "gtest/gtest.h"

namespace umbra::base {
namespace {

struct Base {
  virtual ~Base() = default;
  int value = 7;
};

struct Derived : Base {
  WeakPtrFactory<Derived> weak_factory{this};
};

TEST(WeakPtrTest, DereferencesWhileTheOwnerIsAlive) {
  auto owner = std::make_unique<Derived>();
  WeakPtr<Derived> weak = owner->weak_factory.GetWeakPtr();
  ASSERT_TRUE(weak);
  EXPECT_EQ(weak->value, 7);
  EXPECT_EQ(weak.get(), owner.get());
}

TEST(WeakPtrTest, BecomesNullWhenTheOwnerIsDestroyed) {
  auto owner = std::make_unique<Derived>();
  WeakPtr<Derived> weak = owner->weak_factory.GetWeakPtr();
  WeakPtr<Base> base = weak;
  owner.reset();
  EXPECT_FALSE(weak);
  EXPECT_EQ(base.get(), nullptr);
}

TEST(WeakPtrTest, InvalidatesExistingPointersOnly) {
  Derived owner;
  WeakPtr<Derived> before = owner.weak_factory.GetWeakPtr();
  EXPECT_TRUE(owner.weak_factory.HasWeakPtrs());
  owner.weak_factory.InvalidateWeakPtrs();
  EXPECT_FALSE(before);
  EXPECT_FALSE(owner.weak_factory.HasWeakPtrs());
  WeakPtr<Derived> after = owner.weak_factory.GetWeakPtr();
  EXPECT_TRUE(after);
}

TEST(WeakPtrTest, DefaultAndResetPointersAreNull) {
  WeakPtr<Derived> empty;
  EXPECT_FALSE(empty);
  Derived owner;
  WeakPtr<Derived> weak = owner.weak_factory.GetWeakPtr();
  weak.reset();
  EXPECT_EQ(weak.get(), nullptr);
}

}
}
