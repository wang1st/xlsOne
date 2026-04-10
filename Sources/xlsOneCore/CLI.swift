import Foundation

/// 命令行测试工具
public struct XlsOneCLI {

    public init() {}

    /// 运行测试
    public func run(args: [String]) async {
        print("=== xlsOne CLI 测试工具 ===\n")

        let parser = ExcelParser()
        let merger = SimpleMerger()

        // 获取仙居县目录下的所有xlsx文件
        let xianjuDir = "/Users/ethan/xlsOne/仙居县"
        let fileManager = FileManager.default

        guard let files = try? fileManager.contentsOfDirectory(atPath: xianjuDir) else {
            print("无法读取目录: \(xianjuDir)")
            return
        }

        let xlsxFiles = files
            .filter { $0.hasSuffix(".xlsx") }
            .map { "\(xianjuDir)/\($0)" }
            .sorted()

        print("找到 \(xlsxFiles.count) 个Excel文件:")
        for path in xlsxFiles {
            print("  - \(URL(fileURLWithPath: path).lastPathComponent)")
        }

        print("\n正在解析文件...")

        do {
            let excelFiles = try await parser.parseFiles(at: xlsxFiles)
            print("成功解析 \(excelFiles.count) 个文件")

            // 显示每个文件的工作表
            for file in excelFiles {
                print("\n文件: \(file.filename)")
                print("  工作表: \(file.sheets.map { $0.name }.joined(separator: ", "))")
            }

            // 获取所有工作表名称
            let sheetNames = merger.availableSheetNames(from: excelFiles)
            print("\n所有工作表名称:")
            for name in sheetNames {
                print("  - \(name)")
            }

            // 合并每个工作表
            for sheetName in sheetNames {
                print("\n" + String(repeating: "=", count: 60))
                print("正在合并工作表: \(sheetName)")
                print(String(repeating: "=", count: 60))

                let result = merger.merge(files: excelFiles, sheetName: sheetName)

                print("合并完成!")
                print("  来源文件: \(result.sourceFiles.count) 个")
                print("  行数: \(result.rows.count)")
                if let maxCols = result.rows.map({ $0.count }).max() {
                    print("  最大列数: \(maxCols)")
                }

                // 显示前15行数据
                print("\n前15行数据预览:")
                print(String(repeating: "-", count: 80))
                for (rowIdx, row) in result.rows.prefix(15).enumerated() {
                    let values = row.prefix(8).map { cell in
                        let prefix: String
                        switch cell.type {
                        case .sum: prefix = "∑"
                        case .mixed: prefix = "(混合)"
                        case .label: prefix = ""
                        case .single: prefix = ""
                        }
                        let display = cell.displayValue
                        if display.count > 12 {
                            return String(display.prefix(12)) + "..."
                        }
                        return prefix + display
                    }
                    let rowStr = values.joined(separator: " | ")
                    print(String(format: "%3d: %@", rowIdx + 1, rowStr))
                }

                // 导出为HTML
                let exporter = ExcelExporter()
                let html = exporter.exportToHTML(result: result)
                let safeName = sheetName.replacingOccurrences(of: "/", with: "_")
                let outputPath = "/Users/ethan/xlsOne/output_\(safeName).html"

                // 确保输出目录存在
                let outputDir = URL(fileURLWithPath: outputPath).deletingLastPathComponent().path
                try? fileManager.createDirectory(atPath: outputDir, withIntermediateDirectories: true)

                try html.write(toFile: outputPath, atomically: true, encoding: .utf8)
                print("\n已导出HTML到: \(outputPath)")
            }

            print("\n✅ 所有工作表处理完成!")

        } catch {
            print("错误: \(error)")
        }
    }
}
