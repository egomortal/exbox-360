"""Build the image-model face detail texture from the original concept."""
from pathlib import Path
import sys
import numpy as np
from PIL import Image

source = Image.open(sys.argv[1]).convert("RGB")
if source.size != (1024, 1536):
    raise ValueError("expected the 1024x1536 original concept cutout")
out_path = Path(sys.argv[2])
crop = source.crop((405, 210, 645, 445)).resize((256, 256), Image.Resampling.LANCZOS)
yy, xx = np.mgrid[0:256, 0:256]
rho = ((xx - 128) / 123)**2 + ((yy - 128) / 128)**2
alpha = np.clip((1.0 - rho) / .20, 0, 1)
alpha[:36] *= np.linspace(0, 1, 36)[:, None]
alpha[-29:] *= np.linspace(1, 0, 29)[:, None]
rgba = np.dstack((np.asarray(crop), np.rint(alpha * 255).astype(np.uint8)))
Image.fromarray(rgba, "RGBA").save(out_path)
print(f"wrote {out_path}")
