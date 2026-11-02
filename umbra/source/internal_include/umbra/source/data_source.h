#ifndef UMBRA_SOURCE_DATA_SOURCE_H_
#define UMBRA_SOURCE_DATA_SOURCE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "umbra/source/read_request.h"

namespace umbra::source {

struct DataSourceInfo {
  std::optional<uint64_t> size;
  bool seekable = false;
  std::string content_type;
  std::string final_url;
};

class DataSource {
 public:
  DataSource() = default;
  DataSource(const DataSource&) = delete;
  DataSource& operator=(const DataSource&) = delete;
  virtual ~DataSource() = default;

  [[nodiscard]] virtual const DataSourceInfo& Info() const = 0;
  virtual std::shared_ptr<ReadRequest> Read(uint64_t offset, size_t size,
                                            ReadRequest::Callback callback) = 0;
  virtual void Close() = 0;
};

}

#endif
