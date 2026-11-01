#include "penumbra/interface/shared_bytes.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "gtest/gtest.h"

namespace penumbra {
namespace {

TEST(SharedBytesTest, AdoptKeepsTheVectorBuffer) {
  std::vector<uint8_t> bytes = {1, 2, 3, 4};
  const uint8_t* original = bytes.data();
  const SharedBytes shared = SharedBytes::Adopt(std::move(bytes));
  EXPECT_EQ(shared.data(), original);
  EXPECT_EQ(shared.size(), 4U);
  EXPECT_EQ(shared.span()[2], 3);
}

TEST(SharedBytesTest, CopyOwnsAnIndependentBuffer) {
  const std::array<uint8_t, 3> source = {7, 8, 9};
  const SharedBytes shared = SharedBytes::Copy(source);
  EXPECT_NE(shared.data(), source.data());
  EXPECT_EQ(shared.size(), 3U);
  EXPECT_EQ(shared.span()[0], 7);
}

TEST(SharedBytesTest, SlicesShareStorageAndKeepItAlive) {
  std::optional<SharedBytes> slice;
  const uint8_t* base = nullptr;
  {
    const SharedBytes whole = SharedBytes::Adopt({10, 11, 12, 13, 14});
    base = whole.data();
    slice = whole.Slice(1, 3);
  }
  ASSERT_TRUE(slice.has_value());
  EXPECT_EQ(slice->data(), base + 1);
  EXPECT_EQ(slice->size(), 3U);
  EXPECT_EQ(slice->span()[0], 11);
  EXPECT_EQ(slice->span()[2], 13);
}

TEST(SharedBytesTest, RejectsSlicesOutOfRange) {
  const SharedBytes whole = SharedBytes::Adopt({1, 2, 3});
  EXPECT_TRUE(whole.Slice(0, 3).has_value());
  EXPECT_TRUE(whole.Slice(3, 0).has_value());
  EXPECT_FALSE(whole.Slice(4, 0).has_value());
  EXPECT_FALSE(whole.Slice(2, 2).has_value());
  EXPECT_FALSE(whole.Slice(1, SIZE_MAX).has_value());
}

TEST(SharedBytesTest, WrapKeepsTheOwnerAliveUntilTheLastReference) {
  bool released = false;
  auto* buffer = new std::array<uint8_t, 4>{5, 6, 7, 8};
  std::shared_ptr<const void> owner(buffer, [&released](const void* pointer) {
    released = true;
    delete static_cast<const std::array<uint8_t, 4>*>(pointer);
  });
  std::optional<SharedBytes> tail;
  {
    const SharedBytes wrapped = SharedBytes::Wrap(std::move(owner), *buffer);
    EXPECT_EQ(wrapped.data(), buffer->data());
    tail = wrapped.Slice(2, 2);
  }
  EXPECT_FALSE(released);
  ASSERT_TRUE(tail.has_value());
  EXPECT_EQ(tail->span()[1], 8);
  tail.reset();
  EXPECT_TRUE(released);
}

TEST(SharedBytesTest, DefaultIsEmpty) {
  const SharedBytes empty;
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(empty.data(), nullptr);
  EXPECT_TRUE(empty.span().empty());
}

}
}
