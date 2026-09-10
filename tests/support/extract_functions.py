"""Compile selected production definitions without unrelated hardware code.

Bodies are copied verbatim, not translated or modeled. Source dependencies force
regeneration; missing or ambiguous definitions fail the build. #line preserves
source locations in compiler and sanitizer diagnostics.
"""

import argparse
import re
from pathlib import Path


def extract(source, names, filename):
    masked = re.sub(
        r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
        lambda m: "".join("\n" if c == "\n" else " " for c in m.group()),
        source,
        flags=re.DOTALL,
    )
    result = []
    for name in names:
        matches = list(
            re.finditer(
                r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*(?:const\s*)?\{", masked
            )
        )
        if len(matches) != 1:
            raise ValueError(
                f"{name}: expected exactly one definition, found {len(matches)}"
            )
        match = matches[0]
        start = masked.rfind("\n", 0, match.start()) + 1
        depth = 1
        end = match.end()
        while depth and end < len(masked):
            depth += (masked[end] == "{") - (masked[end] == "}")
            end += 1
        if depth:
            raise ValueError(f"{name}: unbalanced definition")
        line = source.count("\n", 0, start) + 1
        result.append(f'#line {line} "{filename}"\n' + source[start:end] + "\n")
    return "\n".join(result)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("functions", nargs="+")
    args = parser.parse_args()
    args.output.write_text(
        extract(args.source.read_text(), args.functions, args.source.as_posix())
    )
