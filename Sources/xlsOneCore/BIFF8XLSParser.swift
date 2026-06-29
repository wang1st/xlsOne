import Foundation

struct BIFF8XLSParser {
    private struct Record {
        let id: UInt16
        let offset: Int
        let body: Data
    }

    private struct BoundSheet {
        let offset: Int
        let name: String
    }

    private struct WorkbookGlobals {
        let sheets: [BoundSheet]
        let sharedStrings: [String]
        let formats: [Int: String]
        let xfFormatIDs: [Int: Int]
    }

    private struct CellAddress: Hashable {
        let row: Int
        let column: Int
    }

    func parseFile(at path: String) throws -> ExcelFile {
        let url = URL(fileURLWithPath: path)
        let fileData = try Data(contentsOf: url)
        let compoundFile = try CompoundFileReader(data: fileData)
        let workbookData = try compoundFile.stream(named: ["Workbook", "Book"])
        let globals = parseWorkbookGlobals(from: workbookData)

        let sheets = globals.sheets.compactMap { sheet -> SheetData? in
            guard sheet.offset >= 0, sheet.offset < workbookData.count else { return nil }
            return parseWorksheet(
                workbookData: workbookData,
                sheet: sheet,
                globals: globals
            )
        }

        guard !sheets.isEmpty else {
            throw ParserError.cannotOpenFile(path)
        }

        return ExcelFile(
            filename: url.lastPathComponent,
            filepath: path,
            sheets: sheets
        )
    }

    private func parseWorkbookGlobals(from data: Data) -> WorkbookGlobals {
        var sheets: [BoundSheet] = []
        var sharedStrings: [String] = []
        var formats: [Int: String] = Self.defaultFormats
        var xfFormatIDs: [Int: Int] = [:]

        let records = parseRecords(in: data)
        var index = 0
        while index < records.count {
            let record = records[index]
            switch record.id {
            case 0x0085:
                if let sheet = parseBoundSheet(record.body) {
                    sheets.append(sheet)
                }
            case 0x00FC:
                let chunks = continuationChunks(records: records, startingAt: index)
                sharedStrings = parseSharedStrings(from: record.body, continuationChunks: chunks)
                index += chunks.count
            case 0x041E:
                if let format = parseFormat(record.body) {
                    formats[format.id] = format.code
                }
            case 0x00E0:
                if record.body.count >= 4 {
                    let xfIndex = xfFormatIDs.count
                    xfFormatIDs[xfIndex] = Int(record.body.xlsUInt16(at: 2))
                }
            case 0x000A:
                return WorkbookGlobals(
                    sheets: sheets.sorted { $0.offset < $1.offset },
                    sharedStrings: sharedStrings,
                    formats: formats,
                    xfFormatIDs: xfFormatIDs
                )
            default:
                break
            }
            index += 1
        }

        return WorkbookGlobals(
            sheets: sheets.sorted { $0.offset < $1.offset },
            sharedStrings: sharedStrings,
            formats: formats,
            xfFormatIDs: xfFormatIDs
        )
    }

    private func parseWorksheet(
        workbookData: Data,
        sheet: BoundSheet,
        globals: WorkbookGlobals
    ) -> SheetData {
        var cells: [CellAddress: CellData] = [:]
        var maxRow = -1
        var maxColumn = -1

        for record in parseRecords(in: workbookData, startingAt: sheet.offset) {
            switch record.id {
            case 0x000A:
                return SheetData(name: sheet.name, rows: buildRows(from: cells, maxRow: maxRow, maxColumn: maxColumn))
            case 0x0200:
                let dimensions = parseDimensions(record.body)
                maxRow = max(maxRow, dimensions.maxRow)
                maxColumn = max(maxColumn, dimensions.maxColumn)
            case 0x00FD:
                guard record.body.count >= 10 else { break }
                let row = Int(record.body.xlsUInt16(at: 0))
                let column = Int(record.body.xlsUInt16(at: 2))
                let xfIndex = Int(record.body.xlsUInt16(at: 4))
                let stringIndex = Int(record.body.xlsUInt32(at: 6))
                let value = stringIndex < globals.sharedStrings.count ? globals.sharedStrings[stringIndex] : ""
                setCell(
                    CellData(value: value, formatCode: formatCode(xfIndex: xfIndex, globals: globals)),
                    row: row,
                    column: column,
                    cells: &cells,
                    maxRow: &maxRow,
                    maxColumn: &maxColumn
                )
            case 0x0204, 0x00D6:
                guard record.body.count >= 8 else { break }
                let row = Int(record.body.xlsUInt16(at: 0))
                let column = Int(record.body.xlsUInt16(at: 2))
                let xfIndex = Int(record.body.xlsUInt16(at: 4))
                let value = parseXLUnicodeString(record.body, offset: 6)?.value ?? ""
                setCell(
                    CellData(value: value, formatCode: formatCode(xfIndex: xfIndex, globals: globals)),
                    row: row,
                    column: column,
                    cells: &cells,
                    maxRow: &maxRow,
                    maxColumn: &maxColumn
                )
            case 0x0203:
                guard record.body.count >= 14 else { break }
                let row = Int(record.body.xlsUInt16(at: 0))
                let column = Int(record.body.xlsUInt16(at: 2))
                let xfIndex = Int(record.body.xlsUInt16(at: 4))
                let number = record.body.xlsDouble(at: 6)
                setCell(
                    numericCell(number, xfIndex: xfIndex, globals: globals),
                    row: row,
                    column: column,
                    cells: &cells,
                    maxRow: &maxRow,
                    maxColumn: &maxColumn
                )
            case 0x027E:
                guard record.body.count >= 10 else { break }
                let row = Int(record.body.xlsUInt16(at: 0))
                let column = Int(record.body.xlsUInt16(at: 2))
                let xfIndex = Int(record.body.xlsUInt16(at: 4))
                let number = decodeRK(record.body.xlsUInt32(at: 6))
                setCell(
                    numericCell(number, xfIndex: xfIndex, globals: globals),
                    row: row,
                    column: column,
                    cells: &cells,
                    maxRow: &maxRow,
                    maxColumn: &maxColumn
                )
            case 0x00BD:
                parseMulRK(record.body, globals: globals).forEach { entry in
                    setCell(
                        entry.cell,
                        row: entry.row,
                        column: entry.column,
                        cells: &cells,
                        maxRow: &maxRow,
                        maxColumn: &maxColumn
                    )
                }
            case 0x0205:
                guard record.body.count >= 8 else { break }
                let row = Int(record.body.xlsUInt16(at: 0))
                let column = Int(record.body.xlsUInt16(at: 2))
                let xfIndex = Int(record.body.xlsUInt16(at: 4))
                let isError = record.body[7] != 0
                let value = isError ? errorValue(record.body[6]) : (record.body[6] == 0 ? "FALSE" : "TRUE")
                setCell(
                    CellData(value: value, formatCode: formatCode(xfIndex: xfIndex, globals: globals)),
                    row: row,
                    column: column,
                    cells: &cells,
                    maxRow: &maxRow,
                    maxColumn: &maxColumn
                )
            case 0x0006:
                parseFormula(record.body, globals: globals).map { entry in
                    setCell(
                        entry.cell,
                        row: entry.row,
                        column: entry.column,
                        cells: &cells,
                        maxRow: &maxRow,
                        maxColumn: &maxColumn
                    )
                }
            default:
                break
            }
        }

        return SheetData(name: sheet.name, rows: buildRows(from: cells, maxRow: maxRow, maxColumn: maxColumn))
    }

    private func parseRecords(in data: Data, startingAt startOffset: Int = 0) -> [Record] {
        var records: [Record] = []
        var offset = startOffset
        while offset + 4 <= data.count {
            let id = data.xlsUInt16(at: offset)
            let length = Int(data.xlsUInt16(at: offset + 2))
            let bodyOffset = offset + 4
            guard bodyOffset + length <= data.count else { break }
            records.append(Record(id: id, offset: offset, body: Data(data[bodyOffset..<bodyOffset + length])))
            offset = bodyOffset + length
            if id == 0x000A, startOffset != 0 {
                break
            }
        }
        return records
    }

    private func continuationChunks(records: [Record], startingAt index: Int) -> [Data] {
        var chunks: [Data] = []
        var nextIndex = index + 1
        while nextIndex < records.count, records[nextIndex].id == 0x003C {
            chunks.append(records[nextIndex].body)
            nextIndex += 1
        }
        return chunks
    }

    private func parseBoundSheet(_ data: Data) -> BoundSheet? {
        guard data.count >= 8 else { return nil }
        let offset = Int(data.xlsUInt32(at: 0))
        let nameLength = Int(data[6])
        let flags = data[7]
        let nameOffset = 8
        let name: String
        if flags & 0x01 == 0 {
            guard nameOffset + nameLength <= data.count else { return nil }
            name = String(data: data[nameOffset..<nameOffset + nameLength], encoding: .windowsCP1252) ?? "Sheet"
        } else {
            let byteLength = nameLength * 2
            guard nameOffset + byteLength <= data.count else { return nil }
            name = String(data: data[nameOffset..<nameOffset + byteLength], encoding: .utf16LittleEndian) ?? "Sheet"
        }
        return BoundSheet(offset: offset, name: name)
    }

    private func parseSharedStrings(from recordBody: Data, continuationChunks: [Data]) -> [String] {
        guard recordBody.count >= 8 else { return [] }
        let uniqueCount = Int(recordBody.xlsUInt32(at: 4))
        let segments = [Data(recordBody[8..<recordBody.count])] + continuationChunks
        var cursor = SharedStringCursor(segments: segments)

        var strings: [String] = []
        while !cursor.isAtEnd, strings.count < uniqueCount {
            guard let parsed = cursor.readXLUnicodeString() else { break }
            strings.append(parsed)
        }
        return strings
    }

    private func parseFormat(_ data: Data) -> (id: Int, code: String)? {
        guard data.count >= 5 else { return nil }
        let id = Int(data.xlsUInt16(at: 0))
        guard let parsed = parseXLUnicodeString(data, offset: 2) else { return nil }
        return (id, parsed.value)
    }

    private func parseXLUnicodeString(_ data: Data, offset: Int) -> (value: String, nextOffset: Int)? {
        guard offset + 3 <= data.count else { return nil }
        let characterCount = Int(data.xlsUInt16(at: offset))
        let flags = data[offset + 2]
        var cursor = offset + 3

        let hasAsianPhonetics = flags & 0x04 != 0
        let hasRichText = flags & 0x08 != 0
        let isWide = flags & 0x01 != 0

        var richTextRunCount = 0
        var extensionByteCount = 0
        if hasRichText {
            guard cursor + 2 <= data.count else { return nil }
            richTextRunCount = Int(data.xlsUInt16(at: cursor))
            cursor += 2
        }
        if hasAsianPhonetics {
            guard cursor + 4 <= data.count else { return nil }
            extensionByteCount = Int(data.xlsUInt32(at: cursor))
            cursor += 4
        }

        let byteCount = characterCount * (isWide ? 2 : 1)
        guard cursor + byteCount <= data.count else { return nil }
        let valueData = data[cursor..<cursor + byteCount]
        let value = String(
            data: valueData,
            encoding: isWide ? .utf16LittleEndian : .windowsCP1252
        ) ?? ""
        cursor += byteCount
        cursor += richTextRunCount * 4
        cursor += extensionByteCount
        guard cursor <= data.count else { return nil }
        return (value, cursor)
    }

    private func parseMulRK(
        _ data: Data,
        globals: WorkbookGlobals
    ) -> [(row: Int, column: Int, cell: CellData)] {
        guard data.count >= 6 else { return [] }
        let row = Int(data.xlsUInt16(at: 0))
        let firstColumn = Int(data.xlsUInt16(at: 2))
        let lastColumn = Int(data.xlsUInt16(at: data.count - 2))
        let count = max(0, lastColumn - firstColumn + 1)

        return (0..<count).compactMap { index in
            let offset = 4 + index * 6
            guard offset + 6 <= data.count - 2 else { return nil }
            let xfIndex = Int(data.xlsUInt16(at: offset))
            let rk = data.xlsUInt32(at: offset + 2)
            return (
                row: row,
                column: firstColumn + index,
                cell: numericCell(decodeRK(rk), xfIndex: xfIndex, globals: globals)
            )
        }
    }

    private func parseDimensions(_ data: Data) -> (maxRow: Int, maxColumn: Int) {
        if data.count >= 14 {
            let rowMac = Int(data.xlsUInt32(at: 4))
            let colMac = Int(data.xlsUInt16(at: 10))
            return (max(-1, rowMac - 1), max(-1, colMac - 1))
        }

        if data.count >= 10 {
            let rowMac = Int(data.xlsUInt16(at: 2))
            let colMac = Int(data.xlsUInt16(at: 6))
            return (max(-1, rowMac - 1), max(-1, colMac - 1))
        }

        return (-1, -1)
    }

    private func parseFormula(
        _ data: Data,
        globals: WorkbookGlobals
    ) -> (row: Int, column: Int, cell: CellData)? {
        guard data.count >= 14 else { return nil }
        let row = Int(data.xlsUInt16(at: 0))
        let column = Int(data.xlsUInt16(at: 2))
        let xfIndex = Int(data.xlsUInt16(at: 4))
        let resultMarker = data.xlsUInt16(at: 12)

        if resultMarker == 0xFFFF {
            let type = data[6]
            let value: String
            switch type {
            case 0:
                value = ""
            case 1:
                value = data[8] == 0 ? "FALSE" : "TRUE"
            case 2:
                value = errorValue(data[8])
            default:
                return nil
            }
            return (row, column, CellData(value: value, formatCode: formatCode(xfIndex: xfIndex, globals: globals)))
        }

        let number = data.xlsDouble(at: 6)
        return (row, column, numericCell(number, xfIndex: xfIndex, globals: globals))
    }

    private func numericCell(_ number: Double, xfIndex: Int, globals: WorkbookGlobals) -> CellData {
        let formatCode = formatCode(xfIndex: xfIndex, globals: globals)
        if isDateFormat(formatCode: formatCode),
           let date = formatExcelDate(number) {
            return CellData(
                value: date,
                rawValue: numberDisplayValue(number),
                numericValue: number,
                formatCode: formatCode,
                isDate: true
            )
        }

        let displayValue = numberDisplayValue(number)
        return CellData(
            value: displayValue,
            rawValue: displayValue,
            numericValue: number,
            formatCode: formatCode,
            isDate: false
        )
    }

    private func setCell(
        _ cell: CellData,
        row: Int,
        column: Int,
        cells: inout [CellAddress: CellData],
        maxRow: inout Int,
        maxColumn: inout Int
    ) {
        cells[CellAddress(row: row, column: column)] = cell
        maxRow = max(maxRow, row)
        maxColumn = max(maxColumn, column)
    }

    private func buildRows(from cells: [CellAddress: CellData], maxRow: Int, maxColumn: Int) -> [[CellData]] {
        guard maxRow >= 0, maxColumn >= 0 else { return [] }
        return (0...maxRow).map { row in
            (0...maxColumn).map { column in
                cells[CellAddress(row: row, column: column)] ?? CellData(value: "")
            }
        }
    }

    private func formatCode(xfIndex: Int, globals: WorkbookGlobals) -> String? {
        guard let formatID = globals.xfFormatIDs[xfIndex] else { return nil }
        return globals.formats[formatID]
    }

    private func isDateFormat(formatCode: String?) -> Bool {
        guard let formatCode else { return false }
        let code = formatCode.lowercased()
        return ["y", "m", "d", "h", "s"].contains { code.contains($0) } &&
            !code.contains("general") &&
            !code.contains("@")
    }

    private func formatExcelDate(_ number: Double) -> String? {
        let base = Date(timeIntervalSince1970: -2209161600)
        let days = number - 2
        guard let date = Calendar(identifier: .gregorian).date(
            byAdding: .day,
            value: Int(days.rounded(.towardZero)),
            to: base
        ) else {
            return nil
        }

        let formatter = DateFormatter()
        formatter.calendar = Calendar(identifier: .gregorian)
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyy-MM-dd"
        return formatter.string(from: date)
    }

    private func numberDisplayValue(_ number: Double) -> String {
        if number.isFinite, number.rounded() == number {
            return String(Int64(number))
        }
        return String(number)
    }

    private func decodeRK(_ rk: UInt32) -> Double {
        let divideBy100 = rk & 0x01 != 0
        let isInteger = rk & 0x02 != 0
        let value: Double

        if isInteger {
            value = Double(Int32(bitPattern: rk & 0xFFFFFFFC) >> 2)
        } else {
            let bits = UInt64(rk & 0xFFFFFFFC) << 32
            value = Double(bitPattern: bits)
        }

        return divideBy100 ? value / 100 : value
    }

    private func errorValue(_ code: UInt8) -> String {
        switch code {
        case 0x00: return "#NULL!"
        case 0x07: return "#DIV/0!"
        case 0x0F: return "#VALUE!"
        case 0x17: return "#REF!"
        case 0x1D: return "#NAME?"
        case 0x24: return "#NUM!"
        case 0x2A: return "#N/A"
        default: return "#ERROR"
        }
    }

    private static let defaultFormats: [Int: String] = [
        0: "General",
        1: "0",
        2: "0.00",
        3: "#,##0",
        4: "#,##0.00",
        9: "0%",
        10: "0.00%",
        11: "0.00E+00",
        12: "# ?/?",
        13: "# ??/??",
        14: "yyyy-MM-dd",
        15: "d-mmm-yy",
        16: "d-mmm",
        17: "mmm-yy",
        18: "h:mm AM/PM",
        19: "h:mm:ss AM/PM",
        20: "h:mm",
        21: "h:mm:ss",
        22: "m/d/yy h:mm",
        37: "#,##0 ;(#,##0)",
        38: "#,##0 ;[Red](#,##0)",
        39: "#,##0.00;(#,##0.00)",
        40: "#,##0.00;[Red](#,##0.00)",
        45: "mm:ss",
        46: "[h]:mm:ss",
        47: "mmss.0",
        48: "##0.0E+0",
        49: "@"
    ]
}

private struct SharedStringCursor {
    private let segments: [Data]
    private var segmentIndex = 0
    private var offset = 0

    init(segments: [Data]) {
        self.segments = segments
    }

    var isAtEnd: Bool {
        var index = segmentIndex
        var cursor = offset
        while index < segments.count {
            if cursor < segments[index].count {
                return false
            }
            index += 1
            cursor = 0
        }
        return true
    }

    mutating func readXLUnicodeString() -> String? {
        guard let characterCount = readUInt16(),
              let flags = readUInt8() else {
            return nil
        }

        var isWide = flags & 0x01 != 0
        let hasAsianPhonetics = flags & 0x04 != 0
        let hasRichText = flags & 0x08 != 0

        var richTextRunCount = 0
        var extensionByteCount = 0
        if hasRichText {
            guard let value = readUInt16() else { return nil }
            richTextRunCount = Int(value)
        }
        if hasAsianPhonetics {
            guard let value = readUInt32() else { return nil }
            extensionByteCount = Int(value)
        }

        guard let value = readCharacters(count: Int(characterCount), isWide: &isWide) else {
            return nil
        }

        guard skip(richTextRunCount * 4 + extensionByteCount) else {
            return nil
        }

        return value
    }

    private mutating func readCharacters(count: Int, isWide: inout Bool) -> String? {
        var value = ""

        for _ in 0..<count {
            let byteCount = isWide ? 2 : 1
            guard ensureTextBytes(byteCount, isWide: &isWide),
                  let bytes = readBytes(byteCount) else {
                return nil
            }

            let scalar = String(
                data: bytes,
                encoding: isWide ? .utf16LittleEndian : .windowsCP1252
            ) ?? ""
            value.append(scalar)
        }

        return value
    }

    private mutating func ensureTextBytes(_ byteCount: Int, isWide: inout Bool) -> Bool {
        while segmentIndex < segments.count {
            let available = segments[segmentIndex].count - offset
            if available >= byteCount {
                return true
            }

            guard available == 0 else {
                return false
            }

            segmentIndex += 1
            offset = 0

            guard segmentIndex < segments.count else {
                return false
            }

            guard let flags = readUInt8() else {
                return false
            }
            isWide = flags & 0x01 != 0
        }

        return false
    }

    private mutating func skip(_ count: Int) -> Bool {
        guard count > 0 else { return true }
        return readBytes(count) != nil
    }

    private mutating func readUInt8() -> UInt8? {
        readBytes(1).map { $0[0] }
    }

    private mutating func readUInt16() -> UInt16? {
        readBytes(2).map { $0.xlsUInt16(at: 0) }
    }

    private mutating func readUInt32() -> UInt32? {
        readBytes(4).map { $0.xlsUInt32(at: 0) }
    }

    private mutating func readBytes(_ count: Int) -> Data? {
        guard count >= 0 else { return nil }
        var result = Data()
        var remaining = count

        while remaining > 0 {
            guard segmentIndex < segments.count else {
                return nil
            }

            let segment = segments[segmentIndex]
            let available = segment.count - offset
            if available == 0 {
                segmentIndex += 1
                offset = 0
                continue
            }

            let chunkSize = min(available, remaining)
            result.append(segment[offset..<offset + chunkSize])
            offset += chunkSize
            remaining -= chunkSize
        }

        return result
    }
}
