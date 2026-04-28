import XCTest
@testable import xlsOneCore

final class BIFF8XLSParserTests: XCTestCase {
    func testParserReadsNativeBIFF8Workbook() async throws {
        let url = URL(fileURLWithPath: NSTemporaryDirectory())
            .appendingPathComponent("xlsone-biff8-\(UUID().uuidString).xls")
        defer {
            try? FileManager.default.removeItem(at: url)
        }

        try MinimalXLSFixture.makeWorkbook().write(to: url)

        let file = try await ExcelParser().parseFile(at: url.path)

        XCTAssertEqual(file.filename, url.lastPathComponent)
        XCTAssertEqual(file.filepath, url.path)
        XCTAssertEqual(file.sheets.map(\.name), ["Sheet1"])
        XCTAssertEqual(file.sheets[0].rows[0][0].value, "Name")
        XCTAssertEqual(file.sheets[0].rows[0][1].value, "Amount")
        XCTAssertEqual(file.sheets[0].rows[1][0].value, "Alice")
        XCTAssertEqual(file.sheets[0].rows[1][1].value, "1234")
        XCTAssertEqual(file.sheets[0].rows[1][1].numericValue, 1234)
    }

    func testParserReadsLocalXianjuWorkbookWhenPresent() async throws {
        let root = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
        let xlsURL = root
            .appendingPathComponent("仙居县")
            .appendingPathComponent("仙居县朱溪镇人民政府2025乡镇报表主体信息表.xls")
        let xlsxURL = root
            .appendingPathComponent("仙居县")
            .appendingPathComponent("仙居县朱溪镇人民政府2025乡镇报表主体信息表.xlsx")

        guard FileManager.default.fileExists(atPath: xlsURL.path),
              FileManager.default.fileExists(atPath: xlsxURL.path) else {
            throw XCTSkip("Local Xianju .xls/.xlsx fixtures are not present.")
        }

        let parser = ExcelParser()
        let xlsFile = try await parser.parseFile(at: xlsURL.path)
        let xlsxFile = try await parser.parseFile(at: xlsxURL.path)

        XCTAssertEqual(xlsFile.sheets.map(\.name), xlsxFile.sheets.map(\.name))
        XCTAssertFalse(xlsFile.sheets.isEmpty)

        for (sheetIndex, xlsSheet) in xlsFile.sheets.enumerated() {
            let xlsxSheet = xlsxFile.sheets[sheetIndex]
            XCTAssertEqual(xlsSheet.rows.count, xlsxSheet.rows.count, "Row count mismatch in \(xlsSheet.name)")

            for (rowIndex, xlsRow) in xlsSheet.rows.enumerated() {
                let xlsxRow = xlsxSheet.rows[rowIndex]
                XCTAssertEqual(xlsRow.count, xlsxRow.count, "Column count mismatch in \(xlsSheet.name) row \(rowIndex + 1)")

                for (columnIndex, xlsCell) in xlsRow.enumerated() {
                    let xlsxCell = xlsxRow[columnIndex]
                    XCTAssertCellValueEquivalent(
                        xlsCell.value,
                        xlsxCell.value,
                        sheetName: xlsSheet.name,
                        row: rowIndex + 1,
                        column: columnIndex + 1
                    )
                }
            }
        }
    }

    private func XCTAssertCellValueEquivalent(
        _ lhs: String,
        _ rhs: String,
        sheetName: String,
        row: Int,
        column: Int,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        if lhs == rhs {
            return
        }

        if let lhsNumber = Double(lhs),
           let rhsNumber = Double(rhs),
           abs(lhsNumber - rhsNumber) < 0.0000001 {
            return
        }

        XCTFail(
            "Value mismatch in \(sheetName) R\(row)C\(column): \(lhs) != \(rhs)",
            file: file,
            line: line
        )
    }
}

private enum MinimalXLSFixture {
    static func makeWorkbook() -> Data {
        var sst = Data()
        sst.appendUInt32LE(4)
        sst.appendUInt32LE(4)
        sst.appendXLString("Name")
        sst.appendXLString("Alice")
        sst.appendXLString("Amount")
        sst.appendXLString("Code")

        let globalBOF = record(0x0809, workbookBOFBody(kind: 0x0005))
        let sstRecord = record(0x00FC, sst)
        let xfDefault = record(0x00E0, xfBody(formatID: 0))
        let globalsWithoutBoundSheet = globalBOF + xfDefault + sstRecord
        let boundSheetLength = 4 + 8 + "Sheet1".utf8.count
        let sheetOffset = globalsWithoutBoundSheet.count + boundSheetLength + 4

        var boundSheet = Data()
        boundSheet.appendUInt32LE(UInt32(sheetOffset))
        boundSheet.appendUInt16LE(0)
        boundSheet.appendUInt8(6)
        boundSheet.appendUInt8(0)
        boundSheet.append("Sheet1".data(using: .windowsCP1252)!)

        let globals = globalBOF + record(0x0085, boundSheet) + xfDefault + sstRecord + record(0x000A, Data())

        var sheet = Data()
        sheet.append(record(0x0809, workbookBOFBody(kind: 0x0010)))
        sheet.append(labelSST(row: 0, column: 0, sstIndex: 0))
        sheet.append(labelSST(row: 0, column: 1, sstIndex: 2))
        sheet.append(labelSST(row: 1, column: 0, sstIndex: 1))
        sheet.append(number(row: 1, column: 1, value: 1234))
        sheet.append(record(0x000A, Data()))

        var workbook = globals + sheet
        if workbook.count < 4096 {
            workbook.append(Data(repeating: 0, count: 4096 - workbook.count))
        }

        return compoundFile(workbookStream: workbook)
    }

    private static func workbookBOFBody(kind: UInt16) -> Data {
        var data = Data()
        data.appendUInt16LE(0x0600)
        data.appendUInt16LE(kind)
        data.appendUInt16LE(0x0DBB)
        data.appendUInt16LE(0x07CC)
        data.appendUInt32LE(0x00000041)
        data.appendUInt32LE(0x00000006)
        return data
    }

    private static func xfBody(formatID: UInt16) -> Data {
        var data = Data(repeating: 0, count: 20)
        data.writeUInt16LE(formatID, at: 2)
        return data
    }

    private static func labelSST(row: UInt16, column: UInt16, sstIndex: UInt32) -> Data {
        var body = Data()
        body.appendUInt16LE(row)
        body.appendUInt16LE(column)
        body.appendUInt16LE(0)
        body.appendUInt32LE(sstIndex)
        return record(0x00FD, body)
    }

    private static func number(row: UInt16, column: UInt16, value: Double) -> Data {
        var body = Data()
        body.appendUInt16LE(row)
        body.appendUInt16LE(column)
        body.appendUInt16LE(0)
        body.appendDoubleLE(value)
        return record(0x0203, body)
    }

    private static func record(_ id: UInt16, _ body: Data) -> Data {
        var data = Data()
        data.appendUInt16LE(id)
        data.appendUInt16LE(UInt16(body.count))
        data.append(body)
        return data
    }

    private static func compoundFile(workbookStream: Data) -> Data {
        let sectorSize = 512
        let workbookSectorCount = workbookStream.count / sectorSize
        let directorySector = UInt32(workbookSectorCount)
        let fatSector = UInt32(workbookSectorCount + 1)

        var header = Data(repeating: 0, count: 512)
        header.replaceSubrange(0..<8, with: Data([0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1]))
        header.writeUInt16LE(0x003E, at: 24)
        header.writeUInt16LE(0x0003, at: 26)
        header.writeUInt16LE(0xFFFE, at: 28)
        header.writeUInt16LE(9, at: 30)
        header.writeUInt16LE(6, at: 32)
        header.writeUInt32LE(0, at: 40)
        header.writeUInt32LE(1, at: 44)
        header.writeUInt32LE(directorySector, at: 48)
        header.writeUInt32LE(4096, at: 56)
        header.writeUInt32LE(0xFFFFFFFE, at: 60)
        header.writeUInt32LE(0, at: 64)
        header.writeUInt32LE(0xFFFFFFFE, at: 68)
        header.writeUInt32LE(0, at: 72)
        header.writeUInt32LE(fatSector, at: 76)
        for offset in stride(from: 80, to: 512, by: 4) {
            header.writeUInt32LE(0xFFFFFFFF, at: offset)
        }

        var directory = Data(repeating: 0, count: sectorSize)
        writeDirectoryEntry(
            name: "Root Entry",
            type: 5,
            startSector: 0xFFFFFFFE,
            streamSize: 0,
            into: &directory,
            offset: 0
        )
        writeDirectoryEntry(
            name: "Workbook",
            type: 2,
            startSector: 0,
            streamSize: UInt64(workbookStream.count),
            into: &directory,
            offset: 128
        )

        var fat = Data(repeating: 0xFF, count: sectorSize)
        for sector in 0..<workbookSectorCount {
            let next: UInt32 = sector == workbookSectorCount - 1 ? 0xFFFFFFFE : UInt32(sector + 1)
            fat.writeUInt32LE(next, at: sector * 4)
        }
        fat.writeUInt32LE(0xFFFFFFFE, at: Int(directorySector) * 4)
        fat.writeUInt32LE(0xFFFFFFFD, at: Int(fatSector) * 4)

        return header + workbookStream + directory + fat
    }

    private static func writeDirectoryEntry(
        name: String,
        type: UInt8,
        startSector: UInt32,
        streamSize: UInt64,
        into data: inout Data,
        offset: Int
    ) {
        let nameData = (name + "\0").data(using: .utf16LittleEndian)!
        data.replaceSubrange(offset..<offset + nameData.count, with: nameData)
        data.writeUInt16LE(UInt16(nameData.count), at: offset + 64)
        data.writeUInt8(type, at: offset + 66)
        data.writeUInt32LE(0xFFFFFFFF, at: offset + 68)
        data.writeUInt32LE(0xFFFFFFFF, at: offset + 72)
        data.writeUInt32LE(0xFFFFFFFF, at: offset + 76)
        data.writeUInt32LE(startSector, at: offset + 116)
        data.writeUInt32LE(UInt32(streamSize & 0xFFFFFFFF), at: offset + 120)
        data.writeUInt32LE(UInt32(streamSize >> 32), at: offset + 124)
    }
}

private extension Data {
    mutating func appendUInt8(_ value: UInt8) {
        append(Data([value]))
    }

    mutating func appendUInt16LE(_ value: UInt16) {
        var littleEndian = value.littleEndian
        append(Data(bytes: &littleEndian, count: 2))
    }

    mutating func appendUInt32LE(_ value: UInt32) {
        var littleEndian = value.littleEndian
        append(Data(bytes: &littleEndian, count: 4))
    }

    mutating func appendDoubleLE(_ value: Double) {
        var littleEndian = value.bitPattern.littleEndian
        append(Data(bytes: &littleEndian, count: 8))
    }

    mutating func appendXLString(_ value: String) {
        let bytes = value.data(using: .windowsCP1252)!
        appendUInt16LE(UInt16(value.count))
        appendUInt8(0)
        append(bytes)
    }

    mutating func writeUInt8(_ value: UInt8, at offset: Int) {
        replaceSubrange(offset..<offset + 1, with: Data([value]))
    }

    mutating func writeUInt16LE(_ value: UInt16, at offset: Int) {
        var littleEndian = value.littleEndian
        replaceSubrange(offset..<offset + 2, with: Data(bytes: &littleEndian, count: 2))
    }

    mutating func writeUInt32LE(_ value: UInt32, at offset: Int) {
        var littleEndian = value.littleEndian
        replaceSubrange(offset..<offset + 4, with: Data(bytes: &littleEndian, count: 4))
    }
}
