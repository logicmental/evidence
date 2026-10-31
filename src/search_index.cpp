#include "search_index.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

SearchIndex::SearchIndex()
    : m_connectionName(QStringLiteral("evidence-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

SearchIndex::~SearchIndex()
{
    if (m_database.isValid()) {
        const QString connectionName = m_connectionName;
        m_database.close();
        m_database = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }
}

bool SearchIndex::open(const QString &databasePath)
{
    if (m_database.isValid()) {
        m_database.close();
        m_database = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(databasePath);
    if (!m_database.open())
        return false;

    QSqlQuery query(m_database);
    query.exec("PRAGMA secure_delete = ON");
    if (!query.exec("CREATE TABLE IF NOT EXISTS notes (path TEXT PRIMARY KEY, title TEXT NOT NULL, "
                    "content TEXT NOT NULL, tags TEXT NOT NULL DEFAULT '', kind INTEGER NOT NULL DEFAULT 0)"))
        return false;

    bool hasTags = false;
    bool hasKind = false;
    if (!query.exec("PRAGMA table_info(notes)"))
        return false;
    while (query.next()) {
        hasTags = hasTags || query.value(1).toString() == QStringLiteral("tags");
        hasKind = hasKind || query.value(1).toString() == QStringLiteral("kind");
    }
    if (!hasTags && !query.exec("ALTER TABLE notes ADD COLUMN tags TEXT NOT NULL DEFAULT ''"))
        return false;
    if (!hasKind && !query.exec("ALTER TABLE notes ADD COLUMN kind INTEGER NOT NULL DEFAULT 0"))
        return false;

    query.exec("DROP TABLE IF EXISTS notes_fts");
    m_ftsAvailable = query.exec("CREATE VIRTUAL TABLE notes_fts USING fts5(path UNINDEXED, title, content, tags, kind UNINDEXED)");
    return true;
}

void SearchIndex::indexNote(const QString &relativePath, const QString &title, const QString &content,
                            const QString &tags)
{
    indexItem(relativePath, title, content, tags, false);
}

void SearchIndex::indexAttachment(const QString &relativePath, const QString &title, const QString &content)
{
    indexItem(relativePath, title, content, {}, true);
}

bool SearchIndex::forgetNote(const QString &relativePath)
{
    if (!m_database.isOpen())
        return true;

    QSqlQuery removeNote(m_database);
    removeNote.prepare("DELETE FROM notes WHERE path = ?");
    removeNote.addBindValue(relativePath);
    if (!removeNote.exec())
        return false;

    if (m_ftsAvailable) {
        QSqlQuery removeFts(m_database);
        removeFts.prepare("DELETE FROM notes_fts WHERE path = ?");
        removeFts.addBindValue(relativePath);
        if (!removeFts.exec())
            return false;
    }

    QSqlQuery compact(m_database);
    return compact.exec("VACUUM");
}

void SearchIndex::indexItem(const QString &relativePath, const QString &title, const QString &content,
                            const QString &tags, bool attachment)
{
    if (!m_database.isOpen())
        return;

    QSqlQuery note(m_database);
    note.prepare("INSERT INTO notes(path, title, content, tags, kind) VALUES(?, ?, ?, ?, ?) "
                 "ON CONFLICT(path) DO UPDATE SET title=excluded.title, content=excluded.content, "
                 "tags=excluded.tags, kind=excluded.kind");
    note.addBindValue(relativePath);
    note.addBindValue(title);
    note.addBindValue(content);
    note.addBindValue(tags);
    note.addBindValue(attachment ? 1 : 0);
    if (!note.exec())
        return;

    if (!m_ftsAvailable)
        return;

    QSqlQuery remove(m_database);
    remove.prepare("DELETE FROM notes_fts WHERE path = ?");
    remove.addBindValue(relativePath);
    if (!remove.exec())
        return;

    QSqlQuery insert(m_database);
    insert.prepare("INSERT INTO notes_fts(path, title, content, tags, kind) VALUES(?, ?, ?, ?, ?)");
    insert.addBindValue(relativePath);
    insert.addBindValue(title);
    insert.addBindValue(content);
    insert.addBindValue(tags);
    insert.addBindValue(attachment ? 1 : 0);
    insert.exec();
}

void SearchIndex::removeMissing(const QStringList &existingPaths)
{
    if (!m_database.isOpen())
        return;

    QSqlQuery query(m_database);
    if (!query.exec("SELECT path FROM notes"))
        return;

    QStringList obsolete;
    while (query.next()) {
        const QString path = query.value(0).toString();
        if (!existingPaths.contains(path))
            obsolete.append(path);
    }

    for (const QString &path : obsolete) {
        QSqlQuery removeNote(m_database);
        removeNote.prepare("DELETE FROM notes WHERE path = ?");
        removeNote.addBindValue(path);
        removeNote.exec();
        if (m_ftsAvailable) {
            QSqlQuery removeFts(m_database);
            removeFts.prepare("DELETE FROM notes_fts WHERE path = ?");
            removeFts.addBindValue(path);
            removeFts.exec();
        }
    }
}

QStringList SearchIndex::search(const QString &queryText) const
{
    QStringList results;
    if (!m_database.isOpen())
        return results;

    const QStringList words = queryText.toCaseFolded().split(
        QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_-]+")), Qt::SkipEmptyParts);
    if (words.isEmpty())
        return results;

    if (m_ftsAvailable) {
        QStringList terms;
        for (const QString &word : words)
            terms.append(QStringLiteral("\"%1\"").arg(QString(word).replace('"', "\"\"")));

        QSqlQuery query(m_database);
        query.prepare("SELECT path FROM notes_fts WHERE notes_fts MATCH ? ORDER BY rank LIMIT 100");
        query.addBindValue(terms.join(QStringLiteral(" AND ")));
        if (query.exec()) {
            while (query.next())
                results.append(query.value(0).toString());
            return results;
        }
    }

    QSqlQuery query(m_database);
    if (!query.exec("SELECT path, title, content, tags FROM notes ORDER BY title COLLATE NOCASE"))
        return results;
    while (query.next()) {
        const QString haystack = query.value(1).toString() + QLatin1Char('\n') + query.value(2).toString() +
                                 QLatin1Char('\n') + query.value(3).toString();
        bool matches = true;
        for (const QString &word : words) {
            if (!haystack.contains(word, Qt::CaseInsensitive)) {
                matches = false;
                break;
            }
        }
        if (matches)
            results.append(query.value(0).toString());
    }
    return results;
}