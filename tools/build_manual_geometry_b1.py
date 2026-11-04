#!/usr/bin/env python3
"""Recreate the saved Bayonetta 1 geometry draft; never replaces the active pack."""
import subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parent.parent
assets=root/'private/avatar/manual-geometry/b1'
for name in ['front_landmarks.py','fit_b1_pose.py','rebuild_b1.py','review_geometry.py']:
    subprocess.run([sys.executable,str(assets/name)],check=True)
print('Geometry draft: '+str(assets/'b1-rebuilt.nxak'))
