#pragma once

#include <QSqlDatabase>
#include <QStringList>

class SearchIndex final
{
public:
    SearchIndex();
    ~SearchIndex();

    bool open(const QString &databasePath);
    void indexNote(const QString &relativePath, const QString &title, const QString &content,
                   const QString &tags = {});
    void indexAttachment(const QString &relativePath, const QString &title, const QString &content);
    bool forgetNote(const QString &relativePath);
    void removeMissing(const QStringList &existingPaths);
    QStringList search(const QString &query) const;

private:
    void indexItem(const QString &relativePath, const QString &title, const QString &content,
                   const QString &tags, bool attachment);

    QSqlDatabase m_database;
    QString m_connectionName;
    bool m_ftsAvailable = false;
};