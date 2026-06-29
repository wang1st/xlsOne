#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

namespace xlsone {

class CompoundFileReader {
public:
    explicit CompoundFileReader(QByteArray data);

    QByteArray stream(const QStringList& candidateNames) const;

private:
    struct DirectoryEntry {
        QString name;
        quint8 type = 0;
        int startingSector = -1;
        qsizetype streamSize = 0;
    };

    static constexpr quint32 EndOfChain = 0xfffffffe;
    static constexpr quint32 FreeSector = 0xffffffff;
    static constexpr quint32 DifatSector = 0xfffffffc;
    static constexpr quint32 FatSector = 0xfffffffd;

    QByteArray data_;
    int sectorSize_ = 512;
    int miniSectorSize_ = 64;
    int miniStreamCutoffSize_ = 4096;
    int firstDirectorySector_ = -1;
    int firstMiniFATSector_ = -1;
    int numberOfMiniFATSectors_ = 0;
    std::vector<quint32> fat_;
    std::vector<quint32> miniFAT_;
    std::vector<DirectoryEntry> directoryEntries_;
    QByteArray miniStream_;

    QByteArray streamFor(const DirectoryEntry& entry) const;

    static std::vector<int> readDIFAT(
        const QByteArray& data,
        int sectorSize,
        int headerFATSectorCount,
        int firstDIFATSector,
        int numberOfDIFATSectors
    );
    static std::vector<quint32> readFAT(const QByteArray& data, int sectorSize, const std::vector<int>& fatSectorIDs);
    static std::vector<quint32> readUInt32Table(const QByteArray& data);
    static QByteArray readRegularChain(
        const QByteArray& data,
        int sectorSize,
        const std::vector<quint32>& fat,
        int startSector,
        std::optional<int> sectorLimit = std::nullopt,
        std::optional<qsizetype> byteLimit = std::nullopt
    );
    static QByteArray readMiniChain(
        const QByteArray& miniStream,
        int miniSectorSize,
        const std::vector<quint32>& miniFAT,
        int startSector,
        qsizetype byteLimit
    );
    static QByteArray sectorData(const QByteArray& data, int sectorSize, int sectorID);
    static std::vector<DirectoryEntry> parseDirectoryEntries(const QByteArray& data);
};

} // namespace xlsone
