#include "umbra/demux/byte_queue.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "penumbra/interface/shared_bytes.h"

namespace umbra::demux {
namespace {

penumbra::SharedBytes Chunk(std::vector<uint8_t> bytes) {
  return penumbra::SharedBytes::Adopt(std::move(bytes));
}

std::vector<uint8_t> ToVector(const penumbra::SharedBytes& bytes) {
  const std::span<const uint8_t> view = bytes.span();
  return {view.begin(), view.end()};
}

TEST(ByteQueueTest, TracksOffsetsAcrossAppendsAndDiscards) {
  ByteQueue queue(100);
  EXPECT_EQ(queue.StartOffset(), 100U);
  EXPECT_EQ(queue.EndOffset(), 100U);
  queue.Append(Chunk({1, 2, 3}));
  queue.Append(Chunk({}));
  queue.Append(Chunk({4, 5}));
  EXPECT_EQ(queue.size(), 5U);
  EXPECT_EQ(queue.EndOffset(), 105U);
  queue.Discard(4);
  EXPECT_EQ(queue.StartOffset(), 104U);
  EXPECT_EQ(queue.size(), 1U);
  queue.Discard(100);
  EXPECT_EQ(queue.size(), 0U);
  EXPECT_EQ(queue.StartOffset(), 105U);
}

TEST(ByteQueueTest, PeekCopiesAcrossChunksWithoutConsuming) {
  ByteQueue queue;
  queue.Append(Chunk({1, 2}));
  queue.Append(Chunk({3, 4}));
  queue.Append(Chunk({5}));
  EXPECT_EQ(queue.Peek(4), (std::vector<uint8_t>{1, 2, 3, 4}));
  EXPECT_EQ(queue.Peek(50), (std::vector<uint8_t>{1, 2, 3, 4, 5}));
  EXPECT_EQ(queue.size(), 5U);
}

TEST(ByteQueueTest, ReadWithinOneChunkSharesItsStorage) {
  ByteQueue queue;
  const penumbra::SharedBytes chunk = Chunk({10, 11, 12, 13, 14, 15});
  queue.Append(chunk);
  queue.Discard(1);
  const std::optional<penumbra::SharedBytes> read = queue.Read(3);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(ToVector(*read), (std::vector<uint8_t>{11, 12, 13}));
  EXPECT_EQ(read->data(), chunk.span().subspan(1).data());
  EXPECT_EQ(queue.StartOffset(), 4U);
}

TEST(ByteQueueTest, ReadAcrossChunksAssemblesACopy) {
  ByteQueue queue;
  queue.Append(Chunk({1, 2}));
  queue.Append(Chunk({3, 4}));
  const std::optional<penumbra::SharedBytes> read = queue.Read(3);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(ToVector(*read), (std::vector<uint8_t>{1, 2, 3}));
  EXPECT_EQ(queue.Peek(5), (std::vector<uint8_t>{4}));
}

TEST(ByteQueueTest, ReadBeyondTheQueueFailsAndZeroIsEmpty) {
  ByteQueue queue;
  queue.Append(Chunk({1}));
  EXPECT_FALSE(queue.Read(2).has_value());
  const std::optional<penumbra::SharedBytes> empty = queue.Read(0);
  ASSERT_TRUE(empty.has_value());
  EXPECT_TRUE(empty->empty());
  EXPECT_EQ(queue.size(), 1U);
}

TEST(ByteQueueTest, ResetDropsEverythingAndMovesTheOffset) {
  ByteQueue queue;
  queue.Append(Chunk({1, 2, 3}));
  queue.Reset(1000);
  EXPECT_EQ(queue.size(), 0U);
  EXPECT_EQ(queue.StartOffset(), 1000U);
  EXPECT_EQ(queue.EndOffset(), 1000U);
  queue.Append(Chunk({9}));
  EXPECT_EQ(queue.Peek(1), (std::vector<uint8_t>{9}));
}

}
}
