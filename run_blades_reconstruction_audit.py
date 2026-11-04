"""Blades-only convenience entry point; uses the shared native audit runner."""
from pathlib import Path
import subprocess, sys
root=Path(__file__).resolve().parent
arguments=sys.argv[1:]
if '--only' not in arguments and '--video' not in arguments:
    arguments=['--only','blades-games,blades-live,blades-media,blades-system,blades-1888-games,blades-1888-live,blades-1888-media,blades-1888-system,blades-marketplace,blades-tray,blades-tray-return',*arguments]
raise SystemExit(subprocess.call([sys.executable,str(root/'run_era_fidelity_audit.py'),'--output',str(root/'build/blades-reconstruction-20261025'),*arguments]))