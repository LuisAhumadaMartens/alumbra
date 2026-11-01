#ifndef PENUMBRA_INTERFACE_SHARED_BYTES_H_
#define PENUMBRA_INTERFACE_SHARED_BYTES_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace penumbra {

class SharedBytes {
 public:
  SharedBytes() = default;
  SharedBytes(const SharedBytes&) = default;
  SharedBytes& operator=(const SharedBytes&) = default;
  SharedBytes(SharedBytes&& other) noexcept
      : data_(std::move(other.data_)), size_(std::exchange(other.size_, 0)) {}
  SharedBytes& operator=(SharedBytes&& other) noexcept {
    data_ = std::move(other.data_);
    size_ = std::exchange(other.size_, 0);
    return *this;
  }

  static SharedBytes Adopt(std::vector<uint8_t> bytes) {
    auto storage =
        std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
    const uint8_t* data = storage->data();
    const size_t size = storage->size();
    return SharedBytes(std::shared_ptr<const uint8_t>(std::move(storage), data),
                       size);
  }

  static SharedBytes Copy(std::span<const uint8_t> bytes) {
    return Adopt(std::vector<uint8_t>(bytes.begin(), bytes.end()));
  }

  static SharedBytes Wrap(std::shared_ptr<const void> owner,
                          std::span<const uint8_t> bytes) {
    return SharedBytes(
        std::shared_ptr<const uint8_t>(std::move(owner), bytes.data()),
        bytes.size());
  }

  std::optional<SharedBytes> Slice(size_t offset, size_t size) const {
    if (offset > size_ || size > size_ - offset) {
      return std::nullopt;
    }
    return SharedBytes(
        std::shared_ptr<const uint8_t>(data_, data_.get() + offset), size);
  }

  const uint8_t* data() const { return data_.get(); }
  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  std::span<const uint8_t> span() const { return {data_.get(), size_}; }

 private:
  SharedBytes(std::shared_ptr<const uint8_t> data, size_t size)
      : data_(std::move(data)), size_(size) {}

  std::shared_ptr<const uint8_t> data_;
  size_t size_ = 0;
};

}

#endif
