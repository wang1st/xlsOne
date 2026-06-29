#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

namespace xlsone {

class ZipArchive {
public:
    explicit ZipArchive(const QString& path);

    bool contains(const QString& name) const;
    QStringList entryNames() const;
    QByteArray read(const QString& name) const;
    QString readText(const QString& name) const;

private:
    struct Entry {
        quint16 flags = 0;
        quint16 method = 0;
        quint32 compressedSize = 0;
        quint32 uncompressedSize = 0;
        quint32 localHeaderOffset = 0;
    };

    QByteArray data_;
    QHash<QString, Entry> entries_;

    void parseCentralDirectory();
};

} // namespace xlsone
