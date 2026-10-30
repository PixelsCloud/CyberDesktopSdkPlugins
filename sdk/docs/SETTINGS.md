# Standard per-instance settings pages

Every plugin must provide `PluginDescriptor.settings_page` and the `get_setting` / `apply_settings` callbacks. A page contains 1..32 uniquely named fields. Hosts generate the native page from this schema; authors do not need to link Qt or create a host window. The same description is embedded in the single release package.

Types: TOGGLE, INTEGER, NUMBER, TEXT, CHOICE and COLOR. Declare labels, optional descriptions/sections, valid defaults, numeric minimum/maximum/step, text UTF-8 byte limit, and choice value/label pairs. COLOR uses packed RGBA `0xRRGGBBAA`. Numeric integer controls are int32; text is limited to 4096 bytes; at most 64 choices. Defaults must be valid.

For a directory setting, use `type: TEXT text_editor: DIRECTORY`. The desktop, Studio and standalone SDK preview add a folder browser while retaining the normal UTF-8 TEXT value and C ABI. The hint is valid only for TEXT. An empty value may represent a plugin-defined default such as the Windows Pictures known folder. Do not embed user photos in the package.

For multiline notes, tasks or snippets, use `type: TEXT text_editor: MULTILINE`.
Desktop, Studio and native SDK preview use a multiline editor with ordinary
UTF-8 TEXT values. `max_length` remains a byte limit checked atomically by the
host/plugin. Line breaks are preserved; invalid or oversized input is rejected
without replacing the previous value. This hint does not change the C ABI.

```textproto
settings_page {
  title: "Counter settings"
  fields { id: "step" label: "Increment" type: INTEGER
           minimum: 1 maximum: 100 step: 1 integer_default: 1 }
}
```

`CdSettingValue` uses a numeric type matching the schema: integer holds toggles/integers/colors, number holds NUMBER, text holds TEXT/CHOICE. `get_setting` returns borrowed strings valid until the next plugin call; the host copies them immediately. Reject short output buffers without writing. Unknown keys or wrong types return an error. `apply_settings` receives an array of updates, not necessarily every field. Validate the entire transaction before changing anything; if any update is invalid, retain all old values. Never retain borrowed input pointers. Request redraw after applying changes.

`Settings.h` is an optional header-only C++ implementation of storage, getters and atomic validation. The minimal template and two examples use it. Match its initial values/constraints to the descriptor and keep the settings in the instance, never in static globals.

The production host stores a settings snapshot independently of opaque native business state, indexed by the placed instance UUID. `save_state` does not need to duplicate settings. Creating a fresh instance uses defaults; restoring a saved host snapshot re-applies the configuration. The same plugin can be placed twice with different settings.

Desktop: right-click the plugin brick, then 插件设置…. Studio preview has the same button. Public preview: press S. Pages offer defaults/apply/save/cancel; defaults edit pending values, and cancel discards edits not yet applied. Public `cd-sdk test` checks schema/callback agreement, invalid atomic transactions, live values and multiple-instance independence. Official host verification also checks UI, persistence and destroyed/recreated instances.

## Fonts and two supported languages

`FONT` (C API `CD_SETTING_FONT`, value 7) stores a UTF-8 font family of 1–128 bytes. The standard editor lists installed fonts. `@siyuan-heavy` selects the shared Gen Shin Gothic Heavy TTF from the host executable's `assets/fonts/SiYuanZhenHei-Heavy/GenShinGothic-Heavy-2.ttf`; it is an external host asset, not compiled into the DLL or embedded in the plugin package. Standalone tools need the same asset directory; otherwise the optional WindowsFonts helper falls back to Segoe UI.

Use a `language` CHOICE with exactly `zh-CN` and `en`. Settings pages and fields accept a `localized` map for their title, description, label and section; choices use `localized_labels`. Each map accepts only those two locale keys. The original text supplies the Chinese fallback. Main desktop and Studio settings editors translate the page immediately when its language selection changes; rendering changes when settings are applied. Both selections are saved independently per instance. Host-wide menus are outside this plugin localization scope.

`WindowsFonts.h` is an optional Qt-free DirectWrite helper for loading the external font and selecting system fonts. Link `dwrite`; it does not change C ABI structure sizes.

`@zqk-free` selects the external `assets/fonts/ZQKFreeFont/ZQKfreefont-2.ttf`. All flip-card clocks use this default. The standard editor lists both bundled font IDs alongside installed families, and marks the descriptor’s default. `WindowsFonts` reads the file’s own font weight rather than synthesizing Heavy for every bundled family.
