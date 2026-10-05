import re
from dataclasses import dataclass
from pathlib import Path

SOURCE_DIR = Path(__file__).resolve().parents[2] / "src"
UCI_PREFIX = "TUNABLE_"
DECLARATION = re.compile(r"TUNABLE\(\s*(\w+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\)")


@dataclass
class Param:
    name: str
    value: int
    min: int
    max: int
    path: Path
    line: int

    def uci_name(self):
        return UCI_PREFIX + self.name


def find_params(source_dir=SOURCE_DIR):
    params = {}
    for path in sorted(list(source_dir.glob("*.cpp")) + list(source_dir.glob("*.h"))):
        for line_number, line in enumerate(path.read_text().splitlines(), 1):
            for match in DECLARATION.finditer(line):
                name, value, low, high = match[1], *map(int, match.groups()[1:])
                if name in params:
                    raise ValueError(f"{name} is declared twice: {params[name].path}:{params[name].line} and {path}:{line_number}")
                params[name] = Param(name, value, low, high, path, line_number)
    return params
