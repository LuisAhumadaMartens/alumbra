#include "umbra/base/sequence_checker.h"

#include <thread>

#include "gtest/gtest.h"
#include "umbra/base/sequence_token.h"

namespace umbra::base {
namespace {

bool CheckOnAnotherThread(const SequenceChecker& checker) {
  bool valid = true;
  std::thread other([&] { valid = checker.CalledOnValidSequence(); });
  other.join();
  return valid;
}

TEST(SequenceCheckerTest, BindsToTheConstructingSequence) {
  SequenceChecker checker;
  EXPECT_TRUE(checker.CalledOnValidSequence());
  EXPECT_FALSE(CheckOnAnotherThread(checker));
}

TEST(SequenceCheckerTest, RebindsAfterDetaching) {
  SequenceChecker checker;
  checker.DetachFromSequence();
  EXPECT_TRUE(CheckOnAnotherThread(checker));
  EXPECT_FALSE(checker.CalledOnValidSequence());
}

TEST(SequenceCheckerTest, FollowsTheTaskContextRatherThanTheThread) {
  const SequenceToken sequence = SequenceToken::Create();
  SequenceChecker checker;
  checker.DetachFromSequence();
  {
    ScopedTaskContext context(sequence, std::source_location::current());
    EXPECT_TRUE(checker.CalledOnValidSequence());
  }
  EXPECT_FALSE(checker.CalledOnValidSequence());
  std::thread other([&] {
    ScopedTaskContext context(sequence, std::source_location::current());
    EXPECT_TRUE(checker.CalledOnValidSequence());
  });
  other.join();
}

}
}
