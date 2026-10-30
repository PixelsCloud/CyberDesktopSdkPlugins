# Package format 1

Public schema: `schemas/plugin_package.proto`. `.textproto` is the authoring form; `.cdpkg` is the serialized release message. No JSON manifest or legacy ZIP reader is part of this format.

The descriptor is strongly typed. Package `magic` is CyberDesktopPlugin, format_version is 1, and descriptor abi_version is 1. Each file has a relative path, exact bytes, length and SHA-256. Descriptor hash is calculated over its deterministic serialization using the published schema and SDK writer. Use the official packer rather than hashing arbitrary protobuf encodings; deterministic serialization is not a general canonical format across unrelated schemas/runtime versions.

All privately imported DLLs and resources go into this file. The DLL loader uses the verified extracted directory. DLLs loaded manually must be declared. Mutable user/instance state is stored independently by the host; do not overwrite embedded resources.

Root dependency DLLs must be unique under Windows case-insensitive names. Explicit Windows dependencies are restricted to known Windows OS names (VC redistributables are private). Actual normal/delay imports and the new non-forwarded `cd_plugin_query` entry export, payload hashes and file inventory are checked before execution. Checksums detect corruption; they do not authenticate the publisher.

Paths are relative with forward slashes. Absolute/parent paths, ADS, reserved devices, ambiguous trailing spaces/dots, duplicates and file/directory collisions are rejected. Extraction requires an empty trusted directory and rejects reparse-point ancestors. DLLs live at the package root. When packing a development directory, editable `plugin.textproto` takes precedence over generated `descriptor.pb`. The production installer must stage, validate and replace only after unloading existing instances; an in-use DLL cannot be overwritten.

Limits are part of format 1: 256 MiB aggregate/package, 64 MiB file, 4096 files, 64 layouts, 64 cells/layout, 16x16 footprint span. Unknown major formats and old plugin packages are rejected.

Every descriptor must provide a nonempty `settings_page` (1..32 fields). Numeric values have finite ranges/positive steps; integers are limited to int32 for standard controls. Text is limited to 4096 UTF-8 bytes, choices to 64 options. All defaults must satisfy these rules. Settings configuration is mutable per-instance host data, stored outside the immutable package.
