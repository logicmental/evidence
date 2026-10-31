#pragma once

#include "search_index.h"

#include <QFileSystemWatcher>
#include <QMainWindow>
#include <QMap>
#include <QStringList>

class QLineEdit;
class QComboBox;
class QAction;
class QListWidget;
class QLabel;
class QPlainTextEdit;
class QStackedWidget;
class QTextBrowser;
class QTimer;
class GraphView;
class MarkdownHighlighter;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void buildUi();
    void manageTemplates();
    void applyInlineFormat(const QString &prefix, const QString &suffix);
    void applyLineFormat(const QString &prefix);
    void updateNoteHeader();
    void manageTrash();
    void exportVault();
    void insertNoteLink();
    void updateBacklinks();
    void updateCurrentLinks();
    QString localized(const QString &source) const;
    void setLanguage(const QString &language);
    void openVault(const QString &path);
    void refreshVault();
    void createNote();
    void moveCurrentNoteToTrash();
    void renameCurrentNote();
    void deleteCurrentNote();
    void createFolder();
    void attachFile();
    void encryptCurrentNote();
    void lockCurrentNote();
    void saveNote();
    void openNote(const QString &relativePath);
    void updatePreview();
    void updateSearchResults();
    void scheduleRefresh();
    void loadMetadata();
    void saveMetadata() const;
    QStringList attachmentFiles() const;
    QStringList templateNames() const;
    QString templateText(const QString &name) const;
    QString resolveWikiLink(const QString &target) const;
    QStringList noteFiles() const;

    QString m_vaultPath;
    QString m_currentNote;
    QString m_notePassword;
    SearchIndex m_index;
    QMap<QString, QStringList> m_tags;
    QMap<QString, QStringList> m_links;
    QMap<QString, QString> m_titles;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_refreshTimer = nullptr;
    QLineEdit *m_searchInput = nullptr;
    QLineEdit *m_tagsInput = nullptr;
    QComboBox *m_folderFilter = nullptr;
    QListWidget *m_noteList = nullptr;
    QListWidget *m_backlinkList = nullptr;
    QLabel *m_noteTitleLabel = nullptr;
    QLabel *m_noteStateLabel = nullptr;
    QPlainTextEdit *m_editor = nullptr;
    QTextBrowser *m_preview = nullptr;
    QStackedWidget *m_mainStack = nullptr;
    GraphView *m_graph = nullptr;
    MarkdownHighlighter *m_highlighter = nullptr;
    QAction *m_encryptionAction = nullptr;
    QAction *m_renameAction = nullptr;
    QAction *m_deleteAction = nullptr;
    QAction *m_languageAction = nullptr;
    QAction *m_graphAction = nullptr;
    QAction *m_trashAction = nullptr;
    QAction *m_exportAction = nullptr;
    QAction *m_linkAction = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_searchAction = nullptr;
    QString m_language = QStringLiteral("en");
};