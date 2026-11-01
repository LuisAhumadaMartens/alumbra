#ifndef UMBRA_BASE_WEAK_PTR_H_
#define UMBRA_BASE_WEAK_PTR_H_

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

#include "umbra/base/sequence_checker.h"

namespace umbra::base {

namespace internal {

class WeakReferenceFlag {
 public:
  WeakReferenceFlag() { sequence_checker_.DetachFromSequence(); }

  bool IsValid() const {
    assert(sequence_checker_.CalledOnValidSequence());
    return valid_.load(std::memory_order_acquire);
  }

  void Invalidate() {
    assert(sequence_checker_.CalledOnValidSequence());
    valid_.store(false, std::memory_order_release);
  }

 private:
  std::atomic<bool> valid_{true};
  SequenceChecker sequence_checker_;
};

}

template <typename T>
class WeakPtrFactory;

template <typename T>
class WeakPtr {
 public:
  WeakPtr() = default;
  ~WeakPtr() = default;
  WeakPtr(std::nullptr_t) {}
  WeakPtr(const WeakPtr&) = default;
  WeakPtr(WeakPtr&&) noexcept = default;

  WeakPtr& operator=(const WeakPtr&) = default;
  WeakPtr& operator=(WeakPtr&&) noexcept = default;

  template <typename U>
    requires std::convertible_to<U*, T*>
  WeakPtr(const WeakPtr<U>& other) : flag_(other.flag_) {
    if (other.resolve_) {
      resolve_ = [resolve = other.resolve_]() -> T* { return resolve(); };
    }
  }

  [[nodiscard]] T* get() const { return flag_ && flag_->IsValid() ? resolve_() : nullptr; }

  T& operator*() const {
    assert(get() != nullptr);
    return *get();
  }

  T* operator->() const {
    assert(get() != nullptr);
    return get();
  }

  explicit operator bool() const { return get() != nullptr; }

  void reset() {
    flag_.reset();
    resolve_ = nullptr;
  }

 private:
  template <typename U>
  friend class WeakPtr;
  friend class WeakPtrFactory<T>;

  WeakPtr(std::shared_ptr<internal::WeakReferenceFlag> flag, T* ptr)
      : flag_(std::move(flag)), resolve_([ptr] { return ptr; }) {}

  std::shared_ptr<internal::WeakReferenceFlag> flag_;
  std::function<T*()> resolve_;
};

template <typename T>
class WeakPtrFactory {
 public:
  explicit WeakPtrFactory(T* owner) : owner_(owner) {}
  WeakPtrFactory(const WeakPtrFactory&) = delete;
  WeakPtrFactory& operator=(const WeakPtrFactory&) = delete;
  ~WeakPtrFactory() { InvalidateWeakPtrs(); }

  WeakPtr<T> GetWeakPtr() {
    if (!flag_) {
      flag_ = std::make_shared<internal::WeakReferenceFlag>();
    }
    return WeakPtr<T>(flag_, owner_);
  }

  void InvalidateWeakPtrs() {
    if (flag_) {
      flag_->Invalidate();
      flag_.reset();
    }
  }

  [[nodiscard]] bool HasWeakPtrs() const { return flag_ && flag_.use_count() > 1; }

 private:
  T* const owner_;
  std::shared_ptr<internal::WeakReferenceFlag> flag_;
};

}

#endif
