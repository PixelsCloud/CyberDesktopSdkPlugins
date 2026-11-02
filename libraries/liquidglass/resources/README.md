# React glass reference

Primary reference: https://github.com/rdev/liquid-glass-react
Pinned revision: `ac48eab18d1f7f444ae30002d240cae29c863a21`.

`react-standard.rg`, `react-polar.rg` and `react-prominent.rg` contain the original
256 × 256 displacement maps' R and B channels, interleaved as unsigned bytes.
The JPEG maps were decoded through the reference browser to retain its exact
channel values. These are the channels selected by the upstream SVG filter.
Each map is embedded as one immutable RG8 GPU texture. No image decoder is
needed at runtime. Shader mode follows `shader-utils.ts`; the 340 × 48 generated
map is checked byte for byte against the original browser-generated PNG.

The native shader adapts the upstream separate-channel displacement and screen /
overlay edge layers; foreground widgets remain outside the warped backdrop.
Pointer feedback uses the original formulas and finite 200 ms CSS ease-out
animations. Rendering never polls a static desktop.
The SDF supplies DPI-aware shape coverage and attached-top geometry.

Copyright 2025 MAX ROVENSKY. Distributed under the accompanying MIT license.
