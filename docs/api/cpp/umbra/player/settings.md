\file umbra/player/settings.h
Typed per-player settings backed by the generated settings registry.

\typedef umbra::player::SettingValue
A value whose alternatives correspond to schema booleans, integers, doubles, strings and string lists. Duration and
byte-size settings use integer values in the units stated by their keys.

\class umbra::player::Settings
The integrator layer for one player. Settings are validated when set. A failed write returns an ErrorDetail and
leaves the prior value in effect.

\fn std::optional<base::ErrorDetail> umbra::player::Settings::Set(std::string_view key, SettingValue value)
Sets a typed integrator value after validating its key, type, enum and range against the generated registry.

\fn std::optional<SettingValue> umbra::player::Settings::Get(std::string_view key) const
Returns the effective typed value for a registered key, or an empty optional for an unknown key.
