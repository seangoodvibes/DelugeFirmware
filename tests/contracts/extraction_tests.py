import importlib.util
import unittest
from pathlib import Path

path = Path(__file__).resolve().parents[1] / "support/extract_functions.py"
spec = importlib.util.spec_from_file_location("extract_functions", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class FunctionExtractionTests(unittest.TestCase):
    def test_body_is_verbatim_including_nested_blocks_comments_and_literals(self):
        body = """bool Device::send(int value) {
  // } misleading brace
  if (value) { const char* text = "}"; /* } */ return text[0]; }
  return false;
}"""
        result = module.extract(
            "// preamble\n" + body + "\nvoid unrelated() {}",
            ["Device::send"],
            "firmware.cpp",
        )
        self.assertEqual('#line 2 "firmware.cpp"\n' + body + "\n", result)

    def test_multiline_parameters_and_const_are_preserved(self):
        body = "bool Song::owns(\n const Clip* clip, bool detached\n) const { return clip && detached; }"
        self.assertIn(body, module.extract(body, ["Song::owns"], "song.cpp"))

    def test_missing_ambiguous_and_unbalanced_definitions_fail_build(self):
        for source in ["", "bool A::f() {}\nbool A::f(int n) {}", "bool A::f() {"]:
            with self.assertRaises(ValueError):
                module.extract(source, ["A::f"], "source.cpp")

    def test_calls_declarations_and_commented_definitions_are_not_extracted(self):
        source = "bool A::f();\n// bool A::f() { return false; }\nbool A::f() { return true; }"
        self.assertIn("return true;", module.extract(source, ["A::f"], "source.cpp"))
        self.assertNotIn(
            "return false;", module.extract(source, ["A::f"], "source.cpp")
        )


if __name__ == "__main__":
    unittest.main()
