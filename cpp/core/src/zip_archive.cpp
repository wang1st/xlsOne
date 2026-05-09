#include "zip_archive.hpp"

#include <QFile>
#include <QTextCodec>
#include <QtEndian>
#include <stdexcept>
#include <zlib.h>

namespace xlsone {

namespace {

constexpr quint32 EndOfCentralDirectorySignature = 0x06054b50;
constexpr quint32 CentralDirectoryHeaderSignature = 0x02014b50;
constexpr quint32 LocalFileHeaderSignature = 0x04034b50;

quint16 u16(const QByteArray& data, qsizetype offset)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData() + offset));
}

quint32 u32(const QByteArray& data, qsizetype offset)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(data.constData() + offset));
}

QString decodeFileName(const QByteArray& raw, quint16 flags)
{
    if ((flags & (1 << 11)) != 0) {
        return QString::fromUtf8(raw);
    }
    QTextCodec* codec = QTextCodec::codecForLocale();
    return codec->toUnicode(raw);
}

QByteArray inflateRawDeflate(const QByteArray& compressed, quint32 expectedSize)
{
    QByteArray output;
    output.resize(static_cast<qsizetype>(expectedSize));

    z_stream stream {};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.constData()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());

    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        throw std::runtime_error("无法初始化 ZIP deflate 解压器");
    }

    const int result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    if (result != Z_STREAM_END) {
        throw std::runtime_error("ZIP deflate 解压失败");
    }

    output.resize(static_cast<qsizetype>(stream.total_out));
    return output;
}

} // namespace

ZipArchive::ZipArchive(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(QStringLiteral("无法打开 ZIP 文件: %1").arg(file.errorString()).toStdString());
    }
    data_ = file.readAll();
    parseCentralDirectory();
}

bool ZipArchive::contains(const QString& name) const
{
    return entries_.contains(name);
}

QStringList ZipArchive::entryNames() const
{
    QStringList names = entries_.keys();
    names.sort();
    return names;
}

QByteArray ZipArchive::read(const QString& name) const
{
    const auto iterator = entries_.constFind(name);
    if (iterator == entries_.constEnd()) {
        throw std::runtime_error(QStringLiteral("ZIP 条目不存在: %1").arg(name).toStdString());
    }

    const Entry entry = iterator.value();
    const qsizetype headerOffset = entry.localHeaderOffset;
    if (headerOffset < 0 || headerOffset + 30 > data_.size() || u32(data_, headerOffset) != LocalFileHeaderSignature) {
        throw std::runtime_error(QStringLiteral("ZIP 本地文件头无效: %1").arg(name).toStdString());
    }

    const quint16 fileNameLength = u16(data_, headerOffset + 26);
    const quint16 extraLength = u16(data_, headerOffset + 28);
    const qsizetype payloadOffset = headerOffset + 30 + fileNameLength + extraLength;
    if (payloadOffset < 0 || payloadOffset + entry.compressedSize > data_.size()) {
        throw std::runtime_error(QStringLiteral("ZIP 条目数据越界: %1").arg(name).toStdString());
    }

    const QByteArray compressed = data_.mid(payloadOffset, entry.compressedSize);
    if (entry.method == 0) {
        return compressed;
    }
    if (entry.method == 8) {
        return inflateRawDeflate(compressed, entry.uncompressedSize);
    }

    throw std::runtime_error(QStringLiteral("暂不支持 ZIP 压缩方法 %1: %2").arg(entry.method).arg(name).toStdString());
}

QString ZipArchive::readText(const QString& name) const
{
    return QString::fromUtf8(read(name));
}

void ZipArchive::parseCentralDirectory()
{
    const qsizetype maxComment = 0xffff;
    const qsizetype searchStart = std::max<qsizetype>(0, data_.size() - maxComment - 22);
    qsizetype eocdOffset = -1;
    for (qsizetype offset = data_.size() - 22; offset >= searchStart; --offset) {
        if (u32(data_, offset) == EndOfCentralDirectorySignature) {
            eocdOffset = offset;
            break;
        }
    }
    if (eocdOffset < 0) {
        throw std::runtime_error("不是有效的 ZIP 文件：找不到中央目录");
    }

    const quint16 entryCount = u16(data_, eocdOffset + 10);
    const quint32 centralDirectoryOffset = u32(data_, eocdOffset + 16);
    qsizetype offset = centralDirectoryOffset;

    for (quint16 index = 0; index < entryCount; ++index) {
        if (offset + 46 > data_.size() || u32(data_, offset) != CentralDirectoryHeaderSignature) {
            throw std::runtime_error("ZIP 中央目录损坏");
        }

        Entry entry;
        entry.flags = u16(data_, offset + 8);
        entry.method = u16(data_, offset + 10);
        entry.compressedSize = u32(data_, offset + 20);
        entry.uncompressedSize = u32(data_, offset + 24);
        const quint16 fileNameLength = u16(data_, offset + 28);
        const quint16 extraLength = u16(data_, offset + 30);
        const quint16 commentLength = u16(data_, offset + 32);
        entry.localHeaderOffset = u32(data_, offset + 42);

        const QByteArray rawName = data_.mid(offset + 46, fileNameLength);
        entries_.insert(decodeFileName(rawName, entry.flags), entry);

        offset += 46 + fileNameLength + extraLength + commentLength;
    }
}

} // namespace xlsone
