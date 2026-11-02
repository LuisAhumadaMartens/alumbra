import argparse
import datetime
import difflib
import hashlib
import json
import pathlib
import re
import shlex
import subprocess
import sys


BEGIN = "<!-- BEGIN GENERATED VCPKG INVENTORY -->"
END = "<!-- END GENERATED VCPKG INVENTORY -->"
TOKEN = re.compile(r"[A-Za-z0-9.+-]+|[()]")
SPDX_DOC = "vcpkg.spdx.json"
HOST_TOOL = "build-time host tool, not distributed"
UNRESOLVED = frozenset({"NOASSERTION", "NONE", "LicenseRef-vcpkg-null"})
LICENCE_REF = re.compile(r"LicenseRef-[A-Za-z0-9.-]+")
SHA256 = re.compile(r"[0-9a-f]{64}")


class InventoryError(Exception):
    pass


def read_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise InventoryError(f"cannot read {path}: {error}") from error
    except json.JSONDecodeError as error:
        raise InventoryError(f"invalid JSON in {path}: {error}") from error


def read_text(path):
    try:
        return path.read_text(encoding="utf-8")
    except OSError as error:
        raise InventoryError(f"cannot read {path}: {error}") from error


def file_sha256(path):
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        raise InventoryError(f"cannot read {path}: {error}") from error


def fields_from_stanza(stanza):
    fields = {}
    current = None
    for line in stanza.splitlines():
        if line[:1].isspace() and current:
            fields[current] += "\n" + line.strip()
            continue
        key, separator, value = line.partition(":")
        if separator:
            current = key
            fields[key] = value.strip()
    return fields


def dependency_names(value):
    names = []
    for dependency in value.split(","):
        match = re.match(r"\s*([A-Za-z0-9_.+-]+)(?::([A-Za-z0-9_.+-]+))?", dependency)
        if match:
            names.append((match.group(1), match.group(2)))
    return names


def parse_status(path, triplet):
    packages = {}
    for stanza in read_text(path).split("\n\n"):
        fields = fields_from_stanza(stanza)
        name = fields.get("Package")
        if not name or fields.get("Status", "install ok installed") != "install ok installed":
            continue
        architecture = fields.get("Architecture", triplet)
        if architecture != triplet:
            raise InventoryError(
                f"{name}:{architecture} is installed for a different host/target "
                f"triplet than the supported native input {triplet}")
        package = packages.setdefault(name, {
            "version": fields.get("Version"),
            "port_version": fields.get("Port-Version"),
            "dependencies": [],
            "features": set(),
            "description": fields.get("Description", ""),
        })
        if package["version"] != fields.get("Version", package["version"]):
            raise InventoryError(f"conflicting installed versions for {name}:{triplet}")
        if fields.get("Feature"):
            package["features"].add(fields["Feature"])
        package["dependencies"].extend(dependency_names(fields.get("Depends", "")))
    if not packages:
        raise InventoryError(f"no installed packages found in {path}")
    return packages


PLATFORM_TOKEN = re.compile(r"\s*([A-Za-z0-9_-]+|[()!&|])")


def triplet_platform_terms(triplet):
    architecture, _, system = triplet.partition("-")
    terms = {architecture, system.split("-")[0], "static"}
    if architecture == "arm64":
        terms.add("arm")
    return terms


def platform_matches(expression, triplet):
    tokens = PLATFORM_TOKEN.findall(expression)
    if "".join(tokens) != re.sub(r"\s+", "", expression):
        raise InventoryError(f"invalid vcpkg platform expression {expression!r}")
    terms = triplet_platform_terms(triplet)
    position = 0

    def peek():
        return tokens[position] if position < len(tokens) else None

    def take():
        nonlocal position
        position += 1
        return tokens[position - 1]

    def primary():
        token = peek()
        if token is None:
            raise InventoryError(f"invalid vcpkg platform expression {expression!r}")
        take()
        if token == "(":
            value = disjunction()
            if peek() != ")":
                raise InventoryError(f"invalid vcpkg platform expression {expression!r}")
            take()
            return value
        if token == "!":
            return not primary()
        if token in "&|)":
            raise InventoryError(f"invalid vcpkg platform expression {expression!r}")
        return token in terms

    def conjunction():
        value = primary()
        while peek() == "&":
            take()
            value = primary() and value
        return value

    def disjunction():
        value = conjunction()
        while peek() == "|":
            take()
            value = conjunction() or value
        return value

    result = disjunction()
    if position != len(tokens):
        raise InventoryError(f"invalid vcpkg platform expression {expression!r}")
    return result


def manifest_dependency_entries(entries, triplet=None):
    dependencies = {}
    for entry in entries:
        if isinstance(entry, str):
            dependencies.setdefault(entry, set()).add(False)
        elif isinstance(entry, dict) and isinstance(entry.get("name"), str):
            platform = entry.get("platform")
            if triplet is not None and platform and not platform_matches(platform, triplet):
                continue
            dependencies.setdefault(entry["name"], set()).add(entry.get("host") is True)
        else:
            raise InventoryError(f"invalid vcpkg dependency entry: {entry!r}")
    return dependencies


def port_dependency_roles(port_manifest, selected_features):
    result = manifest_dependency_entries(port_manifest.get("dependencies", []))
    features = port_manifest.get("features", {})
    for feature in selected_features:
        feature_data = features.get(feature)
        if feature_data is None:
            continue
        for name, roles in manifest_dependency_entries(
                feature_data.get("dependencies", [])).items():
            result.setdefault(name, set()).update(roles)
    return result


def manifest_roots(manifest, selected_features, triplet=None):
    roots = {}
    for name, roles in manifest_dependency_entries(
            manifest.get("dependencies", []), triplet).items():
        roots.setdefault(name, []).extend(("core", role) for role in roles)
    all_features = manifest.get("features", {})
    for feature in selected_features:
        if feature not in all_features:
            raise InventoryError(f"feature {feature!r} is not declared in vcpkg.json")
        entries = manifest_dependency_entries(
            all_features[feature].get("dependencies", []), triplet)
        for name, roles in entries.items():
            roots.setdefault(name, []).extend((feature, role) for role in roles)
    if not roots:
        raise InventoryError("vcpkg manifest has no dependencies for the requested features")
    return roots


def parse_expression(expression):
    if not isinstance(expression, str) or not expression.strip():
        raise InventoryError("SPDX licence expression is missing")
    tokens = TOKEN.findall(expression)
    if "".join(tokens) != re.sub(r"\s+", "", expression):
        raise InventoryError(f"invalid SPDX licence expression {expression!r}")
    position = 0

    def peek():
        return tokens[position] if position < len(tokens) else None

    def consume(expected=None):
        nonlocal position
        token = peek()
        if token is None or (expected is not None and token != expected):
            raise InventoryError(f"invalid SPDX licence expression {expression!r}")
        position += 1
        return token

    def parse_primary():
        if peek() == "(":
            consume("(")
            node = parse_or()
            consume(")")
            return node
        identifier = consume()
        if identifier in {"AND", "OR", "WITH", ")"}:
            raise InventoryError(f"invalid SPDX licence expression {expression!r}")
        if peek() == "WITH":
            consume("WITH")
            exception = consume()
            if exception in {"AND", "OR", "WITH", "(", ")"}:
                raise InventoryError(f"invalid SPDX licence expression {expression!r}")
            return ("WITH", identifier, exception)
        return ("ID", identifier)

    def parse_and():
        nodes = [parse_primary()]
        while peek() == "AND":
            consume("AND")
            nodes.append(parse_primary())
        return nodes[0] if len(nodes) == 1 else ("AND", *nodes)

    def parse_or():
        nodes = [parse_and()]
        while peek() == "OR":
            consume("OR")
            nodes.append(parse_and())
        return nodes[0] if len(nodes) == 1 else ("OR", *nodes)

    tree = parse_or()
    if position != len(tokens):
        raise InventoryError(f"invalid SPDX licence expression {expression!r}")
    return tree


def format_expression(tree):
    operator = tree[0]
    if operator == "ID":
        return tree[1]
    if operator == "WITH":
        return f"{tree[1]} WITH {tree[2]}"
    precedence = {"OR": 1, "AND": 2}
    parts = []
    for child in tree[1:]:
        value = format_expression(child)
        if child[0] in precedence and precedence[child[0]] < precedence[operator]:
            value = f"({value})"
        parts.append(value)
    return f" {operator} ".join(parts)


def package_features(package):
    return sorted(package["features"])


def port_description(port_manifest):
    description = port_manifest.get("description")
    if isinstance(description, list):
        return next((item for item in description if isinstance(item, str)),
                    "Not specified by vcpkg")
    if isinstance(description, str):
        return description
    return "Not specified by vcpkg"


def package_version(package):
    version = package.get("version")
    if not version:
        raise InventoryError("installed vcpkg status entry is missing its version")
    port_version = package.get("port_version")
    return f"{version}#{port_version}" if port_version not in (None, "", "0") else version


def installed_file_list(installed_root, port, triplet):
    info_dir = installed_root / "vcpkg" / "info"
    files = sorted(info_dir.glob(f"{port}_*_{triplet}.list"))
    if len(files) != 1:
        raise InventoryError(
            f"expected one installed file list for {port}:{triplet} in {info_dir}, "
            f"found {len(files)}")
    return read_text(files[0])


def linkage_from_files(file_list):
    static = False
    dynamic = False
    for line in file_list.splitlines():
        name = pathlib.PurePosixPath(line.strip()).name.lower()
        if name.endswith((".a", ".lib")):
            static = True
        if name.endswith((".dylib", ".dll", ".so")) or re.search(r"\.so\.\d", name):
            dynamic = True
    if static and dynamic:
        return "static and dynamic"
    if dynamic:
        return "dynamic"
    if static:
        return "static"
    return "not applicable"


def markdown_cell(value):
    return " ".join(str(value).replace("|", "\\|").split())


def source_url(port, port_manifest, spdx_path, baseline):
    homepage = port_manifest.get("homepage")
    if isinstance(homepage, str) and homepage.startswith(("https://", "http://")):
        return homepage
    resources_path = spdx_path.parent / "vcpkg-spdx-resources.json"
    if resources_path.is_file():
        resources = read_json(resources_path)
        if isinstance(resources, dict):
            for item in resources.get("packages", []):
                if isinstance(item, dict):
                    location = item.get("downloadLocation")
                    if isinstance(location, str) and location.startswith(
                            ("https://", "git+https://")):
                        return location.removeprefix("git+")
    return f"https://github.com/microsoft/vcpkg/tree/{baseline}/ports/{port}"


def package_expression(spdx_path, port):
    spdx = read_json(spdx_path)
    if not isinstance(spdx, dict):
        raise InventoryError(f"{port}: {spdx_path} must contain an SPDX JSON object")
    if spdx.get("spdxVersion") != "SPDX-2.3":
        raise InventoryError(
            f"{port}: expected SPDX-2.3 in {spdx_path}, got {spdx.get('spdxVersion')!r}")
    packages = spdx.get("packages")
    if not isinstance(packages, list):
        raise InventoryError(f"{port}: {spdx_path} has no SPDX packages list")
    port_packages = [item for item in packages
                     if isinstance(item, dict) and item.get("SPDXID") == "SPDXRef-port"]
    if len(port_packages) != 1 or port_packages[0].get("name") != port:
        raise InventoryError(f"{port}: {spdx_path} does not contain one matching SPDXRef-port")
    return port_packages[0], spdx


def load_inventory(args):
    manifest_path = pathlib.Path(args.manifest)
    manifest = read_json(manifest_path)
    baseline = manifest.get("builtin-baseline")
    if not isinstance(baseline, str) or not re.fullmatch(r"[0-9a-f]{40}", baseline):
        raise InventoryError(f"{manifest_path} must pin a full-SHA builtin-baseline")
    revision = vcpkg_revision(pathlib.Path(args.vcpkg_root))
    if revision != baseline:
        raise InventoryError(
            f"vcpkg checkout is {revision}, but {manifest_path} pins {baseline}")

    installed_root = pathlib.Path(args.installed_root)
    triplet_root = installed_root / args.triplet
    packages = parse_status(installed_root / "vcpkg" / "status", args.triplet)
    spdx_ports = {
        path.parent.name
        for path in (installed_root / args.triplet / "share").glob(f"*/{SPDX_DOC}")
    }
    if spdx_ports != set(packages):
        missing = sorted(set(packages) - spdx_ports)
        extra = sorted(spdx_ports - set(packages))
        details = []
        if missing:
            details.append(f"missing SPDX documents: {', '.join(missing)}")
        if extra:
            details.append(f"SPDX documents for non-installed ports: {', '.join(extra)}")
        raise InventoryError(f"{args.triplet} installed package metadata mismatch: "
                             f"{'; '.join(details)}")
    roots = manifest_roots(manifest, args.feature, args.triplet)
    graph = {}
    for port in packages:
        port_path = pathlib.Path(args.vcpkg_root) / "ports" / port / "vcpkg.json"
        port_manifest = read_json(port_path)
        graph[port] = {
            "manifest": port_manifest,
            "dependency_roles": port_dependency_roles(
                port_manifest, package_features(packages[port])),
        }

    ownership = {}
    roles = {}
    queue = []
    for port, paths in roots.items():
        if port not in packages:
            raise InventoryError(f"manifest dependency {port} is not installed for {args.triplet}")
        for feature, host in paths:
            queue.append((port, feature, "build" if host else "target"))
    visited = set()
    while queue:
        port, feature, role = queue.pop(0)
        state = (port, feature, role)
        if state in visited:
            continue
        visited.add(state)
        ownership.setdefault(port, set()).add(feature)
        roles.setdefault(port, set()).add(role)
        for dependency, dependency_triplet in packages[port]["dependencies"]:
            if dependency_triplet and dependency_triplet != args.triplet:
                raise InventoryError(
                    f"{port} depends on {dependency}:{dependency_triplet}; "
                    f"only native host/target triplets are supported")
            if dependency not in packages:
                raise InventoryError(
                    f"{port} depends on uninstalled package {dependency}:{args.triplet}")
            declarations = graph[port]["dependency_roles"].get(dependency)
            if not declarations:
                raise InventoryError(
                    f"{port} status depends on {dependency}, but its pinned port manifest "
                    "does not declare that dependency")
            for host in declarations:
                next_role = "build" if role == "build" or host else role
                queue.append((dependency, feature, next_role))
    unowned = sorted(set(packages) - set(ownership))
    if unowned:
        raise InventoryError(
            f"installed packages are not reachable from core/selected features: "
            f"{', '.join(unowned)}")

    records = []
    for port in sorted(packages):
        package = packages[port]
        spdx_path = triplet_root / "share" / port / SPDX_DOC
        spdx_package, spdx = package_expression(spdx_path, port)
        version = package_version(package)
        spdx_version = spdx_package.get("versionInfo")
        if spdx_version != version:
            raise InventoryError(
                f"{port}:{args.triplet} version mismatch: vcpkg status has {version}, "
                f"SPDX has {spdx_version!r}")
        copyright_path = triplet_root / "share" / port / "copyright"
        if not copyright_path.is_file():
            raise InventoryError(f"{port}:{args.triplet} is missing {copyright_path}")
        copyright_text = read_text(copyright_path)
        if not copyright_text.strip():
            raise InventoryError(f"{port}:{args.triplet} has empty copyright evidence")
        expression = spdx_package.get("licenseConcluded")
        if expression is not None and not isinstance(expression, str):
            raise InventoryError(
                f"{port}:{args.triplet} has non-string licenseConcluded metadata")
        if expression:
            parse_expression(expression)
        file_list = installed_file_list(installed_root, port, args.triplet)
        upstream = source_url(port, graph[port]["manifest"], spdx_path, baseline)
        records.append({
            "port": port,
            "version": version,
            "expression": expression,
            "expression_present": "licenseConcluded" in spdx_package,
            "linkage": linkage_from_files(file_list),
            "features": sorted(ownership[port]),
            "roles": sorted(roles[port]),
            "port_features": package_features(package),
            "purpose": port_description(graph[port]["manifest"]),
            "upstream": upstream,
            "copyright": copyright_text,
            "copyright_sha256": file_sha256(copyright_path),
            "port_manifest": graph[port]["manifest"],
            "triplet": args.triplet,
            "baseline": baseline,
        })
    return records, baseline


def vcpkg_revision(root):
    result = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "HEAD"],
        capture_output=True, text=True, check=False)
    if result.returncode:
        raise InventoryError(f"cannot read vcpkg checkout revision at {root}: {result.stderr.strip()}")
    return result.stdout.strip()


def review_match(record, entry):
    return (
        entry.get("port") == record["port"]
        and entry.get("version") == record["version"]
        and entry.get("expression") == record["expression"]
        and entry.get("expression_present") == record["expression_present"]
        and entry.get("baseline") == record["baseline"]
        and entry.get("port_features") == record["port_features"]
        and record["triplet"] in entry.get("triplets", [])
        and entry.get("owner_features") == record["features"]
        and entry.get("roles") == record["roles"]
        and entry.get("linkage") == record["linkage"]
        and bool(entry.get("review"))
        and bool(entry.get("evidence"))
    )


def reviewed_entry(record, policy):
    return next((entry for entry in policy.get("reviewed_exceptions", [])
                 if review_match(record, entry)), None)


def or_choice(record, expression, policy):
    return next((entry for entry in policy.get("or_choices", [])
                 if entry.get("port") == record["port"]
                 and entry.get("version") == record["version"]
                 and entry.get("expression") == expression
                 and entry.get("expression_present") == record["expression_present"]
                 and entry.get("baseline") == record["baseline"]
                 and entry.get("port_features") == record["port_features"]
                 and record["triplet"] in entry.get("triplets", [])
                 and entry.get("owner_features") == record["features"]
                 and entry.get("roles") == record["roles"]
                 and entry.get("linkage") == record["linkage"]
                 and bool(entry.get("review"))
                 and bool(entry.get("evidence"))), None)


def denied_rule(identifier, policy):
    for denied in policy.get("denied", []):
        if re.search(denied["pattern"], identifier, re.IGNORECASE):
            return denied["rule"]
    return None


def select_or_branch(tree, record, policy, errors):
    expression = format_expression(tree)
    choice = or_choice(record, expression, policy)
    if not choice:
        errors.append(
            f"{record['port']}:{record['triplet']}: SPDX OR expression "
            f"{expression} has no recorded branch choice under policy rule "
            "'review-spdx-or-choice'")
        return None
    branches = {format_expression(child): child for child in tree[1:]}
    selected = choice.get("selected")
    if selected not in branches:
        errors.append(
            f"{record['port']}:{record['triplet']}: recorded SPDX OR choice "
            f"{selected!r} is not a branch of {expression}")
        return None
    return branches[selected]


def reviewed_obligations(tree, record, policy, errors):
    kind = tree[0]
    if kind == "ID":
        rule = denied_rule(tree[1], policy)
        if rule:
            errors.append(
                f"{record['port']}:{record['triplet']}: SPDX identifier {tree[1]} "
                f"is denied by policy rule '{rule}'")
            return None
        if tree[1] in policy.get("allowed", []) or tree[1] in policy.get("conditional", {}):
            return {tree[1]}
        return set()
    if kind == "WITH":
        return reviewed_obligations(("ID", tree[1]), record, policy, errors)
    if kind == "AND":
        obligations = set()
        for child in tree[1:]:
            child_obligations = reviewed_obligations(child, record, policy, errors)
            if child_obligations is None:
                return None
            obligations.update(child_obligations)
        return obligations
    branch = select_or_branch(tree, record, policy, errors)
    if branch is None:
        return None
    return reviewed_obligations(branch, record, policy, errors)


def active_expression_terms(tree, record, policy, errors):
    kind = tree[0]
    if kind == "ID":
        return {tree[1]}
    if kind == "WITH":
        return {tree[1]}
    if kind == "AND":
        terms = set()
        for child in tree[1:]:
            child_terms = active_expression_terms(child, record, policy, errors)
            if child_terms is None:
                return None
            terms.update(child_terms)
        return terms
    branch = select_or_branch(tree, record, policy, errors)
    if branch is None:
        return None
    return active_expression_terms(branch, record, policy, errors)


def evaluate_expression(tree, record, policy, errors, reviewed, reviewed_terms=frozenset()):
    kind = tree[0]
    if kind == "ID":
        identifier = tree[1]
        rule = denied_rule(identifier, policy)
        if rule:
            errors.append(
                f"{record['port']}:{record['triplet']}: SPDX identifier {identifier} "
                f"is denied by policy rule '{rule}'")
            return False
        if identifier in policy.get("allowed", []):
            return True
        if identifier in reviewed_terms:
            return True
        conditional = policy.get("conditional", {}).get(identifier)
        if conditional:
            if conditional.get("review_required", True) and not reviewed:
                errors.append(
                    f"{record['port']}:{record['triplet']}: {identifier} requires "
                    "a version-specific review under policy rule "
                    f"'{conditional['rule']}'")
                return False
            if conditional.get("features") and not set(record["features"]).issubset(
                    set(conditional["features"])):
                errors.append(
                    f"{record['port']}:{record['triplet']}: {identifier} is not allowed "
                    "for its owning feature under policy rule "
                    f"'{conditional['rule']}'")
                return False
            if conditional.get("linkage") and record["linkage"] != conditional["linkage"]:
                errors.append(
                    f"{record['port']}:{record['triplet']}: {identifier} requires "
                    f"{conditional['linkage']} linkage under policy rule "
                    f"'{conditional['rule']}', found {record['linkage']}")
                return False
            if conditional.get("triplets") and record["triplet"] not in conditional["triplets"]:
                errors.append(
                    f"{record['port']}:{record['triplet']}: {identifier} is not permitted "
                    f"on this target under policy rule '{conditional['rule']}'")
                return False
            return True
        if identifier in UNRESOLVED:
            errors.append(
                f"{record['port']}:{record['triplet']}: unresolved SPDX licence "
                f"{identifier} is blocked by policy rule 'block-unresolved-licences'")
            return False
        errors.append(
            f"{record['port']}:{record['triplet']}: unlisted SPDX identifier {identifier} "
            "is blocked by policy rule 'block-unlisted-licences'")
        return False
    if kind == "WITH":
        errors.append(
            f"{record['port']}:{record['triplet']}: SPDX WITH exception "
            f"{tree[1]} WITH {tree[2]} needs a recorded review under policy rule "
            "'review-spdx-exceptions'")
        return False
    if kind == "AND":
        accepted = True
        for child in tree[1:]:
            if not evaluate_expression(child, record, policy, errors, reviewed, reviewed_terms):
                accepted = False
        return accepted
    branch = select_or_branch(tree, record, policy, errors)
    if branch is None:
        return False
    return evaluate_expression(branch, record, policy, errors, reviewed, reviewed_terms)


def non_empty_string(value):
    return isinstance(value, str) and bool(value.strip())


def host_tool_review_matches(record, entry):
    if record["roles"] != ["build"] or entry.get("roles") != ["build"]:
        return False, (f"classification {HOST_TOOL!r} requires a build-only dependency "
                       f"path, found roles {record['roles']}")
    for field in ("reviewer", "decision", "obligations"):
        if not non_empty_string(entry.get(field)):
            return False, f"classification {HOST_TOOL!r} requires a recorded {field}"
    try:
        datetime.date.fromisoformat(entry.get("reviewed_on", ""))
    except (TypeError, ValueError):
        return False, f"classification {HOST_TOOL!r} requires an ISO reviewed_on date"
    expected = entry.get("copyright_sha256")
    if not isinstance(expected, str) or not SHA256.fullmatch(expected):
        return False, f"classification {HOST_TOOL!r} requires the reviewed copyright_sha256"
    if record.get("copyright_sha256") != expected:
        return False, (f"installed copyright SHA-256 {record.get('copyright_sha256')} "
                       f"does not match the reviewed {expected}")
    return True, ""


def licence_texts_match(record, entry):
    texts = entry.get("licence_texts", {})
    if not isinstance(texts, dict):
        return False, "licence_texts must map LicenseRef identifiers to reviewed texts"
    if texts and entry.get("classification") != HOST_TOOL:
        return False, (f"recorded licence texts are accepted only for classification "
                       f"{HOST_TOOL!r}")
    for identifier, licence in sorted(texts.items()):
        if not LICENCE_REF.fullmatch(identifier) or identifier in UNRESOLVED:
            return False, f"{identifier!r} is not a reviewable LicenseRef identifier"
        if not isinstance(licence, dict) or not non_empty_string(licence.get("text")):
            return False, f"{identifier} has no recorded licence text"
        source = licence.get("source")
        if not isinstance(source, str) or not source.startswith("https://"):
            return False, f"{identifier} has no https source for its licence text"
        digest = hashlib.sha256(licence["text"].encode("utf-8")).hexdigest()
        if digest != licence.get("sha256"):
            return False, f"{identifier} text does not match its recorded SHA-256"
        if licence["text"] not in record["copyright"]:
            return False, f"installed copyright lacks the reviewed {identifier} text"
    return True, ""


def reviewed_licence_terms(entry):
    return frozenset(entry.get("licence_texts", {}))


def exception_evidence_matches(record, entry, vcpkg_root):
    expected_manifest_license = entry.get("expected_manifest_license", "not-specified")
    if expected_manifest_license != "not-specified":
        manifest_path = vcpkg_root / "ports" / record["port"] / "vcpkg.json"
        port_manifest = read_json(manifest_path)
        if port_manifest.get("license", "missing") != expected_manifest_license:
            return False, f"{manifest_path} license field does not match the reviewed exception"
    expected_features = entry.get("port_features")
    if expected_features is not None and record["port_features"] != expected_features:
        return False, (
            f"selected vcpkg features {record['port_features']} do not match "
            f"the reviewed set {expected_features}")
    markers = entry.get("copyright_contains", [])
    for marker in markers:
        if marker not in record["copyright"]:
            return False, f"copyright file lacks reviewed evidence {marker!r}"
    if entry.get("upstream") != record["upstream"]:
        return False, "upstream URL does not match the reviewed exception"
    classification = entry.get("classification")
    if classification is not None:
        if classification != HOST_TOOL:
            return False, f"unknown reviewed classification {classification!r}"
        valid, reason = host_tool_review_matches(record, entry)
        if not valid:
            return False, reason
    return licence_texts_match(record, entry)


def policy_failures(records, policy, vcpkg_root):
    errors = []
    for record in records:
        expression = record["expression"]
        review = reviewed_entry(record, policy)
        if expression is None:
            if not review:
                marker = "null" if record["expression_present"] else "missing"
                errors.append(
                    f"{record['port']}:{record['triplet']}: package licenseConcluded is "
                    f"{marker} and blocked by policy rule 'require-effective-licenseConcluded'")
                continue
            valid, reason = exception_evidence_matches(record, review, vcpkg_root)
            if not valid:
                errors.append(
                    f"{record['port']}:{record['triplet']}: reviewed exception evidence "
                    f"failed validation: {reason}")
                continue
            resolved_expression = review.get("resolved_expression")
            try:
                resolved = parse_expression(resolved_expression)
            except InventoryError as error:
                errors.append(
                    f"{record['port']}:{record['triplet']}: invalid reviewed terms: {error}")
                continue
            evaluate_expression(
                resolved, record, policy, errors, True, reviewed_licence_terms(review))
            continue
        try:
            tree = parse_expression(expression)
        except InventoryError as error:
            errors.append(
                f"{record['port']}:{record['triplet']}: {error} under policy rule "
                "'require-valid-spdx-expression'")
            continue
        if review:
            obligations = reviewed_obligations(tree, record, policy, errors)
            if obligations is None:
                continue
            valid, reason = exception_evidence_matches(record, review, vcpkg_root)
            if not valid:
                errors.append(
                    f"{record['port']}:{record['triplet']}: reviewed exception evidence "
                    f"failed validation: {reason}")
                continue
            resolved_expression = review.get("resolved_expression", expression)
            try:
                resolved = parse_expression(resolved_expression)
            except InventoryError as error:
                errors.append(
                    f"{record['port']}:{record['triplet']}: invalid reviewed terms: {error}")
                continue
            active_terms = active_expression_terms(
                resolved, record, policy, errors)
            if active_terms is None:
                continue
            missing = obligations - active_terms
            if missing:
                errors.append(
                    f"{record['port']}:{record['triplet']}: reviewed terms remove "
                    f"required original SPDX obligations {', '.join(sorted(missing))} "
                    "under policy rule 'preserve-original-license-obligations'")
                continue
            evaluate_expression(
                resolved, record, policy, errors, True, reviewed_licence_terms(review))
            continue
        evaluate_expression(tree, record, policy, errors, False)
    return errors


def render_triplet(records, baseline, triplet, policy, features):
    selected_roots = ["core", *sorted(set(features))]
    lines = [
        f"### `{triplet}`",
        "",
        f"Manifest baseline: `{baseline}`; selected dependency roots: "
        f"{', '.join(f'`{feature}`' for feature in selected_roots)}.",
        "Rows include the complete installed dependency closure. Linkage is detected from the "
        "installed package file list (`.a`/`.lib` versus `.dylib`/`.so`), not inferred from "
        "the triplet. `build tool` denotes host/build-only dependency paths; it is not a "
        "claim that the package is linked into an Alumbra artefact.",
        "",
        "| Port | Version | SPDX expression / reviewed terms | Installed linkage | Owning feature | Role | Purpose | Upstream/source |",
        "|------|---------|-----------------------------------|-------------------|----------------|------|---------|-----------------|",
    ]
    for record in records:
        expression = record["expression"]
        review = reviewed_entry(record, policy)
        if expression is None:
            marker = "null" if record["expression_present"] else "missing"
            expression = f"licenseConcluded {marker}"
            if not review:
                expression += " (unresolved; policy check fails)"
        elif expression in UNRESOLVED and not review:
            expression += " (unresolved; policy check fails)"
        if review:
            terms = review.get("selected_terms", review.get("resolved_expression"))
            classification = review.get("classification")
            if classification:
                terms = f"{terms}; {classification}"
            expression += f" (reviewed as {terms}; {review.get('review')})"
        elif expression and " OR " in expression:
            choice = or_choice(record, expression, policy)
            if choice:
                expression += f" (selected {choice['selected']}; {choice['review']})"
        role = {
            ("build",): "build tool",
            ("target",): "target dependency",
            ("build", "target"): "target and build dependency",
            ("target", "build"): "target and build dependency",
        }.get(tuple(record["roles"]), " / ".join(record["roles"]))
        url = record["upstream"]
        lines.append(
            f"| `{markdown_cell(record['port'])}` | `{markdown_cell(record['version'])}` | "
            f"`{markdown_cell(expression)}` | {markdown_cell(record['linkage'])} | "
            f"{markdown_cell(', '.join(record['features']))} | {role} | "
            f"{markdown_cell(record['purpose'])} | "
            f"[source]({url}) |")
    return "\n".join(lines)


def replace_inventory_section(document, triplet_section, triplet):
    if document.count(BEGIN) != 1 or document.count(END) != 1:
        raise InventoryError(
            f"THIRD_PARTY.md must contain exactly one {BEGIN} and {END} marker")
    start = document.index(BEGIN) + len(BEGIN)
    end = document.index(END)
    if start >= end:
        raise InventoryError("THIRD_PARTY.md generated inventory markers are reversed")
    current = document[start:end].strip("\n")
    section_pattern = re.compile(r"(?ms)^### `([^`]+)`\n.*?(?=^### `|\Z)")
    sections = {match.group(1): match.group(0).strip("\n")
                for match in section_pattern.finditer(current)}
    sections[triplet] = triplet_section
    rendered = "\n\n" + "\n\n".join(sections[name] for name in sorted(sections)) + "\n"
    return document[:start] + rendered + document[end:]


def command_arguments(args, command):
    arguments = [
        "python3", "tools/ci/third_party.py", command,
        "--triplet", args.triplet,
        "--installed-root", args.installed_root,
        "--vcpkg-root", args.vcpkg_root,
        "--manifest", args.manifest,
        "--policy", args.policy,
        "--output", args.output,
    ]
    for feature in args.feature:
        arguments.extend(("--feature", feature))
    return " ".join(shlex.quote(argument) for argument in arguments)


def generate(args):
    records, baseline = load_inventory(args)
    output = pathlib.Path(args.output)
    document = read_text(output)
    policy = read_json(pathlib.Path(args.policy))
    section = render_triplet(records, baseline, args.triplet, policy, args.feature)
    updated = replace_inventory_section(document, section, args.triplet)
    try:
        output.write_text(updated, encoding="utf-8")
    except OSError as error:
        raise InventoryError(f"cannot write {output}: {error}") from error
    print(f"Updated {output} for {args.triplet} ({len(records)} installed ports).")


def check(args):
    records, baseline = load_inventory(args)
    output = pathlib.Path(args.output)
    document = read_text(output)
    policy = read_json(pathlib.Path(args.policy))
    section = render_triplet(records, baseline, args.triplet, policy, args.feature)
    updated = replace_inventory_section(document, section, args.triplet)
    if updated != document:
        diff = difflib.unified_diff(
            document.splitlines(), updated.splitlines(),
            fromfile=str(output), tofile=f"{output} (regenerated)", lineterm="")
        print("\n".join(diff), file=sys.stderr)
        print(
            f"{output} is stale for {args.triplet}.\n"
            f"Regenerate with:\n  {command_arguments(args, 'generate')}",
            file=sys.stderr)
        return 1
    print(f"{output} is current for {args.triplet} ({len(records)} installed ports).")
    return 0


def check_policy(args):
    records, _ = load_inventory(args)
    policy = read_json(pathlib.Path(args.policy))
    errors = policy_failures(records, policy, pathlib.Path(args.vcpkg_root))
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"Licence policy passed for {len(records)} installed ports on {args.triplet}.")
    return 0


def build_parser():
    parser = argparse.ArgumentParser(description="Generate and check Alumbra's vcpkg inventory.")
    parser.add_argument("command", choices=("generate", "check", "policy"))
    parser.add_argument("--triplet", required=True)
    parser.add_argument("--installed-root", required=True)
    parser.add_argument("--vcpkg-root", required=True)
    parser.add_argument("--manifest", default="vcpkg.json")
    parser.add_argument("--policy", default="tools/ci/licence_policy.json")
    parser.add_argument("--output", default="THIRD_PARTY.md")
    parser.add_argument("--feature", action="append", default=[])
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        if args.command == "generate":
            generate(args)
            return 0
        if args.command == "check":
            return check(args)
        return check_policy(args)
    except InventoryError as error:
        print(f"third_party.py: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
