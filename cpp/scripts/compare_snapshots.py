#!/usr/bin/env python3
import argparse
import json
import math
from pathlib import Path


def load(path):
    with Path(path).open("r", encoding="utf-8") as handle:
        return json.load(handle)


def by_name(items, key="name"):
    return {item.get(key): item for item in items}


def cells_by_position(cells):
    return {(cell.get("row"), cell.get("column")): cell for cell in cells}


def equal_number(left, right, tolerance):
    if left is None or right is None:
        return left is right
    try:
        return math.isclose(float(left), float(right), rel_tol=tolerance, abs_tol=tolerance)
    except (TypeError, ValueError):
        return left == right


class DiffReporter:
    def __init__(self, limit):
        self.limit = limit
        self.count = 0
        self.samples = []

    def add(self, path, left, right):
        self.count += 1
        if len(self.samples) < self.limit:
            self.samples.append((path, left, right))


def compare_scalar(reporter, path, left, right):
    if left != right:
        reporter.add(path, left, right)


def compare_keyset(reporter, path, left_keys, right_keys):
    left_set = set(left_keys)
    right_set = set(right_keys)
    if left_set == right_set:
        return
    reporter.add(
        path,
        {
            "count": len(left_set),
            "only": sorted(left_set - right_set)[:10],
        },
        {
            "count": len(right_set),
            "only": sorted(right_set - left_set)[:10],
        },
    )


def compare_number(reporter, path, left, right, tolerance):
    if not equal_number(left, right, tolerance):
        reporter.add(path, left, right)


def normalize_issue(issue):
    return {
        "severity": issue.get("severity"),
        "code": issue.get("code"),
        "fileName": issue.get("fileName"),
        "filePath": issue.get("filePath"),
        "sheetName": issue.get("sheetName"),
        "message": issue.get("message"),
    }


def issue_sort_key(issue):
    normalized = normalize_issue(issue)
    return (
        normalized.get("fileName") or "",
        normalized.get("sheetName") or "",
        normalized.get("code") or "",
        normalized.get("severity") or "",
        normalized.get("message") or "",
        normalized.get("filePath") or "",
    )


def compare_issues(reporter, path, left_issues, right_issues):
    left_sorted = sorted((normalize_issue(issue) for issue in left_issues), key=issue_sort_key)
    right_sorted = sorted((normalize_issue(issue) for issue in right_issues), key=issue_sort_key)
    compare_scalar(reporter, f"{path}.count", len(left_sorted), len(right_sorted))
    for index, (left_issue, right_issue) in enumerate(zip(left_sorted, right_sorted)):
        issue_path = f"{path}[{index}]"
        for field in ["severity", "code", "fileName", "filePath", "sheetName", "message"]:
            compare_scalar(reporter, f"{issue_path}.{field}", left_issue.get(field), right_issue.get(field))


def compare_parse_failures(reporter, left, right):
    compare_issues(reporter, "parseFailures", left.get("parseFailures", []), right.get("parseFailures", []))


def compare_validation(reporter, left, right):
    left_validation = left.get("validation", {})
    right_validation = right.get("validation", {})
    compare_scalar(reporter, "validation.readiness", left_validation.get("readiness"), right_validation.get("readiness"))
    compare_scalar(reporter, "validation.commonSheetNames", left_validation.get("commonSheetNames"), right_validation.get("commonSheetNames"))
    compare_scalar(reporter, "validation.skippedSheetNames", left_validation.get("skippedSheetNames"), right_validation.get("skippedSheetNames"))
    compare_issues(
        reporter,
        "validation.skippedSheetIssues",
        left_validation.get("skippedSheetIssues", []),
        right_validation.get("skippedSheetIssues", []),
    )

    left_files = by_name(left_validation.get("files", []), "filename")
    right_files = by_name(right_validation.get("files", []), "filename")
    compare_keyset(reporter, "validation.files", left_files.keys(), right_files.keys())
    for filename in sorted(set(left_files) & set(right_files)):
        compare_scalar(reporter, f"validation.files[{filename}].status", left_files[filename].get("status"), right_files[filename].get("status"))
        compare_scalar(reporter, f"validation.files[{filename}].isTemplate", left_files[filename].get("isTemplate"), right_files[filename].get("isTemplate"))
        compare_issues(
            reporter,
            f"validation.files[{filename}].issues",
            left_files[filename].get("issues", []),
            right_files[filename].get("issues", []),
        )


def compare_workbooks(reporter, left, right, tolerance):
    left_books = by_name(left.get("workbooks", []), "filename")
    right_books = by_name(right.get("workbooks", []), "filename")
    compare_keyset(reporter, "workbooks", left_books.keys(), right_books.keys())

    for filename in sorted(set(left_books) & set(right_books)):
        compare_workbook(reporter, f"workbooks[{filename}]", left_books[filename], right_books[filename], tolerance)


def compare_workbook(reporter, prefix, left_book, right_book, tolerance):
    compare_scalar(reporter, f"{prefix}.filename", left_book.get("filename"), right_book.get("filename"))
    left_sheets = by_name(left_book.get("sheets", []))
    right_sheets = by_name(right_book.get("sheets", []))
    compare_keyset(reporter, f"{prefix}.sheets", left_sheets.keys(), right_sheets.keys())
    for sheet_name in sorted(set(left_sheets) & set(right_sheets)):
        left_sheet = left_sheets[sheet_name]
        right_sheet = right_sheets[sheet_name]
        sheet_prefix = f"{prefix}.sheets[{sheet_name}]"
        compare_scalar(reporter, f"{sheet_prefix}.rowCount", left_sheet.get("rowCount"), right_sheet.get("rowCount"))
        compare_scalar(reporter, f"{sheet_prefix}.columnCount", left_sheet.get("columnCount"), right_sheet.get("columnCount"))

        left_cells = cells_by_position(left_sheet.get("cells", []))
        right_cells = cells_by_position(right_sheet.get("cells", []))
        compare_keyset(reporter, f"{sheet_prefix}.cells", left_cells.keys(), right_cells.keys())
        for position in sorted(set(left_cells) & set(right_cells)):
            left_cell = left_cells[position]
            right_cell = right_cells[position]
            cell_path = f"{sheet_prefix}.{position}"
            compare_scalar(reporter, f"{cell_path}.value", left_cell.get("value"), right_cell.get("value"))
            compare_scalar(reporter, f"{cell_path}.rawValue", left_cell.get("rawValue"), right_cell.get("rawValue"))
            compare_number(reporter, f"{cell_path}.numericValue", left_cell.get("numericValue"), right_cell.get("numericValue"), tolerance)
            compare_scalar(reporter, f"{cell_path}.formatCode", left_cell.get("formatCode"), right_cell.get("formatCode"))
            compare_scalar(reporter, f"{cell_path}.isDate", left_cell.get("isDate"), right_cell.get("isDate"))


def compare_sources(reporter, prefix, left_sources, right_sources):
    compare_scalar(reporter, f"{prefix}.count", len(left_sources), len(right_sources))
    for index, (left_source, right_source) in enumerate(zip(left_sources, right_sources)):
        source_path = f"{prefix}[{index}]"
        compare_scalar(reporter, f"{source_path}.filename", left_source.get("filename"), right_source.get("filename"))
        compare_scalar(reporter, f"{source_path}.filepath", left_source.get("filepath"), right_source.get("filepath"))
        compare_scalar(reporter, f"{source_path}.value", left_source.get("value"), right_source.get("value"))
        compare_scalar(reporter, f"{source_path}.rawValue", left_source.get("rawValue"), right_source.get("rawValue"))
        compare_scalar(reporter, f"{source_path}.state", left_source.get("state"), right_source.get("state"))


def compare_merged_result(reporter, prefix, left_result, right_result, tolerance):
    compare_scalar(reporter, f"{prefix}.sheetName", left_result.get("sheetName"), right_result.get("sheetName"))
    compare_scalar(reporter, f"{prefix}.rowCount", left_result.get("rowCount"), right_result.get("rowCount"))
    compare_scalar(reporter, f"{prefix}.columnCount", left_result.get("columnCount"), right_result.get("columnCount"))
    compare_scalar(reporter, f"{prefix}.sourceFiles", left_result.get("sourceFiles"), right_result.get("sourceFiles"))

    left_cells = cells_by_position(left_result.get("cells", []))
    right_cells = cells_by_position(right_result.get("cells", []))
    compare_keyset(reporter, f"{prefix}.cells", left_cells.keys(), right_cells.keys())
    for position in sorted(set(left_cells) & set(right_cells)):
        left_cell = left_cells[position]
        right_cell = right_cells[position]
        cell_path = f"{prefix}.{position}"
        compare_scalar(reporter, f"{cell_path}.type", left_cell.get("type"), right_cell.get("type"))
        compare_scalar(reporter, f"{cell_path}.displayValue", left_cell.get("displayValue"), right_cell.get("displayValue"))
        compare_number(reporter, f"{cell_path}.sum", left_cell.get("sum"), right_cell.get("sum"), tolerance)
        compare_scalar(reporter, f"{cell_path}.mixedCount", left_cell.get("mixedCount"), right_cell.get("mixedCount"))
        compare_scalar(reporter, f"{cell_path}.singleValue", left_cell.get("singleValue"), right_cell.get("singleValue"))
        compare_scalar(reporter, f"{cell_path}.isOverridden", left_cell.get("isOverridden"), right_cell.get("isOverridden"))
        compare_scalar(reporter, f"{cell_path}.formatCode", left_cell.get("formatCode"), right_cell.get("formatCode"))
        compare_scalar(reporter, f"{cell_path}.isSuspicious", left_cell.get("isSuspicious"), right_cell.get("isSuspicious"))
        compare_sources(reporter, f"{cell_path}.sources", left_cell.get("sources", []), right_cell.get("sources", []))


def compare_merged_results(reporter, left, right, tolerance):
    left_results = by_name(left.get("mergedResults", []), "sheetName")
    right_results = by_name(right.get("mergedResults", []), "sheetName")
    compare_keyset(reporter, "mergedResults", left_results.keys(), right_results.keys())

    for sheet_name in sorted(set(left_results) & set(right_results)):
        compare_merged_result(
            reporter,
            f"mergedResults[{sheet_name}]",
            left_results[sheet_name],
            right_results[sheet_name],
            tolerance,
        )


def compare_schema_probe(reporter, left, right, tolerance):
    left_probe = left.get("schemaProbe")
    right_probe = right.get("schemaProbe")
    if left_probe is None or right_probe is None:
        compare_scalar(reporter, "schemaProbe", left_probe, right_probe)
        return
    compare_merged_result(reporter, "schemaProbe", left_probe, right_probe, tolerance)


def compare_schema_match_probe(reporter, left, right, tolerance):
    left_probe = left.get("schemaMatchProbe")
    right_probe = right.get("schemaMatchProbe")
    if left_probe is None or right_probe is None:
        compare_scalar(reporter, "schemaMatchProbe", left_probe, right_probe)
        return

    prefix = "schemaMatchProbe"
    compare_number(reporter, f"{prefix}.selfSimilarity", left_probe.get("selfSimilarity"), right_probe.get("selfSimilarity"), tolerance)
    for result_name in ["exactMatch", "ambiguousMatch"]:
        left_result = left_probe.get(result_name, {})
        right_result = right_probe.get(result_name, {})
        compare_scalar(reporter, f"{prefix}.{result_name}.kind", left_result.get("kind"), right_result.get("kind"))
        compare_scalar(reporter, f"{prefix}.{result_name}.names", left_result.get("names"), right_result.get("names"))

    left_fingerprint = left_probe.get("workbookFingerprint", {})
    right_fingerprint = right_probe.get("workbookFingerprint", {})
    compare_scalar(reporter, f"{prefix}.workbookFingerprint.sheetNames", left_fingerprint.get("sheetNames"), right_fingerprint.get("sheetNames"))
    left_sheets = by_name(left_fingerprint.get("sheetFingerprints", []), "sheetName")
    right_sheets = by_name(right_fingerprint.get("sheetFingerprints", []), "sheetName")
    compare_keyset(reporter, f"{prefix}.workbookFingerprint.sheetFingerprints", left_sheets.keys(), right_sheets.keys())
    for sheet_name in sorted(set(left_sheets) & set(right_sheets)):
        sheet_prefix = f"{prefix}.workbookFingerprint.sheetFingerprints[{sheet_name}]"
        compare_scalar(reporter, f"{sheet_prefix}.rowCount", left_sheets[sheet_name].get("rowCount"), right_sheets[sheet_name].get("rowCount"))
        compare_scalar(reporter, f"{sheet_prefix}.columnCount", left_sheets[sheet_name].get("columnCount"), right_sheets[sheet_name].get("columnCount"))
        compare_scalar(reporter, f"{sheet_prefix}.layoutHash", left_sheets[sheet_name].get("layoutHash"), right_sheets[sheet_name].get("layoutHash"))
        compare_scalar(reporter, f"{sheet_prefix}.formatHash", left_sheets[sheet_name].get("formatHash"), right_sheets[sheet_name].get("formatHash"))


def compare_export_parse_back(reporter, left, right, tolerance):
    left_export = left.get("exportParseBack")
    right_export = right.get("exportParseBack")
    if left_export is None or right_export is None:
        compare_scalar(reporter, "exportParseBack", left_export, right_export)
        return
    compare_workbook(reporter, "exportParseBack", left_export, right_export, tolerance)


def main():
    parser = argparse.ArgumentParser(description="Compare Swift and C++ xlsOne golden snapshots.")
    parser.add_argument("swift_snapshot")
    parser.add_argument("cpp_snapshot")
    parser.add_argument("--max-details", type=int, default=50)
    parser.add_argument("--tolerance", type=float, default=1e-7)
    args = parser.parse_args()

    left = load(args.swift_snapshot)
    right = load(args.cpp_snapshot)
    reporter = DiffReporter(args.max_details)

    compare_scalar(reporter, "snapshotVersion", left.get("snapshotVersion"), right.get("snapshotVersion"))
    compare_scalar(reporter, "schemaMode", left.get("schemaMode"), right.get("schemaMode"))
    compare_parse_failures(reporter, left, right)
    compare_validation(reporter, left, right)
    compare_workbooks(reporter, left, right, args.tolerance)
    compare_merged_results(reporter, left, right, args.tolerance)
    compare_schema_probe(reporter, left, right, args.tolerance)
    compare_schema_match_probe(reporter, left, right, args.tolerance)
    compare_export_parse_back(reporter, left, right, args.tolerance)

    print(f"Compared {args.swift_snapshot} -> {args.cpp_snapshot}")
    print(f"Differences: {reporter.count}")
    for path, left_value, right_value in reporter.samples:
        print(f"- {path}")
        print(f"  swift: {left_value!r}")
        print(f"  cpp:   {right_value!r}")

    return 1 if reporter.count else 0


if __name__ == "__main__":
    raise SystemExit(main())
