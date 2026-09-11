"""Generate upstream build inputs; do not rewrite upstream sources."""
from pathlib import Path
import subprocess
import hashlib
Import("env")
port = Path(env.subst("$PROJECT_DIR"))
root = port.parent.parent
generated = port / "generated"
generated.mkdir(exist_ok=True)
apps = generated / "displayapp/apps/Apps.h"
apps.parent.mkdir(parents=True, exist_ok=True)
template = (root / "src/displayapp/apps/Apps.h.in").read_text()
apps.write_text(template.replace("@USERAPP_TYPES@", "Apps::Calculator, Apps::StopWatch, Apps::Twos")
               .replace("@WATCHFACE_TYPES@", "WatchFace::Analog"))
converter = port / "tools/node_modules/.bin/lv_font_conv"
if not converter.exists():
    raise RuntimeError("Run npm ci --prefix tools before building")
fonts = root / "src/displayapp/fonts"
font_inputs = [fonts / "generate.py", fonts / "fonts.json", port / "tools/package-lock.json"]
font_inputs += sorted(path for path in fonts.rglob("*") if path.suffix in (".ttf", ".woff", ".woff2"))
fingerprint = hashlib.sha256(b"".join(path.read_bytes() for path in font_inputs)).hexdigest()
stamp = generated / ".fonts.sha256"
if not stamp.exists() or stamp.read_text() != fingerprint:
    subprocess.run([env.subst("$PYTHONEXE"), str(fonts / "generate.py"),
                    "--lv-font-conv", str(converter), str(fonts / "fonts.json")],
                   cwd=generated, check=True)
    stamp.write_text(fingerprint)
env.Append(CXXFLAGS=["-include", str(apps), "-include", str(root / "src/displayapp/Controllers.h")])
