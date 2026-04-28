import Foundation

enum CompoundFileError: Error, CustomStringConvertible {
    case invalidHeader
    case unsupportedSectorSize(Int)
    case chainLoop(Int)
    case sectorOutOfBounds(Int)
    case streamNotFound([String])

    var description: String {
        switch self {
        case .invalidHeader:
            return "不是有效的 OLE Compound File。"
        case .unsupportedSectorSize(let size):
            return "暂不支持的 OLE sector size: \(size)。"
        case .chainLoop(let sector):
            return "OLE sector chain 出现循环: \(sector)。"
        case .sectorOutOfBounds(let sector):
            return "OLE sector 越界: \(sector)。"
        case .streamNotFound(let names):
            return "未找到工作簿数据流: \(names.joined(separator: ", "))。"
        }
    }
}

struct CompoundFileReader {
    private struct DirectoryEntry {
        let name: String
        let type: UInt8
        let startingSector: Int
        let streamSize: Int
    }

    private static let endOfChain = UInt32(0xFFFFFFFE)
    private static let freeSector = UInt32(0xFFFFFFFF)
    private static let difatSector = UInt32(0xFFFFFFFC)
    private static let fatSector = UInt32(0xFFFFFFFD)

    private let data: Data
    private let sectorSize: Int
    private let miniSectorSize: Int
    private let miniStreamCutoffSize: Int
    private let firstDirectorySector: Int
    private let firstMiniFATSector: Int
    private let numberOfMiniFATSectors: Int
    private let fat: [UInt32]
    private let miniFAT: [UInt32]
    private let directoryEntries: [DirectoryEntry]
    private let rootEntry: DirectoryEntry?
    private let miniStream: Data

    init(data: Data) throws {
        guard data.count >= 512,
              data.prefix(8).elementsEqual([0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1]) else {
            throw CompoundFileError.invalidHeader
        }

        self.data = data

        let sectorShift = Int(data.xlsUInt16(at: 30))
        let miniSectorShift = Int(data.xlsUInt16(at: 32))
        self.sectorSize = 1 << sectorShift
        self.miniSectorSize = 1 << miniSectorShift
        self.firstDirectorySector = Int(data.xlsUInt32(at: 48))
        self.miniStreamCutoffSize = Int(data.xlsUInt32(at: 56))
        self.firstMiniFATSector = Int(data.xlsUInt32(at: 60))
        self.numberOfMiniFATSectors = Int(data.xlsUInt32(at: 64))

        guard sectorSize == 512 || sectorSize == 4096 else {
            throw CompoundFileError.unsupportedSectorSize(sectorSize)
        }

        let numberOfFATSectors = Int(data.xlsUInt32(at: 44))
        let firstDIFATSector = Int(data.xlsUInt32(at: 68))
        let numberOfDIFATSectors = Int(data.xlsUInt32(at: 72))
        let difat = try Self.readDIFAT(
            data: data,
            sectorSize: sectorSize,
            headerFATSectorCount: numberOfFATSectors,
            firstDIFATSector: firstDIFATSector,
            numberOfDIFATSectors: numberOfDIFATSectors
        )

        self.fat = try Self.readFAT(data: data, sectorSize: sectorSize, fatSectorIDs: difat)

        let directoryData = try Self.readRegularChain(
            data: data,
            sectorSize: sectorSize,
            fat: fat,
            startSector: firstDirectorySector
        )
        self.directoryEntries = Self.parseDirectoryEntries(from: directoryData)
        self.rootEntry = directoryEntries.first { $0.type == 5 }

        if firstMiniFATSector >= 0, numberOfMiniFATSectors > 0 {
            let miniFATData = try Self.readRegularChain(
                data: data,
                sectorSize: sectorSize,
                fat: fat,
                startSector: firstMiniFATSector,
                sectorLimit: numberOfMiniFATSectors
            )
            self.miniFAT = Self.readUInt32Table(from: miniFATData)
        } else {
            self.miniFAT = []
        }

        if let rootEntry, rootEntry.startingSector >= 0, rootEntry.streamSize > 0 {
            self.miniStream = try Self.readRegularChain(
                data: data,
                sectorSize: sectorSize,
                fat: fat,
                startSector: rootEntry.startingSector,
                byteLimit: rootEntry.streamSize
            )
        } else {
            self.miniStream = Data()
        }
    }

    func stream(named candidateNames: [String]) throws -> Data {
        for name in candidateNames {
            if let entry = directoryEntries.first(where: { $0.type == 2 && $0.name.caseInsensitiveCompare(name) == .orderedSame }) {
                return try stream(for: entry)
            }
        }
        throw CompoundFileError.streamNotFound(candidateNames)
    }

    private func stream(for entry: DirectoryEntry) throws -> Data {
        guard entry.startingSector >= 0, entry.streamSize > 0 else {
            return Data()
        }

        if entry.streamSize < miniStreamCutoffSize, !miniFAT.isEmpty, !miniStream.isEmpty {
            return try Self.readMiniChain(
                miniStream: miniStream,
                miniSectorSize: miniSectorSize,
                miniFAT: miniFAT,
                startSector: entry.startingSector,
                byteLimit: entry.streamSize
            )
        }

        return try Self.readRegularChain(
            data: data,
            sectorSize: sectorSize,
            fat: fat,
            startSector: entry.startingSector,
            byteLimit: entry.streamSize
        )
    }

    private static func readDIFAT(
        data: Data,
        sectorSize: Int,
        headerFATSectorCount: Int,
        firstDIFATSector: Int,
        numberOfDIFATSectors: Int
    ) throws -> [Int] {
        var sectorIDs: [Int] = []

        for index in 0..<109 {
            let value = data.xlsUInt32(at: 76 + index * 4)
            if value != freeSector {
                sectorIDs.append(Int(value))
            }
        }

        var current = firstDIFATSector
        for _ in 0..<numberOfDIFATSectors where current >= 0 {
            let sector = try sectorData(in: data, sectorSize: sectorSize, sectorID: current)
            let entriesPerDIFATSector = sectorSize / 4 - 1
            for index in 0..<entriesPerDIFATSector {
                let value = sector.xlsUInt32(at: index * 4)
                if value != freeSector {
                    sectorIDs.append(Int(value))
                }
            }
            let next = sector.xlsUInt32(at: entriesPerDIFATSector * 4)
            current = next == endOfChain ? -1 : Int(next)
        }

        if sectorIDs.count > headerFATSectorCount {
            return Array(sectorIDs.prefix(headerFATSectorCount))
        }
        return sectorIDs
    }

    private static func readFAT(data: Data, sectorSize: Int, fatSectorIDs: [Int]) throws -> [UInt32] {
        var result: [UInt32] = []
        for sectorID in fatSectorIDs {
            let sector = try sectorData(in: data, sectorSize: sectorSize, sectorID: sectorID)
            result.append(contentsOf: readUInt32Table(from: sector))
        }
        return result
    }

    private static func readUInt32Table(from data: Data) -> [UInt32] {
        stride(from: 0, to: data.count - data.count % 4, by: 4).map {
            data.xlsUInt32(at: $0)
        }
    }

    private static func readRegularChain(
        data: Data,
        sectorSize: Int,
        fat: [UInt32],
        startSector: Int,
        sectorLimit: Int? = nil,
        byteLimit: Int? = nil
    ) throws -> Data {
        var result = Data()
        var seen: Set<Int> = []
        var current = startSector
        var sectorCount = 0

        while current >= 0 {
            if seen.contains(current) {
                throw CompoundFileError.chainLoop(current)
            }
            seen.insert(current)

            let sector = try sectorData(in: data, sectorSize: sectorSize, sectorID: current)
            result.append(sector)
            sectorCount += 1

            if let sectorLimit, sectorCount >= sectorLimit {
                break
            }
            guard current < fat.count else {
                throw CompoundFileError.sectorOutOfBounds(current)
            }

            let next = fat[current]
            if next == endOfChain || next == freeSector || next == fatSector || next == difatSector {
                break
            }
            current = Int(next)
        }

        if let byteLimit, result.count > byteLimit {
            result.removeSubrange(byteLimit..<result.count)
        }
        return result
    }

    private static func readMiniChain(
        miniStream: Data,
        miniSectorSize: Int,
        miniFAT: [UInt32],
        startSector: Int,
        byteLimit: Int
    ) throws -> Data {
        var result = Data()
        var seen: Set<Int> = []
        var current = startSector

        while current >= 0 {
            if seen.contains(current) {
                throw CompoundFileError.chainLoop(current)
            }
            seen.insert(current)

            let offset = current * miniSectorSize
            guard offset >= 0, offset < miniStream.count else {
                throw CompoundFileError.sectorOutOfBounds(current)
            }
            result.append(Data(miniStream[offset..<min(offset + miniSectorSize, miniStream.count)]))

            guard current < miniFAT.count else {
                throw CompoundFileError.sectorOutOfBounds(current)
            }
            let next = miniFAT[current]
            if next == endOfChain || next == freeSector {
                break
            }
            current = Int(next)
        }

        if result.count > byteLimit {
            result.removeSubrange(byteLimit..<result.count)
        }
        return result
    }

    private static func sectorData(in data: Data, sectorSize: Int, sectorID: Int) throws -> Data {
        let offset = (sectorID + 1) * sectorSize
        guard sectorID >= 0, offset >= 0, offset + sectorSize <= data.count else {
            throw CompoundFileError.sectorOutOfBounds(sectorID)
        }
        return Data(data[offset..<offset + sectorSize])
    }

    private static func parseDirectoryEntries(from data: Data) -> [DirectoryEntry] {
        stride(from: 0, to: data.count - data.count % 128, by: 128).compactMap { offset in
            let nameLength = Int(data.xlsUInt16(at: offset + 64))
            guard nameLength >= 2, offset + nameLength <= data.count else { return nil }

            let nameBytes = Data(data[(offset)..<(offset + nameLength - 2)])
            let name = String(data: nameBytes, encoding: .utf16LittleEndian) ?? ""
            let type = data[offset + 66]
            let startingSectorValue = data.xlsUInt32(at: offset + 116)
            let streamSizeLow = UInt64(data.xlsUInt32(at: offset + 120))
            let streamSizeHigh = UInt64(data.xlsUInt32(at: offset + 124))
            let streamSize = Int((streamSizeHigh << 32) | streamSizeLow)

            return DirectoryEntry(
                name: name,
                type: type,
                startingSector: startingSectorValue == endOfChain ? -1 : Int(startingSectorValue),
                streamSize: streamSize
            )
        }
    }
}

extension Data {
    func xlsUInt16(at offset: Int) -> UInt16 {
        guard offset + 2 <= count else { return 0 }
        return withUnsafeBytes { $0.loadUnaligned(fromByteOffset: offset, as: UInt16.self) }.littleEndian
    }

    func xlsUInt32(at offset: Int) -> UInt32 {
        guard offset + 4 <= count else { return 0 }
        return withUnsafeBytes { $0.loadUnaligned(fromByteOffset: offset, as: UInt32.self) }.littleEndian
    }

    func xlsInt32(at offset: Int) -> Int32 {
        Int32(bitPattern: xlsUInt32(at: offset))
    }

    func xlsDouble(at offset: Int) -> Double {
        guard offset + 8 <= count else { return 0 }
        let bitPattern = withUnsafeBytes { $0.loadUnaligned(fromByteOffset: offset, as: UInt64.self) }.littleEndian
        return Double(bitPattern: bitPattern)
    }
}
