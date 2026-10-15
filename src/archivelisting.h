#pragma once

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <utility>

// 7z -slt output can be much larger than the compressed archive. Consume one
// record at a time instead of retaining byte, UTF-16 and split-line copies.
class ArchiveListing
{
public:
    explicit ArchiveListing(QString destination) : m_destination(std::move(destination)) {}

    void append(const QByteArray &bytes)
    {
        qsizetype offset = 0;
        while (offset < bytes.size() && m_valid) {
            const qsizetype newline = bytes.indexOf('\n', offset);
            const qsizetype end = newline < 0 ? bytes.size() : newline;
            const qsizetype length = end - offset;
            if (m_line.size() + length > 65536) {
                m_valid = false;
                m_line.clear();
                return;
            }
            m_line.append(bytes.constData() + offset, length);
            if (newline < 0)
                break;
            consumeLine();
            m_line.clear();
            offset = newline + 1;
        }
    }

    void finish()
    {
        if (!m_valid) return;
        if (!m_line.isEmpty()) consumeLine();
        m_line.clear();
        finishRecord();
    }

    bool valid() const { return m_valid; }
    const QStringList &conflicts() const { return m_conflicts; }

private:
    void consumeLine()
    {
        if (m_line.endsWith('\r')) m_line.chop(1);
        if (!m_started) {
            m_started = m_line.trimmed() == "----------";
        } else if (m_line.isEmpty()) {
            finishRecord();
        } else if (m_line.startsWith("Path = ")) {
            finishRecord();
            m_path = QString::fromLocal8Bit(m_line.constData() + 7, m_line.size() - 7);
        } else if (m_line.startsWith("Folder = ")) {
            m_folder = m_line.mid(9).trimmed() == "+";
        }
    }

    void finishRecord()
    {
        const QString relative = QDir::cleanPath(m_path);
        m_path.clear();
        const bool folder = std::exchange(m_folder, false);
        if (relative.isEmpty() || relative == "." || QDir::isAbsolutePath(relative)
                || relative == ".." || relative.startsWith("../") || m_conflicts.size() >= 100)
            return;
        const QFileInfo target(QDir(m_destination).filePath(relative));
        if ((!target.exists() && !target.isSymLink()) || (folder && target.isDir()))
            return;
        m_conflicts.append(relative);
    }

    QString m_destination, m_path;
    QStringList m_conflicts;
    QByteArray m_line;
    bool m_started = false, m_folder = false, m_valid = true;
};
