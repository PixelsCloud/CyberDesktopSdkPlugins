# Third-party notices

CyberCanvas Noise uses FastNoiseLite 1.1.1 (MIT), tag commit `7ccfbc16eb1c932568f177d63a9ba51d89bbe516`,
from https://github.com/Auburn/FastNoiseLite through `overlay-ports/fastnoiselite`. The upstream header is compiled
privately into `cybercanvas_noise.lib` with `/MT`. Its original notice ships at
`share/cybercanvas/third_party/fastnoiselite/LICENSE`; consuming plugin packages must include it. The archive SHA512
is pinned in the portfile. This optional module adds no runtime DLL.

CyberCanvas automatic layout uses Yoga 3.2.1 (MIT), obtained through vcpkg and linked statically with `/MT`. Installed kits bundle `cybercanvas-yoga.lib` and its original license at `share/cybercanvas/third_party/yoga/LICENSE`. Plugins using automatic layout must ship that notice. No Yoga runtime DLL is required.

SDK source, native templates and examples are MIT licensed by CyberDesktop contributors. Windows APIs are provided by the operating system. No private desktop renderer, collector, FFT implementation or wallpaper resources are copied into the SDK. Default font assets and preview resources are listed separately below.

Tool/package-runtime dependencies are supplied by vcpkg and linked statically: protobuf C++ 6.33.4#2 / protoc 33.4, Abseil 20260107.1#3 (Apache-2.0), and utf8-range 6.33.4 (MIT). The protobuf runtime is BSD-3-Clause. Exact versions/baseline are in `release.json` and `vcpkg.json`. Their original licenses are in `third_party/*-LICENSE.txt` and must accompany binary releases. Regenerate the dependency/version manifest from the actual resolved toolchain when building a release. These notices do not relicense those dependencies as MIT.

Tweeny easing uses its MIT-licensed header; its original notice and provenance ship in `share/cybercanvas/third_party/tweeny`. The seven starter projects stage the SDK, Yoga and tweeny notices with their native plugin output.


## Public preview and plugin resources

| Resource | Provenance and delivery |
| --- | --- |
| Lucide | Original SVGs and ISC/MIT notices from `libraries/cyberglass/resources/lucide`; embedded preview icons use ThorVG. Installed notice: `share/cyberdesktop-sdk/third_party/lucide/LICENSE`. |
| ThorVG | Existing vcpkg CPU SVG/Lottie library, MIT; original notice installed with the SDK and CyberCanvas. |
| toml++ | Existing vcpkg TOML parser, MIT; full-preview notice installed at `third_party/tomlplusplus/LICENSE`. |
| Qt | Full preview only, Core/Gui/Network dynamic libraries from the selected Qt installation. Original distribution notices are copied from `CD_SDK_QT_NOTICES_DIR`; this does not grant an additional Qt license. |
| Cubism | Full preview only; externally supplied Native SDK. Core/framework licenses and notices accompany binaries. Model files retain their package notices. External Cubism source is excluded from the candidate. |
| Gen Shin Gothic Heavy | Original name-table attribution and SIL OFL 1.1 in `assets/fonts/SiYuanZhenHei-Heavy/`; data is unchanged. Default SDK font identifier `@siyuan-heavy`. |
| ZQKfreefont | Original author 郑庆科 / 济南一字百炼艺术设计有限公司, declared free for commercial use in the font and [author release](https://www.zcool.com.cn/work/ZNjg2OTA5OTI=.html). `assets/fonts/ZQKFreeFont/NOTICE.txt` records that statement; it is not MIT/OFL. The project owner requested retaining this font on 2026-10-30 and is handling its redistribution permission separately; no additional license is inferred here. Default identifier `@zqk-free`. |
| Aeris / DeepWhale / Streetlink fonts | Original OFL files and available source metadata remain adjacent to each font family in `assets/fonts/`. Installed only with the relevant theme dependency closure. |
| sxtwl | BSD 3-Clause, copyright 元谷; original notice in `third_party/sxtwl/LICENSE`. |
| First-party artwork | Theme resources retain their existing provenance and notices and accompany the plugins. `design/` working files remain private and excluded. |

Default fonts are staged next to preview tools and under the plugin distribution's shared `assets/fonts`. The font contract loads actual DirectWrite collections and fails on silent system-font fallback. Font licensing remains distinct from SDK code licensing; an MIT repository root does not override third-party notices.
