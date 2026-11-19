#!/usr/bin/env python3
# Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
"""Package this dependency-free extension as VSIX v2 without an npm download."""
import json
import argparse
import pathlib
import xml.etree.ElementTree as ET
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent
VSIX = "http://schemas.microsoft.com/developer/vsx-schema/2011"
TYPES = "http://schemas.openxmlformats.org/package/2006/content-types"

def main():
    package = json.loads((ROOT / "package.json").read_text())
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path,
                        default=ROOT.parents[1] / 'tmp/js-debugger' / f'{package["name"]}-{package["version"]}.vsix')
    args = parser.parse_args()
    manifest = ET.Element("PackageManifest", {"Version": "2.0.0", "xmlns": VSIX})
    metadata = ET.SubElement(manifest, "Metadata")
    ET.SubElement(metadata, "Identity", {"Language": "en-US", "Id": package["name"],
        "Version": package["version"], "Publisher": package["publisher"]})
    ET.SubElement(metadata, "DisplayName").text = package["displayName"]
    ET.SubElement(metadata, "Description", {"xml:space": "preserve"}).text = package["description"]
    ET.SubElement(metadata, "License").text = "extension/LICENSE"
    ET.SubElement(metadata, "Tags").text = "debugger,javascript,axmol,spidermonkey"
    properties = ET.SubElement(metadata, "Properties")
    ET.SubElement(properties, "Property", {"Id": "Microsoft.VisualStudio.Code.Engine", "Value": package["engines"]["vscode"]})
    ET.SubElement(properties, "Property", {"Id": "Microsoft.VisualStudio.Code.ExecutesCode", "Value": "true"})
    installation = ET.SubElement(manifest, "Installation")
    ET.SubElement(installation, "InstallationTarget", {"Id": "Microsoft.VisualStudio.Code"})
    ET.SubElement(manifest, "Dependencies")
    assets = ET.SubElement(manifest, "Assets")
    for kind, file in [("Microsoft.VisualStudio.Code.Manifest", "package.json"),
                       ("Microsoft.VisualStudio.Services.Content.Details", "README.md"),
                       ("Microsoft.VisualStudio.Services.Content.License", "LICENSE")]:
        ET.SubElement(assets, "Asset", {"Type": kind, "Path": "extension/" + file, "Addressable": "true"})
    types = ET.Element("Types", {"xmlns": TYPES})
    for extension, content_type in [("json", "application/json"), ("js", "application/javascript"),
                                    ("md", "text/markdown"), ("vsixmanifest", "text/xml")]:
        ET.SubElement(types, "Default", {"Extension": extension, "ContentType": content_type})
    ET.SubElement(types, "Override", {"PartName": "/extension/LICENSE", "ContentType": "text/plain"})
    destination = args.output.resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    files = [ROOT / name for name in ("package.json", "README.md", "LICENSE")]
    files += sorted((ROOT / "src").glob("*.js"))
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("extension.vsixmanifest", ET.tostring(manifest, encoding="utf-8", xml_declaration=True))
        archive.writestr("[Content_Types].xml", ET.tostring(types, encoding="utf-8", xml_declaration=True))
        for file in files:
            archive.write(file, "extension/" + file.relative_to(ROOT).as_posix())
    with zipfile.ZipFile(destination) as archive:
        assert archive.testzip() is None
        assert json.loads(archive.read("extension/package.json"))["version"] == package["version"]
    print(destination)

if __name__ == "__main__":
    main()
