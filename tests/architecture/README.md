# Architecture checks

Run `python tests/architecture/check_structure.py` and `python tests/architecture/test_structure.py`
from the public repository. A private consumer may pass `--core-root` to check its own first-party include policy;
the public check does not require private sources.

For a full source configuration containing the shared plugin families, build `cd_shared_architecture_checks`.
This opt-in target compiles the affected public headers independently with PCH and Unity disabled, runs the five
shared controller lifecycle suites, and verifies a negative lifecycle control. A single generated test translation
unit omits destructor audio cleanup; the real partial-startup test must reject that binary with its expected failure.
Production sources are never modified by this check and the negative binary is not packaged.

The include and adapter checks are explicit source policies. They do not parse C++ ownership or certify arbitrary
raw-pointer usage. Ownership, cancellation, callback lifetime, direct dependencies, and hot-path allocations still
require review and targeted runtime tests. General duplicate candidates are measured with jscpd 5.4.2 in weak mode,
with a minimum of 15 lines / 150 tokens and third-party/generated/test/tool sources excluded.

For an existing MSVC/Ninja cache, `check_dependencies.py --build <cache> --source-root <public-root>
--report <Testing/report.json>` detects first-party object files with valid but empty include dependency records.
Enable `CMAKE_EXPORT_COMPILE_COMMANDS` in that cache first, and hold the consumer's normal build lease while
running the audit. The optional `--repair` retires only the identified `.obj` files inside that cache; the next
ordinary build recreates them with the configured compiler dependency prefix. It does not clear the cache,
remove sources, or change third-party objects. `test_dependencies.py` covers classification and output guards.

`PhysicsBaseline.cmake` is an optional comparison harness enabled by `CD_PHYSICS_BASELINE_ROOT`.
Its input directory contains explicitly selected model sources and generated Pixel specializations from a
reference revision. It is not required to build or install the SDK and never enters a release package.
