#!/usr/bin/env python3
"""Writes the values from spsa.py's output file into the defaults of the TUNABLE declarations in the source."""

import argparse

from params import DECLARATION, UCI_PREFIX, find_params


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("values", help="the file spsa.py wrote with --output")
    values = {}
    for line in open(parser.parse_args().values):
        name, value = line.split(",")
        values[name.strip().removeprefix(UCI_PREFIX)] = int(value)

    params = find_params()
    for path in {params[name].path for name in values}:
        path.write_text(DECLARATION.sub(
            lambda m: f"TUNABLE({m[1]}, {values[m[1]]}, {m[3]}, {m[4]})" if m[1] in values else m[0], path.read_text()))
    for name, value in values.items():
        if value != params[name].value:
            print(f"{name}: {params[name].value} -> {value}")


if __name__ == "__main__":
    main()
