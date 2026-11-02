CyberLyrics runtime: source, rebuild and replacement

cyberlyrics_runtime.dll is an independently replaceable C ABI adapter around
the LGPL-only FFmpeg LRC demuxer. Desktop and Player load the same published
DLL; neither application links this FFmpeg implementation into its EXE.
This adapter needs no Qt, Player, private application source or music client.

The adapter source is MIT licensed (see LICENSE). The combined DLL includes
FFmpeg under LGPL 2.1 or later and dav1d under BSD-2-Clause. The accompanying
notices retain their complete license texts and package provenance. FFmpeg
sources include the patches applied by the supplied vcpkg port.

Corresponding materials are delivered separately from the clean runtime
directory, in the adjacent player-release-sources directory. The packaged
licenses/CyberLyrics/source-reference.json records their names and digests.
Redistribution must make these corresponding materials available alongside
the runtime; do not distribute a broken local reference as a source offer.

Extract CyberLyricsRuntime-source.zip into an empty source directory. It
contains this CMake project, src/Runtime.cpp, include/CyberLyrics/Runtime.h,
the dependency manifest, exact FFmpeg/dav1d ports and the /MT release triplet.
FFmpeg-source.zip and dav1d-source.zip contain the corresponding dependency
sources. The dependency manifest pins the vcpkg baseline. With that pinned
vcpkg checkout and an x64 Visual Studio developer shell:

  set VCPKG_MAX_CONCURRENCY=10
  vcpkg install --x-manifest-root=dependencies --overlay-ports=overlay-ports --overlay-triplets=dependencies/triplets --x-install-root=ffmpeg-build --triplet=x64-windows-static-release
  cmake -S . -B rebuild -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCYBERLYRICS_FFMPEG_ROOT=<absolute-source-directory>/ffmpeg-build/x64-windows-static-release
  cmake --build rebuild --parallel 10

An existing compatible LGPL prefix may be supplied instead of rebuilding the
dependencies. Its FFmpeg configuration must keep GPL and nonfree disabled.
The runtime's cyberlyrics_runtime_license() export reports FFmpeg's license.

Close applications using the DLL, keep a recoverable copy outside the runtime
directory, and replace cyberlyrics_runtime.dll beside the application EXEs
with the rebuilt Windows x64 /MT DLL. Keep the two C exports and signatures in
include/CyberLyrics/Runtime.h. All FFmpeg memory stays within this DLL; callback
text is borrowed only until the callback returns. Applications impose no
startup checksum restriction on a compatible replacement.

You may modify and rebuild this adapter and replace its FFmpeg implementation.
Modification for your own use and reverse engineering needed to debug such
modifications are permitted. Third-party license terms remain applicable.
FFmpeg legal information: https://ffmpeg.org/legal.html
LGPL text: https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html
