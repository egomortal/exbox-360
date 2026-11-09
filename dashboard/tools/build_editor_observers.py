# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import sys
from generate_editor_observers import generate,ROOT
from build_editor_layer_candidate import build
out=Path(sys.argv[1]).resolve()
rows=generate(ROOT/'docs/astra/EDITOR-LAYER-SPEC.md',out)
build(out/'observers.cpp',out/'q13-nongpu',[row['symbol'] for row in rows])
