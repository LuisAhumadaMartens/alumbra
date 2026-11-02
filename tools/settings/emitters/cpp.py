import json
import math
import re

from emitters.base import EmitError, Emitter

BASE_NAMESPACE = "umbra::settings"
TYPES_INCLUDE = "umbra/settings/setting_types.h"
PLATFORM_INCLUDE = "umbra/settings/platform.h"
INT64_MIN = -(2 ** 63)
INT64_MAX = 2 ** 63 - 1
COLUMN_LIMIT = 80

VALUE_TYPES = {
    "boolean": "bool",
    "integer": "std::int64_t",
    "double": "double",
    "string": "std::string",
    "string_list": "std::vector<std::string>",
    "duration": "absl::Duration",
    "byte_size": "std::uint64_t",
}
VALUE_TYPE_ENUMERATORS = {
    "boolean": "kBoolean",
    "integer": "kInteger",
    "double": "kDouble",
    "enum": "kEnum",
    "string": "kString",
    "string_list": "kStringList",
    "duration": "kDuration",
    "byte_size": "kByteSize",
}
APPLY_ENUMERATORS = {"live": "kLive", "next_load": "kNextLoad",
                     "restart": "kRestart"}
AUDIENCE_ENUMERATORS = {"integrator": "kIntegrator", "end_user": "kEndUser"}
STABILITY_ENUMERATORS = {"stable": "kStable", "experimental": "kExperimental"}
IDENTIFIER_PART = re.compile(r"[A-Za-z0-9]+")


def camel(text):
    parts = IDENTIFIER_PART.findall(text)
    if not parts or "".join(parts) != re.sub(r"[_\-.]", "", text):
        raise EmitError(f"cannot derive a C++ identifier from {text!r}")
    return "".join(part[0].upper() + part[1:] for part in parts)


def integer(value):
    if value == INT64_MIN:
        return "std::numeric_limits<std::int64_t>::min()"
    if value == INT64_MAX:
        return "std::numeric_limits<std::int64_t>::max()"
    return str(value)


def real(value):
    if not math.isfinite(value):
        raise EmitError(f"non-finite double {value!r}")
    text = repr(float(value))
    return text if re.search(r"[.eE]", text) else text + ".0"


def literal(text):
    return json.dumps(text, ensure_ascii=True)


def list_literal(values):
    for value in values:
        if not value or any(ord(char) < 32 or ord(char) == 127 for char in value):
            raise EmitError(f"list value {value!r} is not representable")
    return [literal(value + "\n") for value in values]


def string_constant(indent, head, values):
    pieces = list_literal(values)
    single = f"{indent}{head} = {pieces[0]};"
    if len(pieces) == 1 and len(single) <= COLUMN_LIMIT:
        return [single]
    lines = [f"{indent}{head} ="]
    lines += [f"{indent}    {piece}" for piece in pieces]
    lines[-1] += ";"
    return lines


class CppEmitter(Emitter):
    name = "cpp"
    platform_names = ()

    def render(self, model):
        self.platform_names = model.platforms
        names = self.identifiers(model)
        enums = {setting.key: names[setting.key][1:]
                 for setting in model.settings if setting.type == "enum"}
        outputs = {
            self.options["header_path"]: self.header(model, names, enums),
            self.options["source_path"]: self.source(model, names, enums),
        }
        if "platform_path" in self.options:
            outputs[self.options["platform_path"]] = self.platform(model)
        return outputs

    def identifiers(self, model):
        result = {}
        seen = {}
        for setting in model.settings:
            name = "k" + "".join(camel(part) for part in setting.key.split("."))
            if name in seen:
                raise EmitError(
                    f"{setting.key} and {seen[name]} map to {name}")
            seen[name] = setting.key
            result[setting.key] = name
        return result

    def value_type(self, setting, enums):
        if setting.type == "enum":
            return enums[setting.key]
        return VALUE_TYPES[setting.type]

    def enumerators(self, setting):
        result = {}
        for value in setting.values:
            name = "k" + camel(value)
            if name in result:
                raise EmitError(
                    f"{setting.key}: values {result[name]!r} and {value!r} "
                    f"map to {name}")
            result[name] = value
        return result

    def platform(self, model):
        lines = [
            "#ifndef UMBRA_SETTINGS_PLATFORM_H_",
            "#define UMBRA_SETTINGS_PLATFORM_H_",
            "",
            "#include <cstddef>",
            "#include <cstdint>",
            "",
            "namespace umbra::settings {",
            "",
            "enum class Platform : std::uint8_t {",
        ]
        lines += [f"  k{camel(name)}," for name in model.platforms]
        lines += [
            "};",
            "",
            f"inline constexpr std::size_t kPlatformCount = "
            f"{len(model.platforms)};",
            "",
            "}",
            "",
            "#endif",
            "",
        ]
        return "\n".join(lines)

    def header(self, model, names, enums):
        namespace = self.options["namespace"]
        guard = re.sub(r"[^A-Z0-9]", "_",
                       self.options["include_name"].upper()) + "_"
        used = {setting.type for setting in model.settings}
        system = []
        if used & {"integer", "byte_size"}:
            system.append("<cstdint>")
        if "enum" in used:
            system.append("<string_view>")
        if used & {"string", "string_list"}:
            system.append("<string>")
        if "string_list" in used:
            system.append("<vector>")
        project = [f'"{TYPES_INCLUDE}"']
        if "duration" in used:
            project.insert(0, '"absl/time/time.h"')
        lines = [f"#ifndef {guard}", f"#define {guard}", ""]
        lines += [f"#include {name}" for name in sorted(system)]
        if system:
            lines.append("")
        lines += [f"#include {name}" for name in project]
        lines += ["", f"namespace {namespace} {{", ""]
        for setting in model.settings:
            if setting.type != "enum":
                continue
            lines.append(f"enum class {enums[setting.key]} {{")
            lines += [f"  {name},"
                      for name in self.enumerators(setting)]
            lines += ["};", ""]
        for setting in model.settings:
            kind = self.value_type(setting, enums)
            index = [item.key for item in model.settings].index(setting.key)
            head = f"inline constexpr Key<{kind}>"
            declarator = f"{names[setting.key]}{{{index}}};"
            if len(f"{head} {declarator}") <= COLUMN_LIMIT:
                lines.append(f"{head} {declarator}")
            elif len(f"{head} {names[setting.key]}{{") <= COLUMN_LIMIT:
                lines += [f"{head} {names[setting.key]}{{", f"    {index}}};"]
            elif len(f"    {declarator}") <= COLUMN_LIMIT:
                lines += [head, f"    {declarator}"]
            else:
                lines += [head, f"    {names[setting.key]}{{",
                          f"        {index}}};"]
        lines += ["", "[[nodiscard]] Registry GetRegistry();", "", "}"]
        enum_settings = [item for item in model.settings
                         if item.type == "enum"]
        if enum_settings:
            lines += ["", f"namespace {BASE_NAMESPACE} {{", ""]
            for setting in enum_settings:
                qualified = enums[setting.key]
                if namespace != BASE_NAMESPACE:
                    qualified = (namespace[len(BASE_NAMESPACE) + 2:] + "::"
                                 + qualified)
                lines += ["template <>", f"struct EnumNames<{qualified}> {{"]
                lines += string_constant(
                    "  ", "static constexpr std::string_view kNames",
                    list(self.enumerators(setting).values()))
                lines += ["};", ""]
            lines.append("}")
        lines += ["", "#endif", ""]
        return "\n".join(lines)

    def source(self, model, names, enums):
        namespace = self.options["namespace"]
        lines = []
        system = ["<array>", "<cstddef>", "<string_view>"]
        if any(self.needs_limits(setting) for setting in model.settings):
            system += ["<cstdint>", "<limits>"]
        lines += [f"#include {name}" for name in sorted(system)]
        project = sorted([f'"{TYPES_INCLUDE}"', f'"{PLATFORM_INCLUDE}"',
                          f'"{self.options["include_name"]}"'])
        lines += [""] + [f"#include {name}" for name in project]
        lines += ["", f"namespace {namespace} {{", "namespace {", ""]
        arrays = {}
        for setting in model.settings:
            index = [item.key for item in model.settings].index(setting.key)
            for label, values in (("Allowed", setting.values),
                                  ("Default", self.default_list(setting))):
                if not values:
                    continue
                name = f"k{label}{index}"
                arrays[(setting.key, label)] = name
                lines += string_constant(
                    "", f"constexpr std::string_view {name}", list(values))
                lines.append("")
        lines.append(
            f"constexpr std::array<SettingInfo, {len(model.settings)}> "
            f"kSettings{{{{")
        for setting in model.settings:
            lines += self.entry(setting, arrays)
        lines += ["}};", ""]
        count = len(model.aliases)
        if count:
            lines.append(
                f"constexpr std::array<AliasInfo, {count}> kAliases{{{{")
            keys = [item.key for item in model.settings]
            for alias, target in model.aliases:
                lines += ["    AliasInfo{", f"        .name = {literal(alias)},",
                          f"        .index = {keys.index(target)},", "    },"]
            lines += ["}};", ""]
        else:
            lines += ["constexpr std::array<AliasInfo, 0> kAliases{};", ""]
        lines += ["}", "", "Registry GetRegistry() {",
                  "  return Registry{.settings = kSettings, "
                  ".aliases = kAliases};", "}", "", "}", ""]
        return "\n".join(lines)

    def needs_limits(self, setting):
        return setting.minimum in (INT64_MIN, INT64_MAX) or \
            setting.maximum in (INT64_MIN, INT64_MAX)

    def default_list(self, setting):
        return setting.default if setting.type == "string_list" else []

    def entry(self, setting, arrays):
        kind = setting.type
        numeric = kind in {"integer", "duration", "byte_size", "double"}
        if kind == "double":
            low, high = real(setting.minimum), real(setting.maximum)
            int_low = int_high = "0"
        elif numeric:
            int_low, int_high = (integer(setting.minimum),
                                 integer(setting.maximum))
            low = high = "0.0"
        else:
            int_low = int_high = "0"
            low = high = "0.0"
        default_int = "0"
        default_double = "0.0"
        default_string = '""'
        default_bool = "false"
        if kind == "boolean":
            default_bool = "true" if setting.default else "false"
        elif kind in {"integer", "duration", "byte_size"}:
            default_int = integer(setting.default)
        elif kind == "double":
            default_double = real(setting.default)
        elif kind in {"string", "enum"}:
            default_string = literal(setting.default)
        allowed = arrays.get((setting.key, "Allowed"))
        default_list = arrays.get((setting.key, "Default"))
        fields = [
            ("name", literal(setting.key)),
            ("type", f"ValueType::{VALUE_TYPE_ENUMERATORS[kind]}"),
            ("apply", f"ApplyMode::{APPLY_ENUMERATORS[setting.apply]}"),
            ("audience",
             f"Audience::{AUDIENCE_ENUMERATORS[setting.audience]}"),
            ("stability",
             f"Stability::{STABILITY_ENUMERATORS[setting.stability]}"),
            ("platforms", None),
            ("deprecated", "true" if setting.deprecated else "false"),
            ("replacement", literal(setting.replacement)),
            ("auto_policy", literal(setting.auto_policy)),
            ("min_integer", int_low),
            ("max_integer", int_high),
            ("min_double", low),
            ("max_double", high),
            ("allowed", allowed or '""'),
            ("default_boolean", default_bool),
            ("default_integer", default_int),
            ("default_double", default_double),
            ("default_string", default_string),
            ("default_list", default_list or '""'),
        ]
        lines = ["    SettingInfo{"]
        for name, value in fields:
            if name == "platforms":
                lines.append("        .platforms =")
                lines.append("            PlatformSet{")
                lines += [f"                Platform::k{self.platform_name(item)},"
                          for item in setting.platforms]
                lines.append("            },")
                continue
            lines += self.field(name, value)
        lines.append("    },")
        return lines

    def field(self, name, value):
        line = f"        .{name} = {value},"
        if len(line) <= COLUMN_LIMIT:
            return [line]
        return [f"        .{name} =", f"            {value},"]

    def platform_name(self, index):
        return camel(self.platform_names[index])
