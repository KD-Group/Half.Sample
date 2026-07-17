#!/usr/bin/env python3
"""Plot timestamped Legacy Instant AI polling samples."""

import argparse
import csv
import math
import re
import sys
from pathlib import Path


FIXED_COLUMNS = (
    "read_index",
    "elapsed_seconds",
    "call_duration_us",
    "interval_us",
)
CHANNEL_COLUMN = re.compile(r"^channel_\d+$")


def uniform_sample_indices(total_points, max_points):
    """Return chronological indices spread across the full sequence."""
    if total_points <= 0:
        return []
    if max_points == 0 or max_points >= total_points:
        return list(range(total_points))
    if max_points == 1:
        return [0]
    denominator = max_points - 1
    return [
        (index * (total_points - 1) + denominator // 2) // denominator
        for index in range(max_points)
    ]


def read_samples(path):
    with path.open("r", encoding="utf-8", newline="") as source:
        reader = csv.DictReader(source, delimiter="\t")
        fields = reader.fieldnames
        if not fields:
            raise ValueError("input is empty or has no header")
        if len(fields) != len(set(fields)):
            raise ValueError("header contains duplicate columns")
        missing = [name for name in FIXED_COLUMNS if name not in fields]
        if missing:
            raise ValueError("missing required columns: " + ",".join(missing))
        channels = [name for name in fields if CHANNEL_COLUMN.fullmatch(name)]
        if not channels:
            raise ValueError("header contains no channel_<n> column")
        unexpected = [
            name for name in fields
            if name not in FIXED_COLUMNS and name not in channels
        ]
        if unexpected:
            raise ValueError("unexpected columns: " + ",".join(unexpected))

        elapsed = []
        values = {channel: [] for channel in channels}
        for line_number, row in enumerate(reader, start=2):
            if None in row or any(row.get(name) is None for name in fields):
                raise ValueError(f"line {line_number} has the wrong column count")
            try:
                read_index = int(row["read_index"])
                if read_index < 0:
                    raise ValueError
                numeric = {
                    name: float(row[name])
                    for name in fields
                    if name != "read_index"
                }
                if any(not math.isfinite(value) for value in numeric.values()):
                    raise ValueError
            except (TypeError, ValueError):
                raise ValueError(
                    f"line {line_number} contains an invalid numeric value"
                ) from None
            elapsed.append(numeric["elapsed_seconds"])
            for channel in channels:
                values[channel].append(numeric[channel])
        if not elapsed:
            raise ValueError("input contains no sample rows")
        return elapsed, channels, values


def parse_arguments(arguments):
    parser = argparse.ArgumentParser(
        description="Plot samples from raw/instant_ai_polling.tsv"
    )
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--max-points",
        type=int,
        default=100000,
        help="maximum uniformly sampled points per channel; 0 plots all",
    )
    parser.add_argument("--show", action="store_true")
    options = parser.parse_args(arguments)
    if options.max_points < 0:
        parser.error("--max-points must be zero or positive")
    if options.output is None:
        options.output = options.input.with_suffix(".png")
    return options


def main(arguments):
    options = parse_arguments(arguments)
    try:
        elapsed, channels, values = read_samples(options.input)
        indices = uniform_sample_indices(len(elapsed), options.max_points)

        import matplotlib.pyplot as pyplot

        figure, axis = pyplot.subplots(figsize=(12, 6))
        x_values = [elapsed[index] for index in indices]
        for channel in channels:
            y_values = [values[channel][index] for index in indices]
            axis.plot(
                x_values,
                y_values,
                marker=".",
                markersize=2,
                linewidth=0.5,
                label=channel,
            )
        axis.set_xlabel("Elapsed time (s)")
        axis.set_ylabel("Voltage (V)")
        axis.set_title(
            "Legacy Instant AI polling "
            f"(total points: {len(elapsed)}, plotted points: {len(indices)})"
        )
        axis.grid(True, alpha=0.25)
        axis.legend()
        figure.tight_layout()
        figure.savefig(options.output, dpi=150)
        if options.show:
            pyplot.show()
        pyplot.close(figure)
    except (OSError, ValueError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print(
        f"output={options.output} total_points={len(elapsed)} "
        f"plotted_points={len(indices)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
