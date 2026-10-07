#!/usr/bin/env python3
"""Pin the Homebrew source formula to a release tag and its archive checksum."""
import hashlib
from pathlib import Path
import re
import sys
from urllib.request import urlopen

tag = sys.argv[1] if len(sys.argv) == 2 else ""
if not re.fullmatch(r"v\d+\.\d+\.\d+", tag):
    sys.exit("Usage: scripts/update-formula.py vMAJOR.MINOR.PATCH")
url = f"https://github.com/Frulko/macos-dell-WD22T04-controller/archive/refs/tags/{tag}.tar.gz"
with urlopen(url, timeout=60) as response:
    digest = hashlib.sha256(response.read()).hexdigest()
path = Path("Formula/dockctl.rb")
text = path.read_text()
# Keep the formula's installer/test logic; update only the immutable source pin.
text = re.sub(r'^  (?:url|sha256|version) .*\n', '', text, flags=re.MULTILINE)
text = text.replace('  # Stable URL and SHA256 are inserted by the release workflow.\n', '')
text = text.replace('  license ', f'  url "{url}"\n  sha256 "{digest}"\n  version "{tag[1:]}"\n  license ', 1)
path.write_text(text)
print(f"Pinned dockctl {tag}: {digest}")
