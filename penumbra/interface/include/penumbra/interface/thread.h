#ifndef PENUMBRA_INTERFACE_THREAD_H_
#define PENUMBRA_INTERFACE_THREAD_H_

#include <functional>
#include <memory>
#include <string_view>

namespace penumbra {

class ThreadHandle {
 public:
  virtual ~ThreadHandle() = default;

  virtual void Join() = 0;
};

class ThreadFactory {
 public:
  virtual ~ThreadFactory() = default;

  virtual std::unique_ptr<ThreadHandle> StartThread(
      std::string_view name, std::function<void()> body) = 0;
};

}

#endif
