#!/usr/bin/env python3
"""Compare semantic, lexical, hybrid, and rerank retrieval on labeled corpora."""

import argparse
import csv
import math
import shutil
import subprocess
import sys
import time
from pathlib import Path


BENCH_DIR = Path(__file__).resolve().parent
PROJECT_DIR = BENCH_DIR.parent
DATASETS_DIR = BENCH_DIR / "datasets"
QUERIES_DIR = BENCH_DIR / "queries"
TOP_K = 5
MODES = ("semantic", "lexical", "hybrid", "rerank")


class BenchmarkError(Exception):
    """Invalid benchmark input or a failed semidx command."""


def read_tsv(path, columns):
    rows = []
    with path.open("r", encoding="utf-8", newline="") as stream:
        for number, row in enumerate(csv.reader(stream, delimiter="\t"), 1):
            if not row:
                continue
            if len(row) != columns or any(not field.strip() for field in row):
                raise BenchmarkError(f"{path}:{number}: expected {columns} nonempty TSV fields")
            rows.append(row)
    if not rows:
        raise BenchmarkError(f"{path}: empty file")
    return rows


def write_tsv(path, header, rows):
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
        writer.writerow(header)
        writer.writerows(rows)


def resolve_binary(value):
    if "/" in value or "\\" in value:
        path = Path(value).expanduser().resolve()
        if not path.is_file():
            raise BenchmarkError(f"semidx binary not found: {path}")
        return str(path)
    found = shutil.which(value)
    if found is None:
        raise BenchmarkError(f"semidx binary not found in PATH: {value} (use --binary)")
    return found


def execute(binary, *args):
    command = [binary, *map(str, args)]
    start = time.perf_counter()
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
    elapsed_ms = (time.perf_counter() - start) * 1000
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise BenchmarkError(
            f"command failed ({result.returncode}): {' '.join(command[:3])}\n{detail}"
        )
    return result.stdout, elapsed_ms


def search_results(output, dataset, case):
    entries = []
    for number, line in enumerate(output.splitlines(), 1):
        fields = line.split("\t", 2)
        if len(fields) != 3:
            raise BenchmarkError(f"{dataset}/{case}: malformed search result {number}")
        doc_id, score, content = fields
        try:
            similarity = float(score)
        except ValueError as error:
            raise BenchmarkError(
                f"{dataset}/{case}: invalid similarity in result {number}: {score}"
            ) from error
        if not math.isfinite(similarity):
            raise BenchmarkError(f"{dataset}/{case}: non-finite score in result {number}")
        entries.append((doc_id, similarity, content))
    return entries


def measure_dataset(name, binary, output_dir, modes):
    documents = read_tsv(DATASETS_DIR / f"{name}.tsv", 2)
    queries = read_tsv(QUERIES_DIR / f"{name}.tsv", 3)

    content_by_id = dict(documents)
    if len(content_by_id) != len(documents):
        raise BenchmarkError(f"{name}: duplicate document IDs")
    case_ids = [row[0] for row in queries]
    if len(set(case_ids)) != len(case_ids):
        raise BenchmarkError(f"{name}: duplicate query IDs")
    for case, expected, _query in queries:
        if expected not in content_by_id:
            raise BenchmarkError(f"{name}/{case}: unknown expected document: {expected}")

    folder = output_dir / name
    folder.mkdir(parents=True, exist_ok=True)
    index = folder / "index.bin"
    _output, build_ms = execute(
        binary, "build", index, "-i", DATASETS_DIR / f"{name}.tsv"
    )

    ranked_rows = []
    case_rows = []
    summary = []
    for mode in modes:
        hit1 = hit5 = 0
        reciprocal_sum = 0.0
        total_ms = 0.0
        misses = []
        for case, expected, query in queries:
            output, elapsed_ms = execute(
                binary, "search", index, query,
                "--mode", mode, "--limit", TOP_K
            )
            total_ms += elapsed_ms
            matches = search_results(output, name, f"{mode}/{case}")
            if len(matches) > TOP_K:
                raise BenchmarkError(f"{name}/{mode}/{case}: more than {TOP_K} results")
            seen = set()
            for doc_id, score, content in matches:
                if doc_id not in content_by_id:
                    raise BenchmarkError(f"{name}/{mode}/{case}: unknown result {doc_id}")
                if doc_id in seen:
                    raise BenchmarkError(f"{name}/{mode}/{case}: repeated result {doc_id}")
                if content != content_by_id[doc_id]:
                    raise BenchmarkError(
                        f"{name}/{mode}/{case}: content mismatch for {doc_id}"
                    )
                seen.add(doc_id)

            rank = next(
                (position for position, entry in enumerate(matches, 1)
                 if entry[0] == expected),
                None,
            )
            hit1 += rank == 1
            hit5 += rank is not None
            reciprocal_sum += 1.0 / rank if rank is not None else 0.0
            first_id = matches[0][0] if matches else ""
            first_score = f"{matches[0][1]:.12g}" if matches else ""
            gap = (
                f"{matches[0][1] - matches[1][1]:.12g}"
                if len(matches) > 1 else ""
            )
            case_rows.append([
                mode, case, expected, rank or "", first_id, first_score,
                gap, f"{elapsed_ms:.3f}", query
            ])
            for position, (doc_id, score, content) in enumerate(matches, 1):
                ranked_rows.append([
                    mode, case, expected, query, position, doc_id,
                    f"{score:.12g}", content
                ])
            if rank != 1:
                misses.append((case, expected, first_id or "(sin resultados)"))

        total = len(queries)
        mean_ms = total_ms / total
        summary.append([
            name, mode, len(documents), total, hit1, hit5,
            f"{hit1 / total:.4f}", f"{hit5 / total:.4f}",
            f"{reciprocal_sum / total:.4f}", f"{mean_ms:.3f}",
            f"{build_ms:.3f}"
        ])
        print(
            f"{name} / {mode}: {len(documents)} documentos, {total} consultas | "
            f"Hit@1 {hit1}/{total} ({100 * hit1 / total:.1f}%) | "
            f"Hit@5 {hit5}/{total} ({100 * hit5 / total:.1f}%) | "
            f"MRR@5 {reciprocal_sum / total:.3f} | "
            f"CLI {mean_ms:.1f} ms/consulta"
        )
        for case, expected, observed in misses[:5]:
            print(f"  {case}: esperado={expected}, primero={observed}")
        if len(misses) > 5:
            print(f"  ... {len(misses) - 5} casos adicionales en {folder / 'cases.tsv'}")

    write_tsv(
        folder / "cases.tsv",
        ["mode", "case", "expected", "rank_at_5", "top1", "score_top1",
         "gap_top1_top2", "cli_ms", "query"],
        case_rows,
    )
    write_tsv(
        folder / "results.tsv",
        ["mode", "case", "expected", "query", "rank", "id", "score", "content"],
        ranked_rows,
    )
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("datasets", nargs="*", help="Dataset names (default: all)")
    parser.add_argument(
        "--binary", default="semidx",
        help="semidx executable, e.g. ./bin/linux/x86_64/semidx",
    )
    parser.add_argument(
        "--output", type=Path, default=PROJECT_DIR / ".build" / "bench",
        help="Directory for built indexes and evaluation results",
    )
    parser.add_argument(
        "--mode", choices=MODES, action="append",
        help="Search mode to evaluate (default: all; repeat to select multiple)",
    )
    args = parser.parse_args()
    try:
        available = sorted(path.stem for path in DATASETS_DIR.glob("*.tsv"))
        selected = args.datasets or available
        for name in selected:
            if name not in available or not (QUERIES_DIR / f"{name}.tsv").is_file():
                raise BenchmarkError(f"unknown or incomplete dataset: {name}")
        binary = resolve_binary(args.binary)
        output_dir = args.output.resolve()
        output_dir.mkdir(parents=True, exist_ok=True)
        modes = tuple(dict.fromkeys(args.mode)) if args.mode else MODES
        summary = [
            row
            for name in selected
            for row in measure_dataset(name, binary, output_dir, modes)
        ]
        write_tsv(
            output_dir / "summary.tsv",
            ["dataset", "mode", "documents", "queries", "hits_at_1", "hits_at_5",
             "hit_at_1", "hit_at_5", "mrr_at_5", "mean_cli_ms", "build_ms"],
            summary,
        )
        print(f"Resultados: {output_dir / 'summary.tsv'}")
    except (BenchmarkError, OSError, UnicodeError) as error:
        print(f"benchmark: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
