Live2D character plugin

Available grid layouts: 2x2 (default), 3x3 and 3x4 (columns x rows).
Each layout uses the same character settings and aspect-preserving rendering.

New instances display Mao immediately. Select Mao, Haru, Hiyori, Wanko or
DS Whale Girl in the built-in character setting. All five work offline with
authored idle and interaction motions. Click the character to cycle through
its interaction motions; each action returns to idle when finished. Automatic
idle mode cycles all authored idle clips. Pointer tracking supports modern and
legacy Cubism parameter names, including Wanko.
Pointer tracking accelerates and decelerates smoothly on entry, movement and
return to center. Its response is consistent at 30/60 FPS; pause freezes it.

Mao: 2 idle clips, 6 tap actions, 8 expressions.
Haru: 2 idle clips, 4 tap actions, 8 expressions.
Hiyori: 9 idle clips, 1 tap action.
Wanko: 3 idle clips, 2 tap actions.
Whale Girl: 1 idle clip, 7 actions, 44 expressions/props.
Motion changes crossfade over 0.25 seconds, including interrupted actions and
the return to idle. Controls unused by the incoming action fade back to their
initial values, preventing leftover phones, hammers or food in later actions.
Textures use premultiplied-alpha mipmaps and 4x anisotropic filtering. Clipping
masks follow output DPI up to 1024 pixels within the GPU budget; local 4x MSAA
remains enabled when supported. SprayWater enables the
author-required whale prop and has a three-second playback window.

To use your own model, select a local directory containing one model3.json and
its referenced resources. A nonempty custom directory overrides the built-in
choice. Clear the directory to return to the selected built-in character.
Models run in isolated, memory-limited child processes. A rejected or failed model
does not restart automatically. Correct the files and apply settings again, or
choose a different directory to recover. Cubism 3+ exported model3.json/MOC3
packages are supported; Editor project files and legacy model.json/MOC are not.
Motion names use group/index, such as Idle/0; auto cycles available idle clips.
Expressions use their model names. Removed character selections fall back to
Mao while retaining other valid saved settings and custom directories.
Default: 30 FPS, physics and pointer tracking enabled. Pause stops animation work.
Requires Desktop's render.live2d service and cyber_live2d_host.exe.
Built-in assets and their pinned origins are listed in models/catalog.json.
See THIRD_PARTY_NOTICES.txt and licenses for model credits and license terms.
Limits: eight active instances, 4096 output pixels per edge and 8 megapixels per surface, 4096 texture pixels
per edge, sixteen textures, 256 resources, 32 MiB per resource, 256 MiB decoded
textures and 1 GiB private commit per worker. Loading has progress/total deadlines.

Sizes are width x height in grid cells: 2x2, 3x3, 3x4, 4x4, 4x5, 5x5, 5x6, 6x6 and 6x7.
Failure cards explain the cause and recovery in Chinese, wrap at the actual DPI,
and offer clearing the custom directory to restore a built-in character.
