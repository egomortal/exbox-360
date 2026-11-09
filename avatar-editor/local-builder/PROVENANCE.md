# Included code and user inputs

This app's original builder/UI code and try-on glue use GPL-3.0-or-later;
see COPYING. External components keep their own notices and terms.

The unchanged try-on sources contain authored delay bookkeeping and wrappers,
API names and numeric layout checks. They contain no original editor assets,
generated guest function bodies, shader packs, fonts or media.

- adapter.cpp: `3282dbf38ce77640b98f8099eaea52394e691ea24c89ac3bac9284d4ec1d0a9a`
- policy.h: `d64e40daf8d33737361d11985416c71c236b5c495439f645f3b377bf58c169e7`
- wearable.h: `52eecdec5c94920936342863f0b762b2b0efc3248be1b215325743c91dff7358`

SDK headers, upstream title source, generated guest code, original editor data
and package artwork are not included. The adapter requires externally generated
ae_pch.h and SDK definitions. Public visibility of an upstream repository is not
treated as permission to redistribute its source under this app's license.

The selected profile is the 125 ms delay plus Sort/Filter on accepted baseline
`89081e85f61f47fafbb3671e00bd3ff4f3d15c48e9d04bdcb03abdc6a7792cc5`.
The upstream AvatarEditorRecomp source revision is
`c7910196da96d0c5b6e3170f77c3c928156a214c`;
ReXGlue SDK 0.10.0 is
`c94f5ebdcb3c9d1a460ca48e04f9758448f8d518`.
Other source revisions and per-file pins are in `external-source-inputs.json`
and the recipe manifests. Baseline binaries are provenance, not build inputs.

Native source-to-package validation is tracked in BUILD-STATUS.md. This is a
source-content inventory, not a certification of an entire toolchain.

The additional Sort/Filter legend is generated locally from the supplied controlp/Legend_Y.png and original XTT font. Its PNG input hash is a1403c004bd70a517c7df65715e9bdbfe15069d1664fbaaf562301f160ede21e. Neither the PNG nor the generated RLE is included in this source download. Legend placement uses the existing bottom-row rendering path; exact retail Y positioning is not claimed.

The static Xbox splash uses the separately supplied 2.0.9199.0 `u/xam/shrdres/splash_360.png` (SHA256 2881b4e94e386d6e15b03dc4e229ac006e3897e36d38b21c14a151198c0081fb). The logo is centered without cropping on an authored white layout, generated locally at 1080p and 4K. Original logo and generated images are absent from this source download.
