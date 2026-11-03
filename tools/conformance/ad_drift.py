import argparse
import hashlib
import json
import pathlib
import re
import sys
import xml.etree.ElementTree as element_tree
import urllib.error
import urllib.request


SOURCES = {
    "iab-vast-inline-simple": {
        "url": "https://raw.githubusercontent.com/InteractiveAdvertisingBureau/VAST_Samples/master/VAST%204.0%20Samples/Inline_Simple.xml",
        "baseline": "59f2fc35741f90c887e3e38ea731f19ff7ec1cb0673a87e5f9c5c1cb327973c6",
    },
    "ima-vast-single-inline-linear": {
        "url": "https://pubads.g.doubleclick.net/gampad/ads?iu=%2F21775744923%2Fexternal%2Fsingle_ad_samples&sz=640x480&cust_params=sample_ct%3Dlinear&ciu_szs=300x250%2C728x90&gdfp_req=1&output=vast&unviewed_position_start=1&env=vp&correlator=123456789",
        "baseline": "81e1c480d24e005e18993a08b48101a2b5b0b2d2735705b7b646687be70aab54",
    },
    "ima-vmap-preroll": {
        "url": "https://pubads.g.doubleclick.net/gampad/ads?iu=%2F21775744923%2Fexternal%2Fvmap_ad_samples&sz=640x480&cust_params=sample_ar%3Dpreonly&ciu_szs=300x250%2C728x90&gdfp_req=1&output=vmap&ad_rule=1&unviewed_position_start=1&env=vp&correlator=123456789",
        "baseline": "7b711385d375788affaa2fb0f6e74594acbc86a6e6038b9ff681aeaea120d575",
    },
}


def element_shape(element):
    attributes = tuple(sorted((key, value) for key, value in element.attrib.items() if key not in {"id", "AdID"}))
    text = (element.text or "").strip()
    if "://" in text or re.search(r"[0-9a-fA-F]{16,}", text):
        text = ""
    children = tuple(element_shape(child) for child in element)
    return element.tag, attributes, text, children


def inspect_source(name, source):
    result = {
        "name": name,
        "url": source["url"],
        "baseline_structure_sha256": source["baseline"],
    }
    request = urllib.request.Request(source["url"], headers={"User-Agent": "Alumbra ad conformance drift check"})
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            body = response.read(2_000_001)
            if len(body) > 2_000_000:
                raise ValueError("response exceeded 2 MiB")
            digest = hashlib.sha256(body).hexdigest()
            result["http_status"] = response.status
            result["response_sha256"] = digest
            result["bytes"] = len(body)
            try:
                root = element_tree.fromstring(body)
                shape = repr(element_shape(root)).encode("utf-8")
                fingerprint = hashlib.sha256(shape).hexdigest()
                result["structure_sha256"] = fingerprint
                result["state"] = "unchanged" if fingerprint == source["baseline"] else "changed"
            except element_tree.ParseError:
                result["state"] = "unparseable"
    except (OSError, urllib.error.URLError, ValueError) as error:
        result["state"] = "unavailable"
        result["error"] = str(error)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=pathlib.Path)
    arguments = parser.parse_args()
    report = {"sources": [inspect_source(name, source) for name, source in SOURCES.items()]}
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    changed = [item["name"] for item in report["sources"] if item["state"] == "changed"]
    unavailable = [item["name"] for item in report["sources"] if item["state"] == "unavailable"]
    print("Changed live ad sources: " + (", ".join(changed) if changed else "none"))
    print("Unavailable live ad sources: " + (", ".join(unavailable) if unavailable else "none"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
