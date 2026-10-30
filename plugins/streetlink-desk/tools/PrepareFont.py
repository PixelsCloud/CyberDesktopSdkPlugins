"""Create a static, renamed OFL display face for legacy DirectWrite collections.

Uses fontTools 4.60.1. No network is needed: the licensed variable source is
already in the repository. The reserved source family name is not reused.
"""

import hashlib
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "build/relwithdebinfo/tools/python"))
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

source = ROOT / "assets/fonts/Aeris/Orbitron-Variable.ttf"
destination = ROOT / "assets/fonts/Streetlink"
destination.mkdir(parents=True, exist_ok=True)
font = instantiateVariableFont(TTFont(source), {"wght": 900}, inplace=True)
names = {
    1: "Streetlink Display",
    2: "Black",
    3: "Streetlink Display Black 1.0",
    4: "Streetlink Display Black",
    6: "StreetlinkDisplay-Black",
    16: "Streetlink Display",
    17: "Black",
}
for key, text in names.items():
    for platform, encoding, language in ((3, 1, 0x409), (1, 0, 0)):
        font["name"].setName(text, key, platform, encoding, language)
font.recalcTimestamp = False
font.save(destination / "StreetlinkDisplay-Black.ttf")
shutil.copyfile(source.parent / "orbitron-OFL.txt", destination / "OFL.txt")
(destination / "sources.json").write_text(
    json.dumps(
        {
            "source": "assets/fonts/Aeris/Orbitron-Variable.ttf",
            "sourceSha256": hashlib.sha256(source.read_bytes()).hexdigest(),
            "license": "SIL Open Font License 1.1",
            "modification": "Static wght=900 instance using fontTools 4.60.1; family renamed Streetlink Display.",
            "reason": "Legacy DirectWrite custom collections select the default variable face otherwise.",
        },
        indent=2,
    )
    + "\n",
    encoding="utf-8",
)
print(destination / "StreetlinkDisplay-Black.ttf")

# The legacy collection also selects Noto's thin default variable instance.
# A static medium face keeps small Chinese UI labels at the intended weight.
source = ROOT / "assets/fonts/Aeris/NotoSansSC-Variable.ttf"
font = instantiateVariableFont(TTFont(source), {"wght": 500}, inplace=True)
for key, text in {
    1: "Streetlink Sans SC",
    2: "Medium",
    3: "Streetlink Sans SC Medium 1.0",
    4: "Streetlink Sans SC Medium",
    6: "StreetlinkSansSC-Medium",
    16: "Streetlink Sans SC",
    17: "Medium",
}.items():
    for platform, encoding, language in ((3, 1, 0x409), (1, 0, 0)):
        font["name"].setName(text, key, platform, encoding, language)
font.recalcTimestamp = False
font.save(destination / "StreetlinkSansSC-Medium.ttf")
shutil.copyfile(source.parent / "noto-sans-sc-OFL.txt", destination / "NotoSansSC-OFL.txt")
(destination / "sans-sources.json").write_text(
    json.dumps(
        {
            "source": "assets/fonts/Aeris/NotoSansSC-Variable.ttf",
            "sourceSha256": hashlib.sha256(source.read_bytes()).hexdigest(),
            "license": "SIL Open Font License 1.1",
            "modification": "Static wght=500 instance using fontTools 4.60.1; renamed Streetlink Sans SC.",
        },
        indent=2,
    )
    + "\n",
    encoding="utf-8",
)
print(destination / "StreetlinkSansSC-Medium.ttf")
