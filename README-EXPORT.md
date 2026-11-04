# Ex-Box 360 source review candidate

Licence: GPL-3.0-or-later; preserve each upstream component's notices.
This local review snapshot is not yet a standalone release. No original dashboard,
avatar, font, video, generated guest-code or shader assets are included. Users
supply their own inputs; private/ and inputs/ must never be committed.

The canonical project uses Python3.11+ setup.py for identification/population and
separate local build steps. The GUI calls the same setup entry point. Native
packages and console installation are separate, explicitly authorized work.

Release blockers: AvatarEditorRecomp redistribution permission is unverified;
its source/adaptations remain excluded. Runtime/toolchain dependency pinning and
sibling source closure remain unfinished. Do not publish this candidate as a
working drag-and-drop release. See the canonical docs/astra/GITHUB-AUDIT.md for
local evidence and the full exclusion policy.


Local setup window: run `py -3 launch_setup.py` (Windows) or `ExBox Setup.cmd`.
Drop files/folders, inspect the feature checklist, then Prepare or Build locally.
The window delegates to setup.py; it never moves originals. Windows builds use
WSL Ubuntu-24.04. This review inventory still needs dependency/provenance closure
before it is a portable distributable; see modules/avatar_editor/UPSTREAM.md.
