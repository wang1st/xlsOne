#include "compound_file_reader.hpp"

#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <optional>
#include <stdexcept>

namespace xlsone {

namespace {

quint16 u16(const QByteArray& data, qsizetype offset)
{
    if (offset < 0 || offset + 2 > data.size()) {
        return 0;
    }
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData() + offset));
}

quint32 u32(const QByteArray& data, qsizetype offset)
{
    if (offset < 0 || offset + 4 > data.size()) {
        return 0;
    }
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(data.constData() + offset));
}

int sectorIDFromUInt32(quint32 value)
{
    return value == 0xfffffffe ? -1 : static_cast<int>(value);
}

} // namespace

CompoundFileReader::CompoundFileReader(QByteArray data) : data_(std::move(data))
{
    static const QByteArray magic = QByteArray::fromHex("d0cf11e0a1b11ae1");
    if (data_.size() < 512 || data_.left(8) != magic) {
        throw std::runtime_error("不是有效的 OLE Compound File");
    }

    const int sectorShift = u16(data_, 30);
    const int miniSectorShift = u16(data_, 32);
    sectorSize_ = 1 << sectorShift;
    miniSectorSize_ = 1 << miniSectorShift;
    firstDirectorySector_ = sectorIDFromUInt32(u32(data_, 48));
    miniStreamCutoffSize_ = static_cast<int>(u32(data_, 56));
    firstMiniFATSector_ = sectorIDFromUInt32(u32(data_, 60));
    numberOfMiniFATSectors_ = static_cast<int>(u32(data_, 64));

    if (sectorSize_ != 512 && sectorSize_ != 4096) {
        throw std::runtime_error(QStringLiteral("暂不支持的 OLE sector size: %1").arg(sectorSize_).toStdString());
    }

    const int numberOfFATSectors = static_cast<int>(u32(data_, 44));
    const int firstDIFATSector = sectorIDFromUInt32(u32(data_, 68));
    const int numberOfDIFATSectors = static_cast<int>(u32(data_, 72));

    const auto difat = readDIFAT(data_, sectorSize_, numberOfFATSectors, firstDIFATSector, numberOfDIFATSectors);
    fat_ = readFAT(data_, sectorSize_, difat);

    const auto directoryData = readRegularChain(data_, sectorSize_, fat_, firstDirectorySector_);
    directoryEntries_ = parseDirectoryEntries(directoryData);

    if (firstMiniFATSector_ >= 0 && numberOfMiniFATSectors_ > 0) {
        const auto miniFATData = readRegularChain(
            data_,
            sectorSize_,
            fat_,
            firstMiniFATSector_,
            numberOfMiniFATSectors_
        );
        miniFAT_ = readUInt32Table(miniFATData);
    }

    const auto root = std::find_if(directoryEntries_.begin(), directoryEntries_.end(), [](const DirectoryEntry& entry) {
        return entry.type == 5;
    });
    if (root != directoryEntries_.end() && root->startingSector >= 0 && root->streamSize > 0) {
        miniStream_ = readRegularChain(data_, sectorSize_, fat_, root->startingSector, std::nullopt, root->streamSize);
    }
}

QByteArray CompoundFileReader::stream(const QStringList& candidateNames) const
{
    for (const auto& name : candidateNames) {
        const auto iterator = std::find_if(directoryEntries_.begin(), directoryEntries_.end(), [&](const DirectoryEntry& entry) {
            return entry.type == 2 && entry.name.compare(name, Qt::CaseInsensitive) == 0;
        });
        if (iterator != directoryEntries_.end()) {
            return streamFor(*iterator);
        }
    }
    throw std::runtime_error(QStringLiteral("未找到工作簿数据流: %1").arg(candidateNames.join(QStringLiteral(", "))).toStdString());
}

QByteArray CompoundFileReader::streamFor(const DirectoryEntry& entry) const
{
    if (entry.startingSector < 0 || entry.streamSize <= 0) {
        return {};
    }
    if (entry.streamSize < miniStreamCutoffSize_ && !miniFAT_.empty() && !miniStream_.isEmpty()) {
        return readMiniChain(miniStream_, miniSectorSize_, miniFAT_, entry.startingSector, entry.streamSize);
    }
    return readRegularChain(data_, sectorSize_, fat_, entry.startingSector, std::nullopt, entry.streamSize);
}

std::vector<int> CompoundFileReader::readDIFAT(
    const QByteArray& data,
    int sectorSize,
    int headerFATSectorCount,
    int firstDIFATSector,
    int numberOfDIFATSectors
)
{
    std::vector<int> sectorIDs;
    for (int index = 0; index < 109; ++index) {
        const quint32 value = u32(data, 76 + index * 4);
        if (value != FreeSector) {
            sectorIDs.push_back(static_cast<int>(value));
        }
    }

    int current = firstDIFATSector;
    for (int count = 0; count < numberOfDIFATSectors && current >= 0; ++count) {
        const auto sector = sectorData(data, sectorSize, current);
        const int entriesPerDIFATSector = sectorSize / 4 - 1;
        for (int index = 0; index < entriesPerDIFATSector; ++index) {
            const quint32 value = u32(sector, index * 4);
            if (value != FreeSector) {
                sectorIDs.push_back(static_cast<int>(value));
            }
        }
        const quint32 next = u32(sector, entriesPerDIFATSector * 4);
        current = next == EndOfChain ? -1 : static_cast<int>(next);
    }

    if (sectorIDs.size() > static_cast<size_t>(headerFATSectorCount)) {
        sectorIDs.resize(static_cast<size_t>(headerFATSectorCount));
    }
    return sectorIDs;
}

std::vector<quint32> CompoundFileReader::readFAT(const QByteArray& data, int sectorSize, const std::vector<int>& fatSectorIDs)
{
    std::vector<quint32> result;
    for (const int sectorID : fatSectorIDs) {
        const auto sector = sectorData(data, sectorSize, sectorID);
        const auto values = readUInt32Table(sector);
        result.insert(result.end(), values.begin(), values.end());
    }
    return result;
}

std::vector<quint32> CompoundFileReader::readUInt32Table(const QByteArray& data)
{
    std::vector<quint32> result;
    for (qsizetype offset = 0; offset + 4 <= data.size(); offset += 4) {
        result.push_back(u32(data, offset));
    }
    return result;
}

QByteArray CompoundFileReader::readRegularChain(
    const QByteArray& data,
    int sectorSize,
    const std::vector<quint32>& fat,
    int startSector,
    std::optional<int> sectorLimit,
    std::optional<qsizetype> byteLimit
)
{
    QByteArray result;
    QSet<int> seen;
    int current = startSector;
    int sectorCount = 0;

    while (current >= 0) {
        if (seen.contains(current)) {
            throw std::runtime_error(QStringLiteral("OLE sector chain 出现循环: %1").arg(current).toStdString());
        }
        seen.insert(current);

        result.append(sectorData(data, sectorSize, current));
        ++sectorCount;

        if (sectorLimit.has_value() && sectorCount >= *sectorLimit) {
            break;
        }
        if (current >= static_cast<int>(fat.size())) {
            throw std::runtime_error(QStringLiteral("OLE sector 越界: %1").arg(current).toStdString());
        }

        const quint32 next = fat[static_cast<size_t>(current)];
        if (next == EndOfChain || next == FreeSector || next == FatSector || next == DifatSector) {
            break;
        }
        current = static_cast<int>(next);
    }

    if (byteLimit.has_value() && result.size() > *byteLimit) {
        result.truncate(*byteLimit);
    }
    return result;
}

QByteArray CompoundFileReader::readMiniChain(
    const QByteArray& miniStream,
    int miniSectorSize,
    const std::vector<quint32>& miniFAT,
    int startSector,
    qsizetype byteLimit
)
{
    QByteArray result;
    QSet<int> seen;
    int current = startSector;

    while (current >= 0) {
        if (seen.contains(current)) {
            throw std::runtime_error(QStringLiteral("OLE mini sector chain 出现循环: %1").arg(current).toStdString());
        }
        seen.insert(current);

        const qsizetype offset = current * miniSectorSize;
        if (offset < 0 || offset >= miniStream.size()) {
            throw std::runtime_error(QStringLiteral("OLE mini sector 越界: %1").arg(current).toStdString());
        }
        result.append(miniStream.mid(offset, std::min<qsizetype>(miniSectorSize, miniStream.size() - offset)));

        if (current >= static_cast<int>(miniFAT.size())) {
            throw std::runtime_error(QStringLiteral("OLE mini FAT 越界: %1").arg(current).toStdString());
        }
        const quint32 next = miniFAT[static_cast<size_t>(current)];
        if (next == EndOfChain || next == FreeSector) {
            break;
        }
        current = static_cast<int>(next);
    }

    if (result.size() > byteLimit) {
        result.truncate(byteLimit);
    }
    return result;
}

QByteArray CompoundFileReader::sectorData(const QByteArray& data, int sectorSize, int sectorID)
{
    const qsizetype offset = (sectorID + 1) * sectorSize;
    if (sectorID < 0 || offset < 0 || offset + sectorSize > data.size()) {
        throw std::runtime_error(QStringLiteral("OLE sector 越界: %1").arg(sectorID).toStdString());
    }
    return data.mid(offset, sectorSize);
}

std::vector<CompoundFileReader::DirectoryEntry> CompoundFileReader::parseDirectoryEntries(const QByteArray& data)
{
    std::vector<DirectoryEntry> entries;
    for (qsizetype offset = 0; offset + 128 <= data.size(); offset += 128) {
        const int nameLength = u16(data, offset + 64);
        if (nameLength < 2 || offset + nameLength > data.size()) {
            continue;
        }

        const QByteArray nameBytes = data.mid(offset, nameLength - 2);
        const QString name = QString::fromUtf16(reinterpret_cast<const char16_t*>(nameBytes.constData()), nameBytes.size() / 2);
        const quint32 startingSector = u32(data, offset + 116);
        const quint64 streamSize = static_cast<quint64>(u32(data, offset + 120))
            | (static_cast<quint64>(u32(data, offset + 124)) << 32);

        entries.push_back({
            name,
            static_cast<quint8>(data[offset + 66]),
            startingSector == EndOfChain ? -1 : static_cast<int>(startingSector),
            static_cast<qsizetype>(streamSize)
        });
    }
    return entries;
}

} // namespace xlsone
