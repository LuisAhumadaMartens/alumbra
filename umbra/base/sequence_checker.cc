#include "umbra/base/sequence_checker.h"

#include "umbra/base/lock.h"
#include "umbra/base/sequence_token.h"

namespace umbra::base {

SequenceChecker::SequenceChecker()
    : bound_(SequenceToken::GetForCurrentThread()) {}

bool SequenceChecker::CalledOnValidSequence() const {
  const SequenceToken current = SequenceToken::GetForCurrentThread();
  AutoLock const lock(lock_);
  if (!bound_.IsValid()) {
    bound_ = current;
    return true;
  }
  return bound_ == current;
}

void SequenceChecker::DetachFromSequence() {
  AutoLock const lock(lock_);
  bound_ = SequenceToken();
}

}
