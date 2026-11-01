#ifndef UMBRA_BASE_SEQUENCE_CHECKER_H_
#define UMBRA_BASE_SEQUENCE_CHECKER_H_

#include "absl/base/thread_annotations.h"
#include "umbra/base/lock.h"
#include "umbra/base/sequence_token.h"

namespace umbra::base {

class SequenceChecker {
 public:
  SequenceChecker();
  SequenceChecker(const SequenceChecker&) = delete;
  SequenceChecker& operator=(const SequenceChecker&) = delete;

  bool CalledOnValidSequence() const;
  void DetachFromSequence();

 private:
  mutable Lock lock_;
  mutable SequenceToken bound_ ABSL_GUARDED_BY(lock_);
};

}

#endif
