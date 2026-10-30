"""Negative fixtures for the source gate; fixtures stay in memory."""

import unittest

from check_structure import adapter_errors, include_errors


class StructureTests(unittest.TestCase):
    def test_parent_includes_are_rejected(self):
        for path in ("../Model.h", "../../Skin.h", "plugins/foo/../../Model.h"):
            self.assertTrue(include_errors("plugins/example/Test.cpp", f'#include "{path}"'))

    def test_absolute_includes_are_rejected(self):
        for path in ("C:/work/Model.h", "D:\\work\\Model.h", "/work/Model.h", "\\\\server\\share\\Model.h"):
            self.assertTrue(include_errors("plugins/example/Test.cpp", f'#include "{path}"'))

    def test_incomplete_public_roots_are_rejected(self):
        for path in ("Model.h", "controllers/widget/Widget.h", "services/Keyboard.h"):
            self.assertTrue(include_errors("plugins/example/Test.cpp", f'#include "{path}"'))

    def test_explicit_roots_and_private_local_includes(self):
        self.assertFalse(include_errors("plugins/example/Test.cpp", '#include "plugins/widget-suite/Widget.h"'))
        self.assertFalse(include_errors("sdk/Controls.h", '#include "sdk/services/Keyboard.h"'))
        self.assertFalse(include_errors("src/example.cpp", '#include "Local.h"', public=False))

    def test_generated_allowance_has_a_fixed_owner(self):
        self.assertFalse(include_errors("plugins/ambience-suite/tools/Tests.cpp", '#include "AmbienceCatalog.h"'))
        self.assertTrue(include_errors("plugins/example/Test.cpp", '#include "AmbienceCatalog.h"'))

    def test_independent_subscription_in_adapter_is_rejected(self):
        self.assertTrue(adapter_errors("adapter.cpp", "themed::create(); subscribe_timer();", "themed::create", 80))

    def test_missing_delegate_and_growing_adapter_are_rejected(self):
        self.assertTrue(adapter_errors("adapter.cpp", "independent_controller();", "themed::create", 80))
        self.assertTrue(adapter_errors("adapter.cpp", "themed::create();\n" * 81, "themed::create", 80))


if __name__ == "__main__":
    unittest.main()
