import json

from emitters.base import EmitError, Emitter

SCHEMA_SOURCE = "settings/schema/*.toml"
GENERATOR = "tools/settings/generate.py"
INDEX_VERSION = 1

APPLY_LABELS = {
    "live": "Live",
    "next_load": "Next load",
    "restart": "Restart",
}
AUDIENCE_LABELS = {"integrator": "Integrator", "end_user": "End user"}
STABILITY_LABELS = {"stable": "Stable", "experimental": "Experimental"}
PLATFORM_LABELS = {
    "linux": "Linux",
    "macos": "macOS",
    "windows": "Windows",
    "android": "Android",
    "google_tv": "Google TV",
    "ios": "iOS",
    "ipados": "iPadOS",
    "tvos": "tvOS",
    "web": "Web",
}


def namespace_of(key):
    return key.split(".", 1)[0]


def cell(text):
    return (text.replace("\\", "\\\\").replace("|", "\\|")
            .replace("<", "&lt;").replace(">", "&gt;")
            .replace("\n", " "))


def code(text):
    return f"`{text}`"


def default_text(setting):
    if setting.default_state == "unset":
        return "unset"
    value = setting.default
    if setting.type == "boolean":
        return "true" if value else "false"
    if setting.type in ("string", "enum", "string_list"):
        return json.dumps(value, ensure_ascii=True)
    return repr(value) if setting.type == "double" else str(value)


class MarkdownEmitter(Emitter):
    name = "markdown"

    def render(self, model):
        directory = self.options["directory"]
        namespaces = {}
        for setting in model.settings:
            namespaces.setdefault(namespace_of(setting.key), []).append(
                setting)
        outputs = {
            f"{directory}/README.md": self.index(model, namespaces),
            f"{directory}/{self.options['index_name']}":
                self.json_index(model),
        }
        for name, settings in sorted(namespaces.items()):
            outputs[f"{directory}/{name}.md"] = self.page(
                model, name, settings)
        return outputs

    def target(self, key):
        return f"{namespace_of(key)}.md#{key}"

    def url(self, key):
        return f"{self.options['route']}/{namespace_of(key)}/#{key}"

    def notice(self):
        return [
            "> **Generated file. Do not edit.** This page is generated from "
            f"`{SCHEMA_SOURCE}` by `{GENERATOR}`. Change the schema and run "
            f"`python3 {GENERATOR}`; hand edits are overwritten and fail the "
            "drift check.",
            "",
        ]

    def index(self, model, namespaces):
        lines = ["# Settings reference", ""] + self.notice()
        lines += [
            f"Every setting in the schema for release {model.release}, one "
            "page per namespace. Each key has a stable anchor equal to the "
            "key itself, for example `abr.rendition_exclusion_ms`, so a link "
            "of the form `<namespace>.md#<key>` always resolves. A renamed "
            "key keeps its old name as an anchor on the new entry.",
            "",
            f"The same keys are available to tools as `"
            f"{self.options['index_name']}` in this directory.",
            "",
            "| Namespace | Settings | Deprecated |",
            "|-----------|----------|------------|",
        ]
        for name, settings in sorted(namespaces.items()):
            deprecated = sum(1 for setting in settings if setting.deprecated)
            lines.append(f"| [{name}]({name}.md) | {len(settings)} | "
                         f"{deprecated} |")
        lines.append("")
        return "\n".join(lines)

    def json_index(self, model):
        settings = {}
        aliases = {}
        for setting in model.settings:
            settings[setting.key] = {
                "url": self.url(setting.key),
                "type": setting.type,
                "default": None if setting.default_state == "unset"
                else setting.default,
            }
            if setting.deprecated:
                settings[setting.key]["deprecated"] = True
                if setting.replacement:
                    settings[setting.key]["replacement"] = setting.replacement
            for alias, _, _ in setting.aliases:
                aliases[alias] = {"key": setting.key,
                                  "url": self.url(setting.key)}
        document = {
            "schema_version": INDEX_VERSION,
            "release": model.release,
            "settings": settings,
            "aliases": aliases,
        }
        return json.dumps(document, indent=2, sort_keys=True,
                          ensure_ascii=True) + "\n"

    def page(self, model, name, settings):
        lines = [f"# Settings: {name}", ""] + self.notice()
        noun = "setting" if len(settings) == 1 else "settings"
        lines += [f"{len(settings)} {noun} in the `{name}` namespace. "
                  "See the [settings reference index](README.md).", ""]
        for setting in settings:
            lines += self.entry(model, setting)
        return "\n".join(lines)

    def entry(self, model, setting):
        lines = [f'<a id="{setting.key}"></a>']
        for alias, _, _ in setting.aliases:
            lines.append(f'<a id="{alias}"></a>')
        lines += ["", f"## {code(setting.key)}", ""]
        if setting.deprecated:
            notice = f"> **Deprecated since {setting.deprecated_since}.**"
            if setting.replacement:
                notice += (f" Use [{code(setting.replacement)}]"
                           f"({self.target(setting.replacement)}) instead.")
            lines += [notice, ""]
        lines += [cell(setting.description), ""]
        lines += ["| Field | Value |", "|-------|-------|"]
        rows = [
            ("Type", code(setting.type)),
            ("Default", code(default_text(setting))),
        ]
        if setting.explicitly_set:
            rows.append(("Explicit state", "Whether the user supplied a value"))
        if setting.minimum is not None:
            rows.append(("Range", f"{code(str(setting.minimum))} to "
                                  f"{code(str(setting.maximum))}"))
        if setting.unit:
            rows.append(("Unit", code(setting.unit)))
        if setting.auto_policy:
            rows.append(("Automatic value",
                         f"`0` selects {code(setting.auto_policy)}"))
        rows += [
            ("Platforms", ", ".join(
                self.platform_label(model, index)
                for index in setting.platforms)),
            ("Mutability", APPLY_LABELS[setting.apply]),
            ("Audience", AUDIENCE_LABELS[setting.audience]),
            ("Stability", STABILITY_LABELS[setting.stability]),
            ("Since", setting.since),
        ]
        if setting.deprecated:
            rows.append(("Deprecated", setting.deprecated_since))
        for alias, since, removal in setting.aliases:
            text = f"{code(alias)} since {since}"
            if removal:
                text += f", removed in {removal}"
            rows.append(("Previous name", text))
        lines += [f"| {field} | {value} |" for field, value in rows]
        lines.append("")
        if setting.value_docs:
            lines += ["| Value | Meaning |", "|-------|---------|"]
            lines += [f"| {code(value)} | {cell(meaning)} |"
                      for value, meaning in setting.value_docs]
            lines.append("")
        return lines

    def platform_label(self, model, index):
        name = model.platforms[index]
        if name not in PLATFORM_LABELS:
            raise EmitError(f"no display name for platform {name!r}")
        return PLATFORM_LABELS[name]
