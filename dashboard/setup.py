"""Populate supported original inputs and optionally build the dashboard locally."""
from pathlib import Path
import sys
if sys.version_info<(3,11):raise SystemExit('Python 3.11 or newer is required')
sys.path.insert(0,str(Path(__file__).resolve().parent/'tools'))
from population.workflow import main

if __name__=='__main__':raise SystemExit(main())
