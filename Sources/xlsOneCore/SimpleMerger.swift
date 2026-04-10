import Foundation

/// 简单的合并引擎
public struct SimpleMerger {

    public init() {}

    /// 合并多个文件的指定工作表
    /// - Parameters:
    ///   - files: 要合并的Excel文件列表
    ///   - sheetName: 工作表名称
    /// - Returns: 合并结果
    public func merge(files: [ExcelFile], sheetName: String) -> MergedResult {
        // 收集所有文件中的该工作表数据
        var sheetDataList: [(filename: String, sheet: SheetData)] = []

        for file in files {
            if let sheet = file.sheets.first(where: { $0.name == sheetName }) {
                sheetDataList.append((filename: file.filename, sheet: sheet))
            }
        }

        // 如果没有找到工作表，返回空结果
        guard !sheetDataList.isEmpty else {
            return MergedResult(sheetName: sheetName, rows: [], sourceFiles: files.map { $0.filename })
        }

        // 确定最大行数和列数
        let maxRows = sheetDataList.map { $0.sheet.rows.count }.max() ?? 0
        let maxCols = sheetDataList.flatMap { $0.sheet.rows.map { $0.count } }.max() ?? 0

        // 合并每个单元格
        var mergedRows: [[MergedCell]] = []

        for rowIdx in 0..<maxRows {
            var mergedRow: [MergedCell] = []

            for colIdx in 0..<maxCols {
                // 收集该位置的所有单元格
                var cellData: [(filename: String, cell: CellData?)] = []

                for (filename, sheet) in sheetDataList {
                    let cell = sheet.cellAt(row: rowIdx, col: colIdx)
                    cellData.append((filename: filename, cell: cell))
                }

                let mergedCell = MergedCell.from(cells: cellData)
                mergedRow.append(mergedCell)
                }

            mergedRows.append(mergedRow)
        }

        return MergedResult(
            sheetName: sheetName,
            rows: mergedRows,
            sourceFiles: sheetDataList.map { $0.filename }
        )
    }

    /// 合并所有文件的第一个工作表（当工作表名称不匹配时使用）
    public func mergeFirstSheets(from files: [ExcelFile]) -> MergedResult {
        var sheetDataList: [(filename: String, sheet: SheetData)] = []

        for file in files {
            if let firstSheet = file.sheets.first {
                sheetDataList.append((filename: file.filename, sheet: firstSheet))
            }
        }

        guard !sheetDataList.isEmpty else {
            return MergedResult(sheetName: "Sheet1", rows: [], sourceFiles: files.map { $0.filename })
        }

        // 使用第一个工作表的名称
        let sheetName = sheetDataList.first?.sheet.name ?? "Sheet1"

        // 确定最大行数和列数
        let maxRows = sheetDataList.map { $0.sheet.rows.count }.max() ?? 0
        let maxCols = sheetDataList.flatMap { $0.sheet.rows.map { $0.count } }.max() ?? 0

        // 合并每个单元格
        var mergedRows: [[MergedCell]] = []

        for rowIdx in 0..<maxRows {
            var mergedRow: [MergedCell] = []

            for colIdx in 0..<maxCols {
                var cellData: [(filename: String, cell: CellData?)] = []

                for (filename, sheet) in sheetDataList {
                    let cell = sheet.cellAt(row: rowIdx, col: colIdx)
                    cellData.append((filename: filename, cell: cell))
                }

                let mergedCell = MergedCell.from(cells: cellData)
                mergedRow.append(mergedCell)
            }

            mergedRows.append(mergedRow)
        }

        return MergedResult(
            sheetName: sheetName,
            rows: mergedRows,
            sourceFiles: sheetDataList.map { $0.filename }
        )
    }

    /// 获取所有可用的工作表名称
    public func availableSheetNames(from files: [ExcelFile]) -> [String] {
        var names: Set<String> = []
        for file in files {
            for sheet in file.sheets {
                names.insert(sheet.name)
            }
        }
        return Array(names).sorted()
    }
}
