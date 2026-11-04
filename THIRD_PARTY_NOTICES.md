# Image runtime notices

The original dashboard/setup code is GPL-3.0-or-later. The optional image runtime uses:

- ncnn, Tencent, BSD 3-Clause, pinned commit 56775de50990ab7f16627efdcf5529b49541206f. Full upstream notices, including third-party notices, are retained in licenses/ncnn-LICENSE.txt. https://github.com/Tencent/ncnn
- Real-ESRGAN, Xintao Wang, BSD 3-Clause; unchanged generic realesr-general-x4v3 v0.2.5.0 weights. Full license in licenses/Real-ESRGAN-LICENSE.txt. https://github.com/xinntao/Real-ESRGAN
- The user-installed Python runtime uses Pillow (HPND license) and NumPy (BSD 3-Clause); retain their distribution notices when redistributing those dependencies. Their packages are not included here.

Source archives, weights, compiled adapters and user images are not part of the code-only source export. No console artwork is bundled or used to train or tune the model.
