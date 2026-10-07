#!/bin/sh
# Build a relocatable archive. This script never talks to the dock.
set -eu
version=${1:?Usage: scripts/package.sh VERSION}
case "$version" in *[!0-9A-Za-z.-]*|'') echo "Invalid version" >&2; exit 2;; esac
arch=$(uname -m)
case "$arch" in arm64|x86_64) ;; *) echo "Unsupported architecture" >&2; exit 2;; esac
make all
bundle="dist/dockctl-${version}-macos-${arch}"
mkdir -p "$bundle/bin" "$bundle/lib" "$bundle/include" "$bundle/share/licenses"
cp dockctl "$bundle/bin/"
cp libdock.a libdock.dylib "$bundle/lib/"
cp include/dock.h include/module.modulemap "$bundle/include/"
cp README.md LICENSE NOTICE "$bundle/share/"
cp research/macvdmtool/LICENSE "$bundle/share/licenses/Apache-2.0.txt"
# Ad-hoc signing supplies an integrity signature, not Apple notarization.
codesign --force --sign - "$bundle/bin/dockctl"
codesign --force --sign - "$bundle/lib/libdock.dylib"
tar -czf "${bundle}.tar.gz" -C dist "$(basename "$bundle")"
(cd dist && shasum -a 256 "$(basename "$bundle").tar.gz" > "$(basename "$bundle").tar.gz.sha256")
