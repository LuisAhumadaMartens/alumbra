#include "umbra/base/weak_ptr.h"

#include <memory>
#include <thread>

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

struct VirtualDerived : virtual Base {
  WeakPtrFactory<VirtualDerived> weak_factory{this};
};

TEST(WeakPtrTest, ConvertsVirtualBasesOnlyAfterValidation) {
  auto owner = std::make_unique<VirtualDerived>();
  auto weak = owner->weak_factory.GetWeakPtr();
  WeakPtr<Base> const live = weak;
  EXPECT_EQ(live.get(), static_cast<Base*>(owner.get()));
  owner.reset();
  WeakPtr<Base> expired;
  std::thread copier([&] { expired = weak; });
  copier.join();
  EXPECT_EQ(expired.get(), nullptr);
  WeakPtr<const Base> const chained = expired;
  EXPECT_EQ(chained.get(), nullptr);
}

TEST(WeakPtrTest, ConvertsLiveVirtualBasesAcrossSequences) {
  VirtualDerived owner;
  auto weak = owner.weak_factory.GetWeakPtr();
  WeakPtr<Base> base;
  std::thread copier([&] { base = weak; });
  copier.join();
  EXPECT_EQ(base.get(), static_cast<Base*>(&owner));
}

TEST(WeakPtrTest, DereferencesWhileTheOwnerIsAlive) {
  auto owner = std::make_unique<Derived>();
  WeakPtr<Derived> const weak = owner->weak_factory.GetWeakPtr();
  ASSERT_TRUE(weak);
  EXPECT_EQ(weak->value, 7);
  EXPECT_EQ(weak.get(), owner.get());
}

TEST(WeakPtrTest, BecomesNullWhenTheOwnerIsDestroyed) {
  auto owner = std::make_unique<Derived>();
  WeakPtr<Derived> const weak = owner->weak_factory.GetWeakPtr();
  WeakPtr<Base> const base = weak;
  owner.reset();
  EXPECT_FALSE(weak);
  EXPECT_EQ(base.get(), nullptr);
}

TEST(WeakPtrTest, InvalidatesExistingPointersOnly) {
  Derived owner;
  WeakPtr<Derived> const before = owner.weak_factory.GetWeakPtr();
  EXPECT_TRUE(owner.weak_factory.HasWeakPtrs());
  owner.weak_factory.InvalidateWeakPtrs();
  EXPECT_FALSE(before);
  EXPECT_FALSE(owner.weak_factory.HasWeakPtrs());
  WeakPtr<Derived> const after = owner.weak_factory.GetWeakPtr();
  EXPECT_TRUE(after);
}

TEST(WeakPtrTest, DefaultAndResetPointersAreNull) {
  WeakPtr<Derived> const empty;
  EXPECT_FALSE(empty);
  Derived owner;
  WeakPtr<Derived> weak = owner.weak_factory.GetWeakPtr();
  weak.reset();
  EXPECT_EQ(weak.get(), nullptr);
}

}
}
