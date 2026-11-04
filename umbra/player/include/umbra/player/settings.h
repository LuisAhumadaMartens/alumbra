#ifndef UMBRA_PLAYER_SETTINGS_H_
#define UMBRA_PLAYER_SETTINGS_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "umbra/base/error_detail.h"

namespace umbra::player {

using SettingValue = std::variant<bool, std::int64_t, double, std::string,
                                  std::vector<std::string>>;

namespace internal {
class SettingsAccess;
class PlayerAccess;
}

class Settings {
 public:
  Settings(const Settings&) = delete;
  Settings& operator=(const Settings&) = delete;
  ~Settings();

  [[nodiscard]] std::optional<base::ErrorDetail> Set(std::string_view key,
                                                     SettingValue value);
  [[nodiscard]] std::optional<SettingValue> Get(std::string_view key) const;

 private:
  class Impl;
  friend class Player;
  friend class internal::SettingsAccess;
  friend class internal::PlayerAccess;

  explicit Settings(std::unique_ptr<Impl> impl);
  void SetChangeHandler(
      std::function<void(std::string_view, const SettingValue&)> handler);

  std::unique_ptr<Impl> impl_;
};

}

#endif
