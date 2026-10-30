import unittest
from pathlib import Path

from check_dependencies import first_party, object_path, zero_records


class DependenciesTests(unittest.TestCase):
    def test_only_zero_valid_records_are_candidates(self):
        text = ("foo/test.cpp.obj: #deps 0, deps mtime 123 (VALID)\n"
                "foo/other.cpp.obj: #deps 3, deps mtime 123 (VALID)\n"
                "foo/stale.cpp.obj: #deps 0, deps mtime 123 (STALE)\n")
        self.assertEqual(zero_records(text), {"foo/test.cpp.obj"})

    def test_generated_and_third_party_inputs_are_not_owned(self):
        root = Path.cwd().resolve()
        self.assertTrue(first_party(root / "plugins/example/Model.cpp", [root]))
        self.assertFalse(first_party(root / "libraries/canvas/third_party/vendor.cpp", [root]))
        self.assertFalse(first_party(root / "build/Testing/generated.cpp", [root]))
        self.assertFalse(first_party(root / "sdk/generated.pb.cc", [root]))

    def test_only_cache_objects_can_be_retired(self):
        build = (Path.cwd() / "build/relwithdebinfo").resolve()
        self.assertEqual(object_path(build, "CMakeFiles/example.cpp.obj"), build / "CMakeFiles/example.cpp.obj")
        for path in ("../../src/main.cpp.obj", "CMakeFiles/main.cpp", "dist/example.cpp.obj"):
            with self.assertRaises(ValueError):
                object_path(build, path)


if __name__ == "__main__":
    unittest.main()
