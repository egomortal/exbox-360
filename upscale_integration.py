# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
from ui_model import wsl_path

def upscale_command(project,paths,data,runtime,kind,scale,method,executable,windows=False):
 if kind not in {'ui','text','gradient','photo'} or scale not in {2,4} or method not in {'auto','classic','ai'}:raise ValueError('Choose a supported image type and scale.')
 if windows:
  return ['wsl.exe','--','python3',wsl_path(Path(project)/'tools/upscale_batch.py'),*[wsl_path(p) for p in paths],'--data-root',wsl_path(data),'--runtime-dir',wsl_path(runtime),'--kind',kind,'--scale',str(scale),'--method',method]
 return [str(executable),str(Path(project)/'tools/upscale_batch.py'),*map(str,paths),'--data-root',str(data),'--runtime-dir',str(runtime),'--kind',kind,'--scale',str(scale),'--method',method]
