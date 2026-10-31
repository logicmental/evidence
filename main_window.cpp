#include "main_window.h"

#include "markdown_highlighter.h"
#include "secure_note.h"

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineF>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QPdfDocument>
#include <QPlainTextEdit>
#include <QPdfWriter>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSettings>
#include <QSet>
#include <QUrl>
#include <QXmlStreamWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>

class GraphView final : public QWidget
{
    Q_OBJECT

public:
    explicit GraphView(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(420, 320);
        setStyleSheet("background: #f5f7f6;");
    }

    void setGraph(const QStringList &nodes, const QMap<QString, QStringList> &edges,
                  const QString &selected)
    {
        m_nodes = nodes;
        m_edges = edges;
        m_selected = selected;
        update();
    }

    void setEmptyMessage(const QString &message)
    {
        m_emptyMessage = message;
        update();
    }

signals:
    void noteActivated(const QString &relativePath);

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#f5f7f6"));
        if (m_nodes.isEmpty()) {
            painter.setPen(QColor("#72756e"));
            painter.drawText(rect(), Qt::AlignCenter, m_emptyMessage);
            return;
        }

        const QPointF center = rect().center();
        const qreal radius = qMax(45.0, qMin(width(), height()) * 0.36);
        constexpr qreal pi = 3.14159265358979323846;
        QMap<QString, QPointF> positions;
        for (qsizetype i = 0; i < m_nodes.size(); ++i) {
            const qreal angle = -pi / 2.0 + (2.0 * pi * i / m_nodes.size());
            positions.insert(m_nodes.at(i), center + QPointF(qCos(angle) * radius, qSin(angle) * radius));
        }

        painter.setPen(QPen(QColor("#cbd9d3"), 1.3));
        for (auto it = m_edges.cbegin(); it != m_edges.cend(); ++it) {
            if (!positions.contains(it.key()))
                continue;
            for (const QString &target : it.value()) {
                if (positions.contains(target))
                    painter.drawLine(positions.value(it.key()), positions.value(target));
            }
        }

        for (const QString &node : m_nodes) {
            const QPointF point = positions.value(node);
            const bool selected = node == m_selected;
            const QRectF marker(point.x() - 7, point.y() - 7, 14, 14);
            painter.setPen(Qt::NoPen);
            painter.setBrush(selected ? QColor("#d16b4f") : QColor("#287a6b"));
            painter.drawEllipse(marker);
            painter.setPen(QColor("#30332f"));
            const QString label = QFileInfo(node).completeBaseName();
            painter.drawText(QRectF(point.x() - 65, point.y() + 10, 130, 20), Qt::AlignHCenter,
                             QFontMetrics(font()).elidedText(label, Qt::ElideRight, 128));
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        const QPointF center = rect().center();
        const qreal radius = qMax(45.0, qMin(width(), height()) * 0.36);
        constexpr qreal pi = 3.14159265358979323846;
        for (qsizetype i = 0; i < m_nodes.size(); ++i) {
            const qreal angle = -pi / 2.0 + (2.0 * pi * i / m_nodes.size());
            const QPointF point = center + QPointF(qCos(angle) * radius, qSin(angle) * radius);
            if (QLineF(event->position(), point).length() < 18) {
                emit noteActivated(m_nodes.at(i));
                return;
            }
        }
    }

private:
    QStringList m_nodes;
    QMap<QString, QStringList> m_edges;
    QString m_selected;
    QString m_emptyMessage = QStringLiteral("No linked notes yet");
};

namespace {
bool isEncryptedNote(const QString &path)
{
    return path.endsWith(QStringLiteral(".evnote"), Qt::CaseInsensitive);
}

QString findBundledOrSystemExecutable(const QStringList &relativePaths, const QString &systemName)
{
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    for (const QString &relativePath : relativePaths) {
        const QString candidate = applicationDirectory.filePath(relativePath);
        if (QFileInfo(candidate).isFile())
            return candidate;
    }
    return QStandardPaths::findExecutable(systemName);
}

QString findPandocExecutable()
{
    return findBundledOrSystemExecutable({QStringLiteral("tools/pandoc.exe"),
                                          QStringLiteral("tools/Pandoc/pandoc.exe"),
                                          QStringLiteral("tools/pandoc")},
                                         QStringLiteral("pandoc"));
}

QString findLibreOfficeExecutable()
{
    QString bundled = findBundledOrSystemExecutable(
        {QStringLiteral("tools/LibreOffice/program/soffice.exe"),
         QStringLiteral("tools/libreoffice/program/soffice.exe"),
         QStringLiteral("tools/libreoffice/program/soffice")},
        QStringLiteral("soffice"));
#ifdef Q_OS_WIN
    if (bundled.isEmpty()) {
        const QStringList programRoots{qEnvironmentVariable("ProgramFiles"),
                                       qEnvironmentVariable("ProgramFiles(x86)")};
        for (const QString &root : programRoots) {
            if (root.isEmpty())
                continue;
            const QString candidate = QDir(root).filePath("LibreOffice/program/soffice.exe");
            if (QFileInfo(candidate).isFile())
                return candidate;
        }
    }
#endif
    return bundled;
}

QString extractPdfText(const QString &path)
{
    QPdfDocument document;
    if (document.load(path) != QPdfDocument::Error::None)
        return {};

    QStringList pages;
    pages.reserve(document.pageCount());
    for (int page = 0; page < document.pageCount(); ++page)
        pages.append(document.getAllText(page).text());
    return pages.join(QLatin1Char('\n'));
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_watcher(new QFileSystemWatcher(this)), m_refreshTimer(new QTimer(this))
{
    const QString bootstrapPath = QDir(QCoreApplication::applicationDirPath()).filePath("initial-language.ini");
    QSettings bootstrap(bootstrapPath, QSettings::IniFormat);
    const QString initialLanguage = bootstrap.value("General/language").toString();
    const QString settingsPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                                     .filePath("settings.ini");
    QSettings settings(settingsPath, QSettings::IniFormat);
    m_language = initialLanguage.isEmpty()
                     ? settings.value("General/language", QStringLiteral("en")).toString()
                     : initialLanguage;
    if (m_language != QStringLiteral("ru"))
        m_language = QStringLiteral("en");
     setWindowTitle(localized("evidence"));
    resize(1180, 760);
    buildUi();
    setLanguage(m_language);
    QFile::remove(bootstrapPath);

    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(350);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::refreshVault);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::scheduleRefresh);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &MainWindow::scheduleRefresh);

    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString defaultVault = QDir(documents).filePath("evidence");
    QDir().mkpath(defaultVault);
    openVault(defaultVault);
}

QString MainWindow::localized(const QString &source) const
{
    if (m_language != QStringLiteral("ru"))
        return source;

    static const QMap<QString, QString> russian{
        {"New note", "Новая заметка"}, {"New folder", "Новая папка"},
        {"Open folder", "Открыть папку"}, {"Attach file", "Прикрепить файл"},
        {"Templates", "Шаблоны"}, {"Encrypt note", "Зашифровать заметку"},
        {"Undo", "Отменить"}, {"Redo", "Повторить"}, {"Link note", "Связать заметку"},
        {"More", "Ещё"}, {"Move to trash", "В корзину"},
        {"No note open", "Заметка не выбрана"},
        {"Plain Markdown", "Обычный Markdown"}, {"Encrypted", "Зашифровано"},
        {"Choose a note to link", "Выберите заметку для связи"},
        {"Trash", "Корзина"}, {"Export", "Экспорт"}, {"Search", "Поиск"},
        {"BACKLINKS", "ОБРАТНЫЕ ССЫЛКИ"}, {"Search entire vault (Ctrl+F)", "Поиск по всему хранилищу (Ctrl+F)"},
        {"No backlinks", "Нет обратных ссылок"}, {"No notes in trash", "Корзина пуста"},
        {"Restore", "Восстановить"}, {"Delete permanently", "Удалить навсегда"},
        {"Export vault", "Экспорт хранилища"}, {"Choose an export destination folder", "Выберите папку для экспорта"},
        {"Export notes", "Экспорт заметок"}, {"Choose what to export", "Что экспортировать"},
        {"Current note", "Текущую заметку"}, {"Selected notes", "Выбранные заметки"},
        {"Current folder", "Текущую папку"}, {"Entire vault", "Всё хранилище"},
        {"No notes selected for export.", "Нет заметок для экспорта."},
        {"Choose export format", "Выберите формат экспорта"},
        {"Plain text (*.txt)", "Текст (*.txt)"}, {"Word document (*.doc)", "Документ Word (*.doc)"},
        {"Word document (*.docx)", "Документ Word (*.docx)"}, {"Rich text (*.rtf)", "Rich Text (*.rtf)"},
        {"PDF document (*.pdf)", "Документ PDF (*.pdf)"}, {"HTML document (*.html)", "Документ HTML (*.html)"},
        {"XML document (*.xml)", "Документ XML (*.xml)"}, {"EPUB book (*.epub)", "Книга EPUB (*.epub)"},
        {"FictionBook (*.fb2)", "FictionBook (*.fb2)"}, {"CSV table (*.csv)", "Таблица CSV (*.csv)"},
        {"Markdown document (*.md)", "Документ Markdown (*.md)"},
        {"Pandoc is required for this format. Install Pandoc and try again.", "Для этого формата нужен Pandoc. Установите Pandoc и повторите попытку."},
        {"LibreOffice is required to export legacy DOC files.", "Для экспорта старого формата DOC нужен LibreOffice."},
        {"Could not export the selected notes.", "Не удалось экспортировать выбранные заметки."},
        {"Could not read the selected note.", "Не удалось прочитать выбранную заметку."},
        {"Enter the password to export '%1'.", "Введите пароль для экспорта «%1»."},
        {"The password is incorrect or the note is damaged.", "Неверный пароль или файл заметки повреждён."},
        {"Exported %1 notes to %2", "Экспортировано заметок: %1; файл: %2"},
        {"Export completed to %1", "Экспорт завершён: %1"},
        {"Could not export %1", "Не удалось экспортировать %1"},
        {"Move '%1' to the trash?", "Переместить «%1» в корзину?"},
        {"Could not move the note to the trash.", "Не удалось переместить заметку в корзину."},
        {"This note is already in the trash.", "Эта заметка уже находится в корзине."},
        {"Restore '%1' to its original location?", "Восстановить «%1» в исходное расположение?"},
        {"A note already exists at the original location.", "В исходном расположении уже есть заметка."},
        {"Could not restore the note.", "Не удалось восстановить заметку."},
        {"Delete '%1' permanently?", "Удалить «%1» навсегда?"},
        {"%1 search results", "Результатов поиска: %1"},
        {"Formatting", "Форматирование"}, {"Code", "Код"},
        {"List", "Список"}, {"Quote", "Цитата"},
        {"Lock note", "Заблокировать заметку"}, {"Rename note", "Переименовать"},
        {"Rename", "Переименовать"},
        {"Delete note", "Удалить заметку"}, {"Save", "Сохранить"},
        {"Graph", "Граф"}, {"Editor", "Редактор"}, {"Language", "Язык"},
        {"Search notes and files", "Поиск заметок и файлов"},
        {"NOTES & FILES", "ЗАМЕТКИ И ФАЙЛЫ"},
        {"Tags, separated by commas", "Теги через запятую"},
        {"Start writing in Markdown...", "Начните писать в Markdown..."},
        {"Bold", "Жирный"}, {"Italic", "Курсив"}, {"Inline code", "Встроенный код"},
        {"Link", "Ссылка"}, {"Toggle level 1 heading", "Заголовок первого уровня"},
        {"Toggle level 2 heading", "Заголовок второго уровня"},
        {"Toggle unordered list", "Маркированный список"}, {"Toggle block quote", "Цитата"},
        {"Ready", "Готово"}, {"Unsaved changes", "Несохранённые изменения"},
        {"Save changes before opening another folder?", "Сохранить изменения перед открытием другой папки?"},
        {"Save changes before opening another note?", "Сохранить изменения перед открытием другой заметки?"},
        {"Save changes before deleting the note?", "Сохранить изменения перед удалением заметки?"},
        {"Search index is unavailable; notes are still accessible", "Индекс недоступен; заметки доступны"},
        {"Note templates", "Шаблоны заметок"}, {"Template name", "Название шаблона"},
        {"Write a Markdown template...", "Введите шаблон в Markdown..."},
        {"Existing template", "Существующий шаблон"}, {"Name", "Название"},
        {"Markdown", "Markdown"}, {"New", "Создать"}, {"Save template", "Сохранить шаблон"},
        {"Delete", "Удалить"}, {"Close", "Закрыть"}, {"New template", "Новый шаблон"},
        {"Enter a valid template name.", "Введите допустимое название шаблона."},
        {"Could not create the templates folder.", "Не удалось создать папку шаблонов."},
        {"Could not save the template.", "Не удалось сохранить шаблон."},
        {"Delete template", "Удалить шаблон"}, {"Delete '%1'?", "Удалить «%1»?"},
        {"All folders", "Все папки"}, {"[Encrypted] ", "[Зашифровано] "},
        {"[File] ", "[Файл] "}, {"Blank", "Пустая"}, {"Template", "Шаблон"},
        {"Could not create the note.", "Не удалось создать заметку."},
        {"Folder name", "Название папки"},
        {"Choose a folder path inside the current notes folder.", "Выберите папку внутри текущей папки заметок."},
        {"That folder name is reserved for attached files.", "Это имя зарезервировано для вложений."},
        {"That folder name is reserved.", "Это имя зарезервировано."},
        {"Could not create the folder.", "Не удалось создать папку."},
        {"Open a note before attaching a file.", "Сначала откройте заметку."},
        {"Could not create the attachments folder.", "Не удалось создать папку вложений."},
        {"Could not copy the attachment into this notes folder.", "Не удалось скопировать вложение в папку заметок."},
        {"Could not encrypt the note.", "Не удалось зашифровать заметку."},
        {"Could not save the note.", "Не удалось сохранить заметку."},
        {"The password cannot be empty.", "Пароль не может быть пустым."},
        {"Password", "Пароль"}, {"Confirm password", "Повторите пароль"},
        {"The passwords do not match.", "Пароли не совпадают."},
        {"An encrypted note with that name already exists.", "Зашифрованная заметка с таким именем уже существует."},
        {"Could not write the encrypted note.", "Не удалось записать зашифрованную заметку."},
        {"The search index could not be securely updated; encryption was cancelled.", "Не удалось безопасно обновить индекс; шифрование отменено."},
        {"The original note could not be removed; encryption was cancelled.", "Не удалось удалить исходную заметку; шифрование отменено."},
        {"Note encrypted", "Заметка зашифрована"}, {"Unlock note", "Разблокировать заметку"},
        {"The password is incorrect or the note is damaged.", "Неверный пароль или файл заметки повреждён."},
        {"No linked notes yet", "Связанных заметок пока нет"},
        {"Search notes", "Поиск заметок"}, {"Application language", "Язык приложения"},
        {"Choose the interface language", "Выберите язык интерфейса"},
        {"English", "English"}, {"Русский", "Русский"},
        {"All folders", "Все папки"}, {"%1 notes · %2 files · %3", "%1 заметок · %2 файлов · %3"},
        {"The open note changed on disk; your unsaved edits were kept", "Открытая заметка изменилась на диске; несохранённые правки сохранены в редакторе"},
        {"Saved %1", "Сохранено: %1"}, {"Saved template %1", "Шаблон сохранён: %1"},
        {"Open notes folder", "Открыть папку заметок"},
        {"Enter a new note name", "Введите новое название заметки"},
        {"The note name contains invalid characters.", "В названии заметки есть недопустимые символы."},
        {"A note with that name already exists.", "Заметка с таким именем уже существует."},
        {"Could not rename the note.", "Не удалось переименовать заметку."},
        {"Delete note '%1'? This cannot be undone.", "Удалить заметку «%1»? Это действие нельзя отменить."},
        {"Could not delete the note.", "Не удалось удалить заметку."}
    };
    return russian.value(source, source);
}

void MainWindow::setLanguage(const QString &language)
{
    m_language = language == QStringLiteral("ru") ? QStringLiteral("ru") : QStringLiteral("en");
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    const QString settingsPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                                    .filePath("settings.ini");
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.setValue("General/language", m_language);

    for (QAction *action : findChildren<QAction *>()) {
        const QString source = action->property("sourceText").toString();
        if (!source.isEmpty())
            action->setText(localized(source));
        const QString tooltip = action->property("sourceTooltip").toString();
        if (!tooltip.isEmpty())
            action->setToolTip(localized(tooltip));
    }
    for (QToolButton *button : findChildren<QToolButton *>()) {
        const QString source = button->property("sourceText").toString();
        if (!source.isEmpty())
            button->setText(localized(source));
        const QString tooltip = button->property("sourceTooltip").toString();
        if (!tooltip.isEmpty())
            button->setToolTip(localized(tooltip));
    }
    for (QLabel *label : findChildren<QLabel *>()) {
        const QString source = label->property("sourceText").toString();
        if (!source.isEmpty())
            label->setText(localized(source));
    }
    for (QLineEdit *input : findChildren<QLineEdit *>()) {
        const QString source = input->property("sourcePlaceholder").toString();
        if (!source.isEmpty())
            input->setPlaceholderText(localized(source));
    }
    for (QPlainTextEdit *input : findChildren<QPlainTextEdit *>()) {
        const QString source = input->property("sourcePlaceholder").toString();
        if (!source.isEmpty())
            input->setPlaceholderText(localized(source));
    }
    if (m_folderFilter && m_folderFilter->count() > 0)
        m_folderFilter->setItemText(0, localized("All folders"));
    for (QToolBar *toolbar : findChildren<QToolBar *>()) {
        const QString source = toolbar->property("sourceTitle").toString();
        if (!source.isEmpty())
            toolbar->setWindowTitle(localized(source));
    }
    if (m_languageAction)
        m_languageAction->setText(localized("Language") + QStringLiteral(": ") +
                                  (m_language == QStringLiteral("ru") ? localized("Русский") : localized("English")));
    if (m_graphAction)
        m_graphAction->setText(localized(m_mainStack->currentIndex() == 0 ? "Graph" : "Editor"));
    if (m_encryptionAction && !m_currentNote.isEmpty())
        m_encryptionAction->setText(localized(isEncryptedNote(m_currentNote) ? "Lock note" : "Encrypt note"));
    if (m_graph)
        m_graph->setEmptyMessage(localized("No linked notes yet"));
    updateBacklinks();
    if (m_noteList) {
        for (int i = 0; i < m_noteList->count(); ++i) {
            QListWidgetItem *item = m_noteList->item(i);
            const QString path = item->data(Qt::UserRole).toString();
            if (item->data(Qt::UserRole + 1).toBool()) {
                item->setText(localized("[File] ") + QFileInfo(path).fileName());
                continue;
            }
            QString label = m_titles.value(path, QFileInfo(path).completeBaseName());
            if (isEncryptedNote(path))
                label = localized("[Encrypted] ") + label;
            const QString folder = QFileInfo(path).path();
            if (folder != QStringLiteral("."))
                label += QStringLiteral("  ·  ") + folder;
            const QStringList tags = m_tags.value(path);
            if (!tags.isEmpty())
                label += QStringLiteral("  #") + tags.join(QStringLiteral(" #"));
            item->setText(label);
        }
    }
    if (!m_currentNote.isEmpty())
        setWindowTitle(localized("evidence - ") + QFileInfo(m_currentNote).completeBaseName());
    else if (!m_vaultPath.isEmpty())
        setWindowTitle(localized("evidence - ") + QFileInfo(m_vaultPath).fileName());
    updateNoteHeader();
}

void MainWindow::updateNoteHeader()
{
    if (!m_noteTitleLabel)
        return;
    if (m_currentNote.isEmpty()) {
        m_noteTitleLabel->setText(localized("No note open"));
        m_noteTitleLabel->setToolTip(QString());
        if (m_noteStateLabel)
            m_noteStateLabel->setVisible(false);
        return;
    }
    const QFileInfo noteInfo(m_currentNote);
    m_noteTitleLabel->setText(noteInfo.completeBaseName());
    m_noteTitleLabel->setToolTip(QDir(m_vaultPath).toNativeSeparators(m_currentNote));
    if (m_noteStateLabel) {
        const bool encrypted = isEncryptedNote(m_currentNote);
        m_noteStateLabel->setText(localized(encrypted ? "Encrypted" : "Plain Markdown"));
        m_noteStateLabel->setProperty("encrypted", encrypted);
        m_noteStateLabel->style()->unpolish(m_noteStateLabel);
        m_noteStateLabel->style()->polish(m_noteStateLabel);
        m_noteStateLabel->setVisible(true);
    }
}

void MainWindow::buildUi()
{
    setStyleSheet(R"(
        QMainWindow, QDialog { background: #f5f7f6; color: #202b29; }
        QToolBar { background: #ffffff; border: 0; border-bottom: 1px solid #dce5e1; spacing: 5px; padding: 8px 12px; }
        QToolButton, QPushButton { color: #34423f; background: transparent; border: 1px solid transparent; border-radius: 5px; padding: 8px 11px; }
        QToolButton:hover, QPushButton:hover { background: #e8f1ed; border-color: #d3e2dc; }
        QToolButton:pressed, QPushButton:pressed { background: #d9e9e2; }
        QToolButton:disabled, QPushButton:disabled { color: #9aa8a3; }
        QToolButton#noteActionButton { background: #f3f7f5; border: 1px solid #dce7e2; padding: 7px 10px; }
        QToolButton#noteActionButton:hover { color: #1d5b50; background: #e5f0eb; border-color: #c9ddd4; }
        QToolButton#trashNoteButton { color: #9b493a; background: #fbf3f1; border-color: #efdbd5; }
        QToolButton#trashNoteButton:hover { color: #853b2d; background: #f7e6e1; border-color: #e8c7bd; }
        QToolButton::menu-indicator { subcontrol-origin: padding; subcontrol-position: bottom right; }
        QMenu { background: #ffffff; border: 1px solid #dce5e1; padding: 5px; }
        QMenu::item { padding: 8px 24px 8px 10px; border-radius: 4px; }
        QMenu::item:selected { color: #1d5b50; background: #e8f1ed; }
        QWidget#sidebar { background: #edf2ef; border-right: 1px solid #dce5e1; }
        QWidget#editorPane { background: #ffffff; }
        QLineEdit, QComboBox { min-height: 20px; background: #ffffff; border: 1px solid #d4dfda; border-radius: 5px; padding: 8px 10px; color: #202b29; selection-background-color: #d5e9e0; }
        QLineEdit:focus, QComboBox:focus { border: 1px solid #3b8273; }
        QComboBox::drop-down { border: 0; width: 24px; }
        QComboBox QAbstractItemView { background: #ffffff; border: 1px solid #d4dfda; selection-background-color: #e8f1ed; }
        QListWidget { background: #ffffff; border: 1px solid #e1e9e5; border-radius: 6px; outline: 0; padding: 4px; }
        QListWidget#noteList, QListWidget#backlinkList { background: transparent; border: 0; }
        QListWidget::item { padding: 8px 9px; color: #34423f; border-radius: 4px; }
        QListWidget::item:hover { background: #e7efeb; }
        QListWidget::item:selected { background: #dcebe5; color: #1e5c50; }
        QPlainTextEdit, QTextBrowser { background: #ffffff; border: 0; padding: 18px; color: #202b29; selection-background-color: #d5e9e0; }
        QPlainTextEdit { padding: 20px 24px; }
        QSplitter::handle { background: #e6ece9; width: 1px; }
        QSplitter::handle:hover { background: #8eb6a9; }
        QLabel#sectionTitle { color: #65756f; font-size: 10px; font-weight: 700; padding-top: 5px; }
        QLabel#noteTitle { color: #243531; font-size: 19px; font-weight: 650; padding: 2px 2px 5px 2px; }
        QLabel#noteState { color: #1d6255; background: #e2f1e9; border: 1px solid #cce5d8; border-radius: 10px; padding: 4px 10px; font-size: 10px; font-weight: 700; }
        QLabel#noteState[encrypted="true"] { color: #76552a; background: #f6eddb; border-color: #ead9b7; }
        QStatusBar { background: #ffffff; color: #687872; border-top: 1px solid #dce5e1; }
        QStatusBar::item { border: 0; }
        QScrollBar:vertical { width: 10px; background: transparent; margin: 2px; }
        QScrollBar::handle:vertical { background: #cbd8d2; min-height: 28px; border-radius: 4px; }
        QScrollBar::handle:vertical:hover { background: #a9beb4; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    )");

    auto *toolbar = addToolBar("Workspace");
    toolbar->setObjectName("workspaceToolbar");
    toolbar->setMovable(false);
    const auto addToolbarAction = [this, toolbar](const QString &source) {
        QAction *action = toolbar->addAction(localized(source));
        action->setProperty("sourceText", source);
        return action;
    };
    QAction *newAction = addToolbarAction("New note");
    QAction *newFolderAction = addToolbarAction("New folder");
    QAction *folderAction = addToolbarAction("Open folder");
    QAction *saveAction = addToolbarAction("Save");
    m_searchAction = addToolbarAction("Search");
    m_graphAction = addToolbarAction("Graph");
    QAction *graphAction = m_graphAction;
    auto *moreButton = new QToolButton(toolbar);
    moreButton->setText(localized("More"));
    moreButton->setProperty("sourceText", "More");
    moreButton->setPopupMode(QToolButton::InstantPopup);
    auto *moreMenu = new QMenu(moreButton);
    moreButton->setMenu(moreMenu);
    toolbar->addWidget(moreButton);
    const auto addMoreAction = [this, moreMenu](const QString &source) {
        QAction *action = moreMenu->addAction(localized(source));
        action->setProperty("sourceText", source);
        return action;
    };
    QAction *attachAction = addMoreAction("Attach file");
    QAction *templatesAction = addMoreAction("Templates");
    m_encryptionAction = addMoreAction("Encrypt note");
    m_renameAction = addMoreAction("Rename note");
    m_deleteAction = addMoreAction("Move to trash");
    moreMenu->addSeparator();
    m_undoAction = addMoreAction("Undo");
    m_redoAction = addMoreAction("Redo");
    m_linkAction = addMoreAction("Link note");
    moreMenu->addSeparator();
    m_trashAction = addMoreAction("Trash");
    m_exportAction = addMoreAction("Export");
    m_languageAction = addMoreAction("Language");
    m_encryptionAction->setEnabled(false);
    m_renameAction->setEnabled(false);
    m_deleteAction->setEnabled(false);
    m_undoAction->setEnabled(false);
    m_redoAction->setEnabled(false);
    m_linkAction->setEnabled(false);
    m_languageAction->setText(localized("Language") + QStringLiteral(": ") +
                              (m_language == QStringLiteral("ru") ? localized("Русский") : localized("English")));
    newAction->setShortcut(QKeySequence::New);
    saveAction->setShortcut(QKeySequence::Save);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_undoAction->setShortcutContext(Qt::WindowShortcut);
    m_redoAction->setShortcutContext(Qt::WindowShortcut);
    m_searchAction->setShortcut(QKeySequence::Find);
    connect(newAction, &QAction::triggered, this, &MainWindow::createNote);
    connect(newFolderAction, &QAction::triggered, this, &MainWindow::createFolder);
    connect(folderAction, &QAction::triggered, this, [this] {
        const QString folder = QFileDialog::getExistingDirectory(this, localized("Open notes folder"), m_vaultPath);
        if (!folder.isEmpty())
            openVault(folder);
    });
    connect(attachAction, &QAction::triggered, this, &MainWindow::attachFile);
    connect(templatesAction, &QAction::triggered, this, &MainWindow::manageTemplates);
    connect(m_renameAction, &QAction::triggered, this, &MainWindow::renameCurrentNote);
    connect(m_deleteAction, &QAction::triggered, this, &MainWindow::deleteCurrentNote);
    connect(m_undoAction, &QAction::triggered, m_editor, &QPlainTextEdit::undo);
    connect(m_redoAction, &QAction::triggered, m_editor, &QPlainTextEdit::redo);
    connect(m_linkAction, &QAction::triggered, this, &MainWindow::insertNoteLink);
    connect(m_trashAction, &QAction::triggered, this, &MainWindow::manageTrash);
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::exportVault);
    connect(m_searchAction, &QAction::triggered, this, [this] {
        m_searchInput->setFocus();
        m_searchInput->selectAll();
    });
    connect(m_languageAction, &QAction::triggered, this, [this] {
        const QStringList choices{localized("English"), localized("Русский")};
        bool accepted = false;
        const int current = m_language == QStringLiteral("ru") ? 1 : 0;
        const QString choice = QInputDialog::getItem(this, localized("Application language"),
                                                      localized("Choose the interface language"), choices,
                                                      current, false, &accepted);
        if (accepted)
            setLanguage(choice == localized("Русский") ? QStringLiteral("ru") : QStringLiteral("en"));
    });
    connect(m_encryptionAction, &QAction::triggered, this, [this] {
        if (isEncryptedNote(m_currentNote))
            lockCurrentNote();
        else
            encryptCurrentNote();
    });
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveNote);
    connect(graphAction, &QAction::triggered, this, [this] {
        m_mainStack->setCurrentIndex(m_mainStack->currentIndex() == 0 ? 1 : 0);
    });
    connect(m_mainStack, &QStackedWidget::currentChanged, this, [this, graphAction](int index) {
        const QString source = index == 0 ? QStringLiteral("Graph") : QStringLiteral("Editor");
        graphAction->setProperty("sourceText", source);
        graphAction->setText(localized(source));
    });

    auto *root = new QWidget(this);
    auto *layout = new QHBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *sidebar = new QWidget(root);
    sidebar->setObjectName("sidebar");
    sidebar->setMinimumWidth(230);
    sidebar->setMaximumWidth(330);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(16, 18, 14, 14);
    sidebarLayout->setSpacing(10);
    auto *brand = new QLabel("evidence", sidebar);
    QFont brandFont = brand->font();
    brandFont.setPointSize(20);
    brandFont.setWeight(QFont::DemiBold);
    brand->setFont(brandFont);
    brand->setStyleSheet("color: #a33f2b;");
    sidebarLayout->addWidget(brand);
    m_searchInput = new QLineEdit(sidebar);
    m_searchInput->setProperty("sourcePlaceholder", "Search entire vault (Ctrl+F)");
    m_searchInput->setPlaceholderText(localized("Search entire vault (Ctrl+F)"));
    m_searchInput->setClearButtonEnabled(true);
    sidebarLayout->addWidget(m_searchInput);
    m_folderFilter = new QComboBox(sidebar);
    m_folderFilter->addItem(localized("All folders"), QString());
    sidebarLayout->addWidget(m_folderFilter);
    auto *notesLabel = new QLabel(localized("NOTES & FILES"), sidebar);
    notesLabel->setProperty("sourceText", "NOTES & FILES");
    notesLabel->setObjectName("sectionTitle");
    sidebarLayout->addWidget(notesLabel);
    m_noteList = new QListWidget(sidebar);
    m_noteList->setObjectName("noteList");
    m_noteList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_noteList->setUniformItemSizes(true);
    m_noteList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    sidebarLayout->addWidget(m_noteList, 3);
    auto *backlinksLabel = new QLabel(localized("BACKLINKS"), sidebar);
    backlinksLabel->setProperty("sourceText", "BACKLINKS");
    backlinksLabel->setObjectName("sectionTitle");
    sidebarLayout->addWidget(backlinksLabel);
    m_backlinkList = new QListWidget(sidebar);
    m_backlinkList->setObjectName("backlinkList");
    m_backlinkList->setUniformItemSizes(true);
    m_backlinkList->setMaximumHeight(150);
    sidebarLayout->addWidget(m_backlinkList, 1);

    m_mainStack = new QStackedWidget(root);
    auto *editorSplit = new QSplitter(Qt::Horizontal, m_mainStack);
    auto *editorPane = new QWidget(editorSplit);
    editorPane->setObjectName("editorPane");
    auto *editorLayout = new QVBoxLayout(editorPane);
    editorLayout->setContentsMargins(18, 14, 18, 8);
    editorLayout->setSpacing(8);
    auto *noteHeading = new QHBoxLayout;
    noteHeading->setContentsMargins(2, 0, 2, 0);
    m_noteTitleLabel = new QLabel(localized("No note open"), editorPane);
    m_noteTitleLabel->setObjectName("noteTitle");
    noteHeading->addWidget(m_noteTitleLabel, 1);
    auto *renameButton = new QToolButton(editorPane);
    renameButton->setObjectName("noteActionButton");
    renameButton->setText(localized("Rename"));
    renameButton->setProperty("sourceText", "Rename");
    renameButton->setProperty("sourceTooltip", "Rename note");
    renameButton->setToolTip(localized("Rename note"));
    renameButton->setEnabled(false);
    noteHeading->addWidget(renameButton);
    auto *trashButton = new QToolButton(editorPane);
    trashButton->setObjectName("trashNoteButton");
    trashButton->setText(localized("Move to trash"));
    trashButton->setProperty("sourceText", "Move to trash");
    trashButton->setProperty("sourceTooltip", "Move to trash");
    trashButton->setToolTip(localized("Move to trash"));
    trashButton->setEnabled(false);
    noteHeading->addWidget(trashButton);
    m_noteStateLabel = new QLabel(editorPane);
    m_noteStateLabel->setObjectName("noteState");
    m_noteStateLabel->setVisible(false);
    noteHeading->addWidget(m_noteStateLabel, 0, Qt::AlignVCenter);
    editorLayout->addLayout(noteHeading);
    connect(renameButton, &QToolButton::clicked, m_renameAction, &QAction::trigger);
    connect(trashButton, &QToolButton::clicked, m_deleteAction, &QAction::trigger);
    connect(m_renameAction, &QAction::changed, renameButton, [this, renameButton] {
        renameButton->setEnabled(m_renameAction->isEnabled());
    });
    connect(m_deleteAction, &QAction::changed, trashButton, [this, trashButton] {
        trashButton->setEnabled(m_deleteAction->isEnabled());
    });
    m_tagsInput = new QLineEdit(editorPane);
    m_tagsInput->setProperty("sourcePlaceholder", "Tags, separated by commas");
    m_tagsInput->setPlaceholderText(localized("Tags, separated by commas"));
    editorLayout->addWidget(m_tagsInput);
    m_editor = new QPlainTextEdit(editorPane);
    m_editor->setProperty("sourcePlaceholder", "Start writing in Markdown...");
    m_editor->setPlaceholderText(localized("Start writing in Markdown..."));
    m_editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    QFont editorFont("DejaVu Sans Mono");
    editorFont.setPointSize(10);
    m_editor->setFont(editorFont);
    m_highlighter = new MarkdownHighlighter(m_editor->document());
    auto *formatToolbar = new QToolBar(localized("Formatting"), editorPane);
    formatToolbar->setProperty("sourceTitle", "Formatting");
    formatToolbar->setMovable(false);
    formatToolbar->setFloatable(false);
    formatToolbar->setStyleSheet("QToolBar { border: 0; spacing: 3px; padding: 3px; }");
    const auto addInlineAction = [this, formatToolbar](const QString &label, const QString &tooltip,
                                                        const QString &prefix, const QString &suffix,
                                                        const QKeySequence &shortcut) {
        QAction *action = formatToolbar->addAction(label);
        action->setProperty("sourceText", label);
        action->setProperty("sourceTooltip", tooltip);
        action->setText(localized(label));
        action->setToolTip(localized(tooltip));
        if (!shortcut.isEmpty())
            action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, [this, prefix, suffix] {
            applyInlineFormat(prefix, suffix);
        });
    };
    addInlineAction("B", "Bold", "**", "**", QKeySequence::Bold);
    addInlineAction("I", "Italic", "*", "*", QKeySequence::Italic);
    addInlineAction("Code", "Inline code", "`", "`", QKeySequence());
    addInlineAction("Link", "Link", "[", "](url)", QKeySequence());
    QAction *headingAction = formatToolbar->addAction("H1");
    headingAction->setProperty("sourceTooltip", "Toggle level 1 heading");
    headingAction->setToolTip(localized("Toggle level 1 heading"));
    connect(headingAction, &QAction::triggered, this, [this] { applyLineFormat("# "); });
    QAction *subheadingAction = formatToolbar->addAction("H2");
    subheadingAction->setProperty("sourceTooltip", "Toggle level 2 heading");
    subheadingAction->setToolTip(localized("Toggle level 2 heading"));
    connect(subheadingAction, &QAction::triggered, this, [this] { applyLineFormat("## "); });
    QAction *listAction = formatToolbar->addAction(localized("List"));
    listAction->setProperty("sourceText", "List");
    listAction->setProperty("sourceTooltip", "Toggle unordered list");
    listAction->setToolTip(localized("Toggle unordered list"));
    connect(listAction, &QAction::triggered, this, [this] { applyLineFormat("- "); });
    QAction *quoteAction = formatToolbar->addAction(localized("Quote"));
    quoteAction->setProperty("sourceText", "Quote");
    quoteAction->setProperty("sourceTooltip", "Toggle block quote");
    quoteAction->setToolTip(localized("Toggle block quote"));
    connect(quoteAction, &QAction::triggered, this, [this] { applyLineFormat("> "); });
    m_preview = new QTextBrowser(editorSplit);
    m_preview->setOpenLinks(false);
    editorLayout->addWidget(formatToolbar);
    editorLayout->addWidget(m_editor, 1);
    editorSplit->addWidget(editorPane);
    editorSplit->addWidget(m_preview);
    editorSplit->setStretchFactor(0, 1);
    editorSplit->setStretchFactor(1, 1);
    m_graph = new GraphView(m_mainStack);
    m_mainStack->addWidget(editorSplit);
    m_mainStack->addWidget(m_graph);

    layout->addWidget(sidebar);
    layout->addWidget(m_mainStack, 1);
    setCentralWidget(root);
    statusBar()->showMessage(localized("Ready"));

    connect(m_searchInput, &QLineEdit::textChanged, this, &MainWindow::updateSearchResults);
    connect(m_folderFilter, &QComboBox::currentIndexChanged, this, &MainWindow::updateSearchResults);
    connect(m_tagsInput, &QLineEdit::editingFinished, this, [this] {
        if (m_currentNote.isEmpty())
            return;
        QStringList tags;
        for (const QString &tag : m_tagsInput->text().split(',', Qt::SkipEmptyParts)) {
            const QString normalized = tag.trimmed();
            if (!normalized.isEmpty() && !tags.contains(normalized, Qt::CaseInsensitive))
                tags.append(normalized);
        }
        m_tags.insert(m_currentNote, tags);
        saveMetadata();
        const QString content = isEncryptedNote(m_currentNote) ? QString() : m_editor->toPlainText();
        m_index.indexNote(m_currentNote, QFileInfo(m_currentNote).completeBaseName(), content,
                          tags.join(QLatin1Char(' ')));
        for (int i = 0; i < m_noteList->count(); ++i) {
            QListWidgetItem *item = m_noteList->item(i);
            if (item->data(Qt::UserRole).toString() != m_currentNote)
                continue;
            QString label = QFileInfo(m_currentNote).completeBaseName();
            if (isEncryptedNote(m_currentNote))
                label = QStringLiteral("[Encrypted] ") + label;
            const QString folder = QFileInfo(m_currentNote).path();
            if (folder != QStringLiteral("."))
                label += QStringLiteral("  ·  ") + folder;
            if (!tags.isEmpty())
                label += QStringLiteral("  #") + tags.join(QStringLiteral(" #"));
            item->setText(label);
            break;
        }
        updateSearchResults();
    });
    connect(m_noteList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        const QString path = item->data(Qt::UserRole).toString();
        if (item->data(Qt::UserRole + 1).toBool())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(m_vaultPath).filePath(path)));
        else
            openNote(path);
    });
    connect(m_noteList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString path = item->data(Qt::UserRole).toString();
        if (item->data(Qt::UserRole + 1).toBool())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(m_vaultPath).filePath(path)));
        else
            openNote(path);
    });
    connect(m_backlinkList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        openNote(item->data(Qt::UserRole).toString());
    });
    connect(m_backlinkList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        openNote(item->data(Qt::UserRole).toString());
    });
    connect(m_editor, &QPlainTextEdit::textChanged, this, &MainWindow::updatePreview);
    connect(m_editor->document(), &QTextDocument::undoAvailable, m_undoAction, &QAction::setEnabled);
    connect(m_editor->document(), &QTextDocument::redoAvailable, m_redoAction, &QAction::setEnabled);
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_currentNote.isEmpty()) {
            setWindowTitle("evidence - " + QFileInfo(m_currentNote).completeBaseName() + "*");
            updateCurrentLinks();
        }
    });
    connect(m_preview, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() == "evidence") {
            const QString encodedPath = url.path(QUrl::FullyEncoded);
            openNote(QUrl::fromPercentEncoding(encodedPath.mid(1).toUtf8()));
        } else if (url.scheme().isEmpty() && !m_currentNote.isEmpty()) {
            const QString target = QUrl::fromPercentEncoding(url.path(QUrl::FullyEncoded).toUtf8());
            const QString noteFolder = QFileInfo(m_currentNote).path();
            const QString filePath = QDir(m_vaultPath).filePath(QDir(noteFolder).filePath(target));
            if (QFileInfo::exists(filePath))
                QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        }
    });
    connect(m_graph, &GraphView::noteActivated, this, [this](const QString &path) {
        m_mainStack->setCurrentIndex(0);
        openNote(path);
    });
}

void MainWindow::openVault(const QString &path)
{
    const QString absolutePath = QDir(path).absolutePath();
    if (absolutePath == m_vaultPath)
        return;

    if (m_editor->document()->isModified()) {
        const auto answer = QMessageBox::question(this, localized("Unsaved changes"),
                               localized("Save changes before opening another folder?"),
                                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                                   QMessageBox::Save);
        if (answer == QMessageBox::Cancel)
            return;
        if (answer == QMessageBox::Save) {
            saveNote();
            if (m_editor->document()->isModified())
                return;
        }
    }

    m_vaultPath = absolutePath;
    m_currentNote.clear();
    m_notePassword.fill(QChar(0));
    m_notePassword.clear();
    m_tags.clear();
    m_encryptionAction->setEnabled(false);
    m_encryptionAction->setText(localized("Encrypt note"));
    m_renameAction->setEnabled(false);
    m_deleteAction->setEnabled(false);
    m_linkAction->setEnabled(false);
    m_editor->clear();
    m_tagsInput->clear();
    m_editor->document()->setModified(false);
    QDir().mkpath(QDir(m_vaultPath).filePath(".evidence"));
    loadMetadata();
    const QString databasePath = QDir(m_vaultPath).filePath(".evidence/search.sqlite");
    const bool indexReady = m_index.open(databasePath);
    setWindowTitle(QStringLiteral("evidence - ") + QFileInfo(m_vaultPath).fileName());
    refreshVault();
    if (!indexReady)
        statusBar()->showMessage(localized("Search index is unavailable; notes are still accessible"));
}

QStringList MainWindow::noteFiles() const
{
    QStringList files;
    QDirIterator iterator(m_vaultPath, {"*.md", "*.markdown", "*.evnote"}, QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QString relativePath = QDir(m_vaultPath).relativeFilePath(path);
        if (relativePath.startsWith(QStringLiteral("attachments/"), Qt::CaseInsensitive) ||
            path.contains(QDir::separator() + QStringLiteral(".evidence") + QDir::separator()))
            continue;
        files.append(path);
    }
    return files;
}

QStringList MainWindow::attachmentFiles() const
{
    QStringList files;
    const QString attachmentRoot = QDir(m_vaultPath).filePath("attachments");
    QDirIterator iterator(attachmentRoot, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext())
        files.append(QDir(m_vaultPath).relativeFilePath(iterator.next()));
    return files;
}

void MainWindow::loadMetadata()
{
    QFile file(QDir(m_vaultPath).filePath(".evidence/metadata.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return;
    const QJsonObject notes = document.object();
    for (auto it = notes.begin(); it != notes.end(); ++it) {
        QStringList tags;
        for (const QJsonValue &value : it.value().toArray()) {
            if (value.isString())
                tags.append(value.toString());
        }
        m_tags.insert(it.key(), tags);
    }
}

void MainWindow::saveMetadata() const
{
    QJsonObject notes;
    for (auto it = m_tags.cbegin(); it != m_tags.cend(); ++it) {
        QJsonArray tags;
        for (const QString &tag : it.value())
            tags.append(tag);
        notes.insert(it.key(), tags);
    }
    QSaveFile file(QDir(m_vaultPath).filePath(".evidence/metadata.json"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(notes).toJson(QJsonDocument::Indented));
        file.commit();
    }
}

QStringList MainWindow::templateNames() const
{
    const QDir directory(QDir(m_vaultPath).filePath(".evidence/templates"));
    QStringList names = directory.entryList({"*.md"}, QDir::Files, QDir::Name);
    for (QString &name : names)
        name.chop(3);
    return names;
}

QString MainWindow::templateText(const QString &name) const
{
    QFile file(QDir(m_vaultPath).filePath(QStringLiteral(".evidence/templates/%1.md").arg(name)));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

void MainWindow::manageTemplates()
{
    if (m_vaultPath.isEmpty())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(localized("Note templates"));
    dialog.resize(640, 480);
    auto *layout = new QVBoxLayout(&dialog);
    auto *selector = new QComboBox(&dialog);
    auto *nameInput = new QLineEdit(&dialog);
    nameInput->setPlaceholderText(localized("Template name"));
    auto *bodyInput = new QPlainTextEdit(&dialog);
    bodyInput->setPlaceholderText(localized("Write a Markdown template..."));
    auto *form = new QFormLayout;
    form->addRow(localized("Existing template"), selector);
    form->addRow(localized("Name"), nameInput);
    form->addRow(localized("Markdown"), bodyInput);
    layout->addLayout(form, 1);

    auto *buttons = new QHBoxLayout;
    auto *newButton = new QPushButton(localized("New"), &dialog);
    auto *saveButton = new QPushButton(localized("Save template"), &dialog);
    auto *deleteButton = new QPushButton(localized("Delete"), &dialog);
    auto *closeButton = new QPushButton(localized("Close"), &dialog);
    buttons->addWidget(newButton);
    buttons->addWidget(saveButton);
    buttons->addWidget(deleteButton);
    buttons->addStretch(1);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    QString currentFileName;
    const auto reloadTemplates = [this, selector] {
        selector->clear();
        selector->addItem(localized("New template"), QString());
        for (const QString &name : templateNames())
            selector->addItem(name, name);
    };
    reloadTemplates();
    connect(selector, &QComboBox::currentIndexChanged, &dialog, [this, selector, nameInput, bodyInput,
                                                                  &currentFileName](int) {
        currentFileName = selector->currentData().toString();
        nameInput->setText(currentFileName);
        bodyInput->setPlainText(currentFileName.isEmpty() ? QString() : templateText(currentFileName));
    });
    connect(newButton, &QPushButton::clicked, &dialog, [selector] { selector->setCurrentIndex(0); });
    connect(saveButton, &QPushButton::clicked, &dialog, [this, selector, nameInput, bodyInput,
                                                          &currentFileName, reloadTemplates] {
        const QString name = nameInput->text().trimmed();
        const QString safeName = QString(name).replace(QRegularExpression("[^\\p{L}\\p{N}_ -]"), "_")
                                  .replace(QRegularExpression("\\s+"), "_");
        if (safeName.isEmpty()) {
            QMessageBox::warning(this, localized("Templates"), localized("Enter a valid template name."));
            return;
        }
        const QString directory = QDir(m_vaultPath).filePath(".evidence/templates");
        if (!QDir().mkpath(directory)) {
            QMessageBox::warning(this, localized("Templates"), localized("Could not create the templates folder."));
            return;
        }
        QSaveFile file(QDir(directory).filePath(safeName + ".md"));
        const QByteArray content = bodyInput->toPlainText().toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size() || !file.commit()) {
            QMessageBox::warning(this, localized("Templates"), localized("Could not save the template."));
            return;
        }
        if (!currentFileName.isEmpty() && currentFileName != safeName)
            QFile::remove(QDir(directory).filePath(currentFileName + ".md"));
        currentFileName = safeName;
        reloadTemplates();
        selector->setCurrentIndex(selector->findData(safeName));
        statusBar()->showMessage(localized("Saved template %1").arg(safeName), 2500);
    });
    connect(deleteButton, &QPushButton::clicked, &dialog, [this, selector, nameInput, bodyInput,
                                                            &currentFileName, reloadTemplates] {
        if (currentFileName.isEmpty())
            return;
        if (QMessageBox::question(this, localized("Delete template"),
                      localized("Delete '%1'?").arg(currentFileName),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        QFile::remove(QDir(m_vaultPath).filePath(".evidence/templates/" + currentFileName + ".md"));
        currentFileName.clear();
        reloadTemplates();
        selector->setCurrentIndex(0);
        nameInput->clear();
        bodyInput->clear();
    });
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::refreshVault()
{
    if (m_vaultPath.isEmpty())
        return;

    const QStringList files = noteFiles();
    const QStringList attachments = attachmentFiles();
    QStringList relativePaths;
    QStringList indexedPaths;
    QMap<QString, QString> titleToPath;
    QMap<QString, QString> titles;
    QMap<QString, QStringList> links;
    const QRegularExpression linkPattern("\\[\\[([^]|#]+)(?:\\|([^]]+))?\\]\\]");
    bool currentNoteChangedWhileEditing = false;
    for (const QString &filePath : files) {
        const QString relativePath = QDir(m_vaultPath).relativeFilePath(filePath);
        const QString title = QFileInfo(filePath).completeBaseName();
        const bool encrypted = isEncryptedNote(relativePath);
        QString content;
        if (!encrypted) {
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            content = QString::fromUtf8(file.readAll());
        }
        relativePaths.append(relativePath);
        indexedPaths.append(relativePath);
        titles.insert(relativePath, title);
        titleToPath.insert(title.toCaseFolded(), relativePath);
        titleToPath.insert(relativePath.toCaseFolded(), relativePath);
        m_index.indexNote(relativePath, title, content, m_tags.value(relativePath).join(QLatin1Char(' ')));
        if (relativePath == m_currentNote) {
            if (encrypted)
                continue;
            if (m_editor->document()->isModified()) {
                currentNoteChangedWhileEditing = content != m_editor->toPlainText();
            } else if (content != m_editor->toPlainText()) {
                m_editor->setPlainText(content);
                m_editor->document()->setModified(false);
            }
        }
    }

    for (const QString &relativePath : attachments) {
        const QString filePath = QDir(m_vaultPath).filePath(relativePath);
        const QString title = QFileInfo(filePath).fileName();
        QString content;
        const QString suffix = QFileInfo(filePath).suffix().toCaseFolded();
        if (suffix == QStringLiteral("pdf")) {
            content = extractPdfText(filePath);
        } else if (suffix == QStringLiteral("txt") || suffix == QStringLiteral("md") ||
                   suffix == QStringLiteral("markdown") || suffix == QStringLiteral("csv") ||
                   suffix == QStringLiteral("log")) {
            QFile file(filePath);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text))
                content = QString::fromUtf8(file.readAll());
        }
        indexedPaths.append(relativePath);
        m_index.indexAttachment(relativePath, title, content);
    }

    for (const QString &filePath : files) {
        const QString relativePath = QDir(m_vaultPath).relativeFilePath(filePath);
        QString content;
        if (isEncryptedNote(relativePath)) {
            if (relativePath != m_currentNote || m_notePassword.isEmpty())
                continue;
            content = m_editor->toPlainText();
        } else {
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            content = QString::fromUtf8(file.readAll());
        }
        auto matches = linkPattern.globalMatch(content);
        while (matches.hasNext()) {
            const QString target = matches.next().captured(1).trimmed();
            QString resolved = titleToPath.value(target.toCaseFolded());
            if (resolved.isEmpty()) {
                const QString normalized = target.endsWith(".md", Qt::CaseInsensitive) ? target : target + ".md";
                for (const QString &candidate : files) {
                    const QString candidateRelative = QDir(m_vaultPath).relativeFilePath(candidate);
                    if (candidateRelative.compare(normalized, Qt::CaseInsensitive) == 0) {
                        resolved = candidateRelative;
                        break;
                    }
                }
                if (resolved.isEmpty()) {
                    const QString encryptedPath = normalized.left(normalized.lastIndexOf(QLatin1Char('.'))) +
                                                  QStringLiteral(".evnote");
                    for (const QString &candidate : files) {
                        const QString candidateRelative = QDir(m_vaultPath).relativeFilePath(candidate);
                        if (candidateRelative.compare(encryptedPath, Qt::CaseInsensitive) == 0) {
                            resolved = candidateRelative;
                            break;
                        }
                    }
                }
            }
            if (!resolved.isEmpty())
                links[relativePath].append(resolved);
        }
    }

    m_titles = titles;
    m_links = links;
    m_index.removeMissing(indexedPaths);

    m_watcher->removePaths(m_watcher->files());
    m_watcher->removePaths(m_watcher->directories());
    QStringList directories{m_vaultPath};
    QDirIterator directoryIterator(m_vaultPath, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (directoryIterator.hasNext()) {
        const QString directory = directoryIterator.next();
        if (!directory.contains(QDir::separator() + QStringLiteral(".evidence")))
            directories.append(directory);
    }
    m_watcher->addPaths(directories);
    m_watcher->addPaths(files + attachments);

    m_noteList->clear();
    const QString selectedFolder = m_folderFilter->currentData().toString();
    QSet<QString> folders;
    QDirIterator folderIterator(m_vaultPath, QDir::Dirs | QDir::NoDotAndDotDot,
                                QDirIterator::Subdirectories);
    while (folderIterator.hasNext()) {
        const QString directory = folderIterator.next();
        const QString relativeDirectory = QDir(m_vaultPath).relativeFilePath(directory);
        if (relativeDirectory == QStringLiteral("attachments") ||
            relativeDirectory.startsWith(QStringLiteral("attachments/")) ||
            relativeDirectory.contains(QDir::separator() + QStringLiteral(".evidence")))
            continue;
        folders.insert(relativeDirectory);
    }
    QStringList sortedFolders = folders.values();
    sortedFolders.sort(Qt::CaseInsensitive);
    {
        const QSignalBlocker blocker(m_folderFilter);
        m_folderFilter->clear();
        m_folderFilter->addItem(localized("All folders"), QString());
        for (const QString &folder : sortedFolders)
            m_folderFilter->addItem(folder, folder);
        const int folderIndex = m_folderFilter->findData(selectedFolder);
        m_folderFilter->setCurrentIndex(folderIndex < 0 ? 0 : folderIndex);
    }

    for (const QString &relativePath : relativePaths) {
        QString label = titles.value(relativePath);
        if (isEncryptedNote(relativePath))
            label = localized("[Encrypted] ") + label;
        const QString folder = QFileInfo(relativePath).path();
        if (folder != QStringLiteral("."))
            label += QStringLiteral("  ·  ") + folder;
        const QStringList tags = m_tags.value(relativePath);
        if (!tags.isEmpty())
            label += QStringLiteral("  #") + tags.join(QStringLiteral(" #"));
        auto *item = new QListWidgetItem(label, m_noteList);
        item->setData(Qt::UserRole, relativePath);
        item->setData(Qt::UserRole + 1, false);
    }
    for (const QString &relativePath : attachments) {
        auto *item = new QListWidgetItem(localized("[File] ") + QFileInfo(relativePath).fileName(), m_noteList);
        item->setData(Qt::UserRole, relativePath);
        item->setData(Qt::UserRole + 1, true);
    }
    m_graph->setGraph(relativePaths, links, m_currentNote);
    if (!m_currentNote.isEmpty())
        updateCurrentLinks();
    else
        updateBacklinks();
    updateSearchResults();
    const QString message = currentNoteChangedWhileEditing
                                                                ? localized("The open note changed on disk; your unsaved edits were kept")
                                                                    : localized("%1 notes · %2 files · %3")
                                      .arg(relativePaths.size()).arg(attachments.size()).arg(m_vaultPath);
    statusBar()->showMessage(message);
    if (!m_searchInput->text().trimmed().isEmpty())
        updateSearchResults();
    updateNoteHeader();
}

void MainWindow::createNote()
{
    if (m_vaultPath.isEmpty())
        return;

    QString selectedTemplate;
    const QStringList availableTemplates = templateNames();
    if (!availableTemplates.isEmpty()) {
        const QString blankChoice = localized("Blank");
        QStringList choices{blankChoice};
        choices.append(availableTemplates);
        bool accepted = false;
        const QString choice = QInputDialog::getItem(this, localized("New note"), localized("Template"),
                                                      choices, 0, false, &accepted);
        if (!accepted)
            return;
        if (choice != blankChoice)
            selectedTemplate = choice;
    }

    const QString folder = m_folderFilter->currentData().toString();
    QString name = "Untitled";
    QString path = QDir(m_vaultPath).filePath(QDir(folder).filePath(name + ".md"));
    int number = 2;
    while (QFileInfo::exists(path))
        path = QDir(m_vaultPath).filePath(QDir(folder).filePath(QString("%1 %2.md").arg(name).arg(number++)));
    QDir().mkpath(QFileInfo(path).path());
    QString content = selectedTemplate.isEmpty() ? QString() : templateText(selectedTemplate);
    content.replace("{{title}}", QFileInfo(path).completeBaseName());
    content.replace("{{date}}", QDate::currentDate().toString(Qt::ISODate));
    const QByteArray encodedContent = content.toUtf8();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(encodedContent) != encodedContent.size() || !file.commit()) {
        QMessageBox::warning(this, QStringLiteral("evidence"), localized("Could not create the note."));
        return;
    }
    refreshVault();
    openNote(QDir(m_vaultPath).relativeFilePath(path));
}

void MainWindow::renameCurrentNote()
{
    if (m_currentNote.isEmpty())
        return;
    if (m_editor->document()->isModified()) {
        saveNote();
        if (m_editor->document()->isModified())
            return;
    }

    const QFileInfo originalInfo(m_currentNote);
    bool accepted = false;
    const QString requestedName = QInputDialog::getText(this, localized("Rename note"),
                                                         localized("Enter a new note name"),
                                                         QLineEdit::Normal, originalInfo.completeBaseName(),
                                                         &accepted).trimmed();
    if (!accepted || requestedName.isEmpty() || requestedName == originalInfo.completeBaseName())
        return;
    static const QRegularExpression invalidNamePattern(QStringLiteral("[<>:\"/\\\\|?*]"));
    if (requestedName == QStringLiteral(".") || requestedName == QStringLiteral("..") ||
        requestedName.contains(invalidNamePattern) || requestedName.endsWith(QLatin1Char('.')) ||
        requestedName.endsWith(QLatin1Char(' '))) {
        QMessageBox::warning(this, localized("Rename note"),
                             localized("The note name contains invalid characters."));
        return;
    }

    QString newName = requestedName;
    if (!originalInfo.suffix().isEmpty() &&
        !newName.endsWith(QLatin1Char('.') + originalInfo.suffix(), Qt::CaseInsensitive))
        newName += QLatin1Char('.') + originalInfo.suffix();
    const QString newPath = QDir(originalInfo.path()).filePath(newName);
    const QString absoluteNewPath = QDir(m_vaultPath).filePath(newPath);
    if (QFileInfo::exists(absoluteNewPath)) {
        QMessageBox::warning(this, localized("Rename note"), localized("A note with that name already exists."));
        return;
    }
    if (!QFile::rename(QDir(m_vaultPath).filePath(m_currentNote), absoluteNewPath)) {
        QMessageBox::warning(this, localized("Rename note"), localized("Could not rename the note."));
        return;
    }

    const QString previousPath = m_currentNote;
    if (m_tags.contains(previousPath))
        m_tags.insert(newPath, m_tags.take(previousPath));
    m_currentNote = newPath;
    saveMetadata();
    refreshVault();
    setWindowTitle(QStringLiteral("evidence - ") + QFileInfo(m_currentNote).completeBaseName());
}

void MainWindow::deleteCurrentNote()
{
    if (m_currentNote.isEmpty())
        return;
    if (m_editor->document()->isModified()) {
        const auto answer = QMessageBox::question(this, localized("Unsaved changes"),
                                                   localized("Save changes before deleting the note?"),
                                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                                   QMessageBox::Save);
        if (answer == QMessageBox::Cancel)
            return;
        if (answer == QMessageBox::Save) {
            saveNote();
            if (m_editor->document()->isModified())
                return;
        }
    }
    moveCurrentNoteToTrash();
}

void MainWindow::moveCurrentNoteToTrash()
{
    const QString deletedPath = m_currentNote;
    const QString prompt = localized("Move '%1' to the trash?").arg(QFileInfo(deletedPath).completeBaseName());
    if (QMessageBox::question(this, localized("Trash"), prompt,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    const QString entryName = QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmsszzz") +
                              QLatin1Char('-') + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString entryDirectory = QDir(m_vaultPath).filePath(".evidence/trash/" + entryName);
    if (!QDir().mkpath(entryDirectory)) {
        QMessageBox::warning(this, localized("Trash"), localized("Could not move the note to the trash."));
        return;
    }

    QJsonArray tags;
    for (const QString &tag : m_tags.value(deletedPath))
        tags.append(tag);
    const QFileInfo originalInfo(deletedPath);
    QJsonObject manifestObject;
    manifestObject.insert("originalPath", deletedPath);
    manifestObject.insert("storedName", originalInfo.fileName());
    manifestObject.insert("tags", tags);
    QSaveFile manifest(QDir(entryDirectory).filePath("manifest.json"));
    const QByteArray manifestBytes = QJsonDocument(manifestObject).toJson(QJsonDocument::Compact);
    if (!manifest.open(QIODevice::WriteOnly) || manifest.write(manifestBytes) != manifestBytes.size() ||
        !manifest.commit()) {
        QDir(entryDirectory).removeRecursively();
        QMessageBox::warning(this, localized("Trash"), localized("Could not move the note to the trash."));
        return;
    }

    if (!m_index.forgetNote(deletedPath)) {
        QDir(entryDirectory).removeRecursively();
        QMessageBox::warning(this, localized("Trash"), localized("Could not move the note to the trash."));
        return;
    }
    const QString storedPath = QDir(entryDirectory).filePath(originalInfo.fileName());
    if (!QFile::rename(QDir(m_vaultPath).filePath(deletedPath), storedPath)) {
        QDir(entryDirectory).removeRecursively();
        refreshVault();
        QMessageBox::warning(this, localized("Trash"), localized("Could not move the note to the trash."));
        return;
    }
    m_tags.remove(deletedPath);
    m_links.remove(deletedPath);
    m_notePassword.fill(QChar(0));
    m_notePassword.clear();
    m_currentNote.clear();
    m_editor->clear();
    m_tagsInput->clear();
    m_editor->document()->setModified(false);
    m_encryptionAction->setEnabled(false);
    m_renameAction->setEnabled(false);
    m_deleteAction->setEnabled(false);
    m_linkAction->setEnabled(false);
    m_undoAction->setEnabled(false);
    m_redoAction->setEnabled(false);
    m_encryptionAction->setText(localized("Encrypt note"));
    saveMetadata();
    refreshVault();
    setWindowTitle(QStringLiteral("evidence - ") + QFileInfo(m_vaultPath).fileName());
}

void MainWindow::manageTrash()
{
    const QString trashRoot = QDir(m_vaultPath).filePath(".evidence/trash");
    QDialog dialog(this);
    dialog.setWindowTitle(localized("Trash"));
    dialog.resize(520, 380);
    auto *layout = new QVBoxLayout(&dialog);
    auto *entries = new QListWidget(&dialog);
    auto *emptyLabel = new QLabel(localized("No notes in trash"), &dialog);
    emptyLabel->setAlignment(Qt::AlignCenter);
    auto *buttons = new QHBoxLayout;
    auto *restoreButton = new QPushButton(localized("Restore"), &dialog);
    auto *permanentButton = new QPushButton(localized("Delete permanently"), &dialog);
    auto *closeButton = new QPushButton(localized("Close"), &dialog);
    buttons->addWidget(restoreButton);
    buttons->addWidget(permanentButton);
    buttons->addStretch(1);
    buttons->addWidget(closeButton);
    layout->addWidget(entries, 1);
    layout->addWidget(emptyLabel);
    layout->addLayout(buttons);

    const auto reloadEntries = [this, trashRoot, entries, emptyLabel] {
        entries->clear();
        const QDir root(trashRoot);
        for (const QString &entryName : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString entryDirectory = root.filePath(entryName);
            QFile manifest(QDir(entryDirectory).filePath("manifest.json"));
            if (!manifest.open(QIODevice::ReadOnly))
                continue;
            const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll());
            if (!document.isObject())
                continue;
            const QString originalPath = document.object().value("originalPath").toString();
            const QString storedName = document.object().value("storedName").toString();
            if (originalPath.isEmpty() || storedName.isEmpty() ||
                !QFileInfo::exists(QDir(entryDirectory).filePath(storedName)))
                continue;
            auto *item = new QListWidgetItem(QDir::toNativeSeparators(originalPath), entries);
            item->setData(Qt::UserRole, entryName);
        }
        emptyLabel->setVisible(entries->count() == 0);
    };
    reloadEntries();

    connect(restoreButton, &QPushButton::clicked, &dialog, [this, entries, reloadEntries, trashRoot] {
        QListWidgetItem *item = entries->currentItem();
        if (!item)
            return;
        const QString entryDirectory = QDir(trashRoot).filePath(item->data(Qt::UserRole).toString());
        QFile manifest(QDir(entryDirectory).filePath("manifest.json"));
        if (!manifest.open(QIODevice::ReadOnly))
            return;
        const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll());
        if (!document.isObject())
            return;
        const QJsonObject data = document.object();
        const QString originalPath = QDir::cleanPath(data.value("originalPath").toString());
        const QString storedName = data.value("storedName").toString();
        if (originalPath.isEmpty() || QDir::isAbsolutePath(originalPath) ||
            originalPath == QStringLiteral("..") || originalPath.startsWith(QStringLiteral("../")) ||
            originalPath.startsWith(QStringLiteral(".evidence/")) ||
            originalPath.startsWith(QStringLiteral("attachments/"), Qt::CaseInsensitive) ||
            originalPath.contains('\\') ||
            originalPath.split(QLatin1Char('/')).contains(QStringLiteral(".evidence")) ||
            storedName.isEmpty() || storedName == QStringLiteral(".") || storedName == QStringLiteral("..") ||
            QFileInfo(storedName).fileName() != storedName || storedName.contains('/') || storedName.contains('\\'))
            return;

        const QString destination = QDir(m_vaultPath).filePath(originalPath);
        if (QFileInfo::exists(destination)) {
            QMessageBox::warning(this, localized("Restore"), localized("A note already exists at the original location."));
            return;
        }
        const QString prompt = localized("Restore '%1' to its original location?")
                                   .arg(QFileInfo(originalPath).fileName());
        if (QMessageBox::question(this, localized("Restore"), prompt,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        QDir().mkpath(QFileInfo(destination).path());
        const QString storedPath = QDir(entryDirectory).filePath(storedName);
        if (!QFile::rename(storedPath, destination)) {
            QMessageBox::warning(this, localized("Restore"), localized("Could not restore the note."));
            return;
        }

        QStringList tags;
        for (const QJsonValue &value : data.value("tags").toArray()) {
            if (value.isString())
                tags.append(value.toString());
        }
        if (!tags.isEmpty())
            m_tags.insert(originalPath, tags);
        QDir(entryDirectory).removeRecursively();
        saveMetadata();
        refreshVault();
        reloadEntries();
    });
    connect(permanentButton, &QPushButton::clicked, &dialog, [this, entries, reloadEntries, trashRoot] {
        QListWidgetItem *item = entries->currentItem();
        if (!item)
            return;
        const QString entryName = item->data(Qt::UserRole).toString();
        const QString prompt = localized("Delete '%1' permanently?").arg(item->text());
        if (QMessageBox::question(this, localized("Delete permanently"), prompt,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        if (!QDir(QDir(trashRoot).filePath(entryName)).removeRecursively()) {
            QMessageBox::warning(this, localized("Delete permanently"), localized("Could not delete the note."));
            return;
        }
        reloadEntries();
    });
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::exportVault()
{
    if (m_vaultPath.isEmpty())
        return;
    QStringList scopes;
    if (!m_currentNote.isEmpty())
        scopes.append(localized("Current note"));
    if (!m_noteList->selectedItems().isEmpty())
        scopes.append(localized("Selected notes"));
    scopes.append(localized("Current folder"));
    scopes.append(localized("Entire vault"));

    bool accepted = false;
    const QString scope = QInputDialog::getItem(this, localized("Export notes"), localized("Choose what to export"),
                                                 scopes, 0, false, &accepted);
    if (!accepted)
        return;

    QStringList paths;
    if (scope == localized("Current note")) {
        paths.append(m_currentNote);
    } else if (scope == localized("Selected notes")) {
        for (QListWidgetItem *item : m_noteList->selectedItems()) {
            if (!item->data(Qt::UserRole + 1).toBool())
                paths.append(item->data(Qt::UserRole).toString());
        }
    } else if (scope == localized("Current folder")) {
        const QString folder = m_folderFilter->currentData().toString();
        for (const QString &absolutePath : noteFiles()) {
            const QString relativePath = QDir(m_vaultPath).relativeFilePath(absolutePath);
            if (folder.isEmpty() || relativePath.startsWith(folder + QLatin1Char('/')))
                paths.append(relativePath);
        }
    } else {
        for (const QString &absolutePath : noteFiles())
            paths.append(QDir(m_vaultPath).relativeFilePath(absolutePath));
    }
    paths.removeDuplicates();
    if (paths.isEmpty()) {
        QMessageBox::information(this, localized("Export notes"), localized("No notes selected for export."));
        return;
    }

    QString markdown;
    struct ExportRecord { QString path; QString title; QString content; QStringList tags; };
    QList<ExportRecord> records;
    for (const QString &path : paths) {
        QString content;
        if (path == m_currentNote) {
            content = m_editor->toPlainText();
        } else {
            QFile file(QDir(m_vaultPath).filePath(path));
            if (!file.open(QIODevice::ReadOnly)) {
                QMessageBox::warning(this, localized("Export notes"), localized("Could not read the selected note."));
                return;
            }
            QByteArray bytes = file.readAll();
            if (isEncryptedNote(path)) {
                bool passwordAccepted = false;
                const QString password = QInputDialog::getText(
                    this, localized("Export notes"),
                    localized("Enter the password to export '%1'.").arg(QFileInfo(path).completeBaseName()),
                    QLineEdit::Password, {}, &passwordAccepted);
                if (!passwordAccepted)
                    return;
                QByteArray plaintext;
                if (!SecureNote::decrypt(bytes, password, &plaintext)) {
                    QMessageBox::warning(this, localized("Export notes"),
                                         localized("The password is incorrect or the note is damaged."));
                    return;
                }
                bytes = plaintext;
            }
            content = QString::fromUtf8(bytes);
        }
        const QString title = m_titles.value(path, QFileInfo(path).completeBaseName());
        const QStringList tags = m_tags.value(path);
        records.append({path, title, content, tags});
        markdown += QStringLiteral("# %1\n\n").arg(title);
        if (!tags.isEmpty())
            markdown += QStringLiteral("**Tags:** %1\n\n").arg(tags.join(QStringLiteral(", ")));
        markdown += content + QStringLiteral("\n\n");
    }

    const QStringList formatFilters{
        localized("Plain text (*.txt)"), localized("Word document (*.doc)"),
        localized("Word document (*.docx)"), localized("Rich text (*.rtf)"),
        localized("PDF document (*.pdf)"), localized("HTML document (*.html)"),
        localized("XML document (*.xml)"), localized("EPUB book (*.epub)"),
        localized("FictionBook (*.fb2)"), localized("CSV table (*.csv)"),
        localized("Markdown document (*.md)")};
    QFileDialog saveDialog(this, localized("Export notes"),
                           QDir::home().filePath(QStringLiteral("notes-export")),
                           formatFilters.join(QStringLiteral(";;")));
    saveDialog.setAcceptMode(QFileDialog::AcceptSave);
    saveDialog.setFileMode(QFileDialog::AnyFile);
    if (saveDialog.exec() != QDialog::Accepted || saveDialog.selectedFiles().isEmpty())
        return;
    QString outputPath = saveDialog.selectedFiles().first();
    const QString selectedFilter = saveDialog.selectedNameFilter();
    QString extension;
    const QRegularExpression extensionPattern(QStringLiteral("\\*\\.([a-z0-9]+)"), QRegularExpression::CaseInsensitiveOption);
    const auto extensionMatch = extensionPattern.match(selectedFilter);
    if (extensionMatch.hasMatch())
        extension = extensionMatch.captured(1).toLower();
    if (extension.isEmpty())
        return;
    if (QFileInfo(outputPath).suffix().compare(extension, Qt::CaseInsensitive) != 0)
        outputPath += QLatin1Char('.') + extension;

    bool success = false;
    if (extension == QStringLiteral("md")) {
        QFile file(outputPath);
        success = file.open(QIODevice::WriteOnly) && file.write(markdown.toUtf8()) == markdown.toUtf8().size();
        if (success)
            success = file.flush();
    } else if (extension == QStringLiteral("txt") || extension == QStringLiteral("html") ||
               extension == QStringLiteral("pdf")) {
        QTextDocument document;
        document.setMarkdown(markdown);
        if (extension == QStringLiteral("txt")) {
            QFile file(outputPath);
            const QByteArray output = document.toPlainText().toUtf8();
            success = file.open(QIODevice::WriteOnly) && file.write(output) == output.size();
            if (success)
                success = file.flush();
        } else if (extension == QStringLiteral("html")) {
            QFile file(outputPath);
            const QByteArray output = document.toHtml().toUtf8();
            success = file.open(QIODevice::WriteOnly) && file.write(output) == output.size();
            if (success)
                success = file.flush();
        } else {
            QPdfWriter writer(outputPath);
            document.print(&writer);
            success = QFileInfo(outputPath).size() > 0;
        }
    } else if (extension == QStringLiteral("xml")) {
        QByteArray output;
        QXmlStreamWriter writer(&output);
        writer.setAutoFormatting(true);
        writer.writeStartDocument();
        writer.writeStartElement(QStringLiteral("notes"));
        for (const ExportRecord &record : records) {
            writer.writeStartElement(QStringLiteral("note"));
            writer.writeAttribute(QStringLiteral("path"), record.path);
            writer.writeTextElement(QStringLiteral("title"), record.title);
            writer.writeTextElement(QStringLiteral("content"), record.content);
            writer.writeStartElement(QStringLiteral("tags"));
            for (const QString &tag : record.tags)
                writer.writeTextElement(QStringLiteral("tag"), tag);
            writer.writeEndElement();
            writer.writeEndElement();
        }
        writer.writeEndElement();
        writer.writeEndDocument();
        QFile file(outputPath);
        success = file.open(QIODevice::WriteOnly) && file.write(output) == output.size();
        if (success)
            success = file.flush();
    } else if (extension == QStringLiteral("csv")) {
        const auto csvField = [](QString value) {
            value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
            return QStringLiteral("\"%1\"").arg(value);
        };
        QStringList rows{QStringLiteral("path,title,tags,content")};
        for (const ExportRecord &record : records) {
            rows.append(QStringList{csvField(record.path), csvField(record.title),
                                    csvField(record.tags.join(QStringLiteral(", "))),
                                    csvField(record.content)}.join(QLatin1Char(',')));
        }
        QFile file(outputPath);
        const QByteArray output = rows.join(QStringLiteral("\r\n")).toUtf8();
        success = file.open(QIODevice::WriteOnly) && file.write(output) == output.size();
        if (success)
            success = file.flush();
    } else {
        const QString pandoc = findPandocExecutable();
        if (pandoc.isEmpty()) {
            QMessageBox::warning(this, localized("Export notes"), localized("Pandoc is required for this format. Install Pandoc and try again."));
            return;
        }
        QTemporaryDir temporaryDirectory;
        if (!temporaryDirectory.isValid())
            return;
        const QString sourcePath = QDir(temporaryDirectory.path()).filePath("notes.md");
        QFile source(sourcePath);
        const QByteArray sourceBytes = markdown.toUtf8();
        if (!source.open(QIODevice::WriteOnly) || source.write(sourceBytes) != sourceBytes.size() || !source.flush())
            return;

        QString pandocTarget = extension;
        QString pandocOutput = QDir(temporaryDirectory.path()).filePath("notes." + extension);
        if (extension == QStringLiteral("doc")) {
            pandocTarget = QStringLiteral("docx");
            pandocOutput = QDir(temporaryDirectory.path()).filePath("notes.docx");
        }
        QProcess pandocProcess;
        pandocProcess.start(pandoc, {sourcePath, "--from=markdown", "--to=" + pandocTarget,
                                    "--standalone", "--output", pandocOutput});
        if (!pandocProcess.waitForStarted() || !pandocProcess.waitForFinished(-1) ||
            pandocProcess.exitStatus() != QProcess::NormalExit || pandocProcess.exitCode() != 0) {
            QMessageBox::warning(this, localized("Export notes"), localized("Could not export the selected notes."));
            return;
        }
        if (extension == QStringLiteral("doc")) {
            const QString libreOffice = findLibreOfficeExecutable();
            if (libreOffice.isEmpty()) {
                QMessageBox::warning(this, localized("Export notes"), localized("LibreOffice is required to export legacy DOC files."));
                return;
            }
            QProcess conversionProcess;
            const QString profileUrl = QUrl::fromLocalFile(
                QDir(temporaryDirectory.path()).filePath("libreoffice-profile")).toString(QUrl::FullyEncoded);
            conversionProcess.setWorkingDirectory(QFileInfo(libreOffice).path());
            conversionProcess.start(libreOffice, {"--headless", "-env:UserInstallation=" + profileUrl,
                                                   "--convert-to", "doc", "--outdir",
                                                   temporaryDirectory.path(), pandocOutput});
            if (!conversionProcess.waitForStarted() || !conversionProcess.waitForFinished(-1) ||
                conversionProcess.exitStatus() != QProcess::NormalExit || conversionProcess.exitCode() != 0)
                return;
            const QString convertedPath = QDir(temporaryDirectory.path()).filePath("notes.doc");
            QFile::remove(outputPath);
            success = QFile::copy(convertedPath, outputPath);
        } else {
            QFile::remove(outputPath);
            success = QFile::copy(pandocOutput, outputPath);
        }
    }

    if (!success) {
        QMessageBox::warning(this, localized("Export notes"), localized("Could not export the selected notes."));
        return;
    }
    statusBar()->showMessage(localized("Exported %1 notes to %2").arg(records.size()).arg(outputPath), 5000);
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(outputPath).absolutePath()));
}

void MainWindow::insertNoteLink()
{
    if (m_currentNote.isEmpty())
        return;
    QStringList choices;
    QMap<QString, QString> choiceToPath;
    for (const QString &path : m_titles.keys()) {
        if (path == m_currentNote)
            continue;
        const QString label = m_titles.value(path) + QStringLiteral("  ·  ") + path;
        choices.append(label);
        choiceToPath.insert(label, path);
    }
    if (choices.isEmpty())
        return;

    bool accepted = false;
    const QString choice = QInputDialog::getItem(this, localized("Link note"), localized("Choose a note to link"),
                                                  choices, 0, false, &accepted);
    if (!accepted)
        return;
    m_editor->setFocus();
    m_editor->textCursor().insertText(QStringLiteral("[[%1]]").arg(choiceToPath.value(choice)));
    updateCurrentLinks();
}

void MainWindow::updateCurrentLinks()
{
    if (m_currentNote.isEmpty()) {
        updateBacklinks();
        return;
    }

    QStringList outgoingLinks;
    const QRegularExpression linkPattern("\\[\\[([^]|#]+)(?:\\|([^]]+))?\\]\\]");
    auto matches = linkPattern.globalMatch(m_editor->toPlainText());
    while (matches.hasNext()) {
        const QString target = matches.next().captured(1).trimmed();
        const QString resolved = resolveWikiLink(target);
        if (!resolved.isEmpty() && !outgoingLinks.contains(resolved))
            outgoingLinks.append(resolved);
    }
    m_links.insert(m_currentNote, outgoingLinks);
    m_graph->setGraph(m_titles.keys(), m_links, m_currentNote);
    updateBacklinks();
}

void MainWindow::updateBacklinks()
{
    if (!m_backlinkList)
        return;
    m_backlinkList->clear();
    if (m_currentNote.isEmpty()) {
        auto *empty = new QListWidgetItem(localized("No backlinks"), m_backlinkList);
        empty->setFlags(Qt::NoItemFlags);
        return;
    }

    for (auto it = m_links.cbegin(); it != m_links.cend(); ++it) {
        if (it.key() == m_currentNote || !it.value().contains(m_currentNote))
            continue;
        const QString title = m_titles.value(it.key(), QFileInfo(it.key()).completeBaseName());
        auto *item = new QListWidgetItem(title + QStringLiteral("  ·  ") + it.key(), m_backlinkList);
        item->setData(Qt::UserRole, it.key());
    }
    if (m_backlinkList->count() == 0) {
        auto *empty = new QListWidgetItem(localized("No backlinks"), m_backlinkList);
        empty->setFlags(Qt::NoItemFlags);
    }
}

void MainWindow::applyInlineFormat(const QString &prefix, const QString &suffix)
{
    if (!m_editor || m_currentNote.isEmpty())
        return;

    QTextCursor cursor = m_editor->textCursor();
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    QString selected = m_editor->toPlainText().mid(start, end - start);
    if (selected.isEmpty())
        selected = "text";
    const bool removeFormat = !prefix.isEmpty() && !suffix.isEmpty() &&
                              selected.startsWith(prefix) && selected.endsWith(suffix) &&
                              selected.size() >= prefix.size() + suffix.size();
    const QString formattedText = removeFormat
                                      ? selected.mid(prefix.size(), selected.size() - prefix.size() - suffix.size())
                                      : selected;
    const QString replacement = removeFormat ? formattedText : prefix + selected + suffix;

    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.insertText(replacement);

    QTextCursor selection = m_editor->textCursor();
    const int selectionStart = start + (removeFormat ? 0 : prefix.size());
    selection.setPosition(selectionStart);
    selection.setPosition(selectionStart + formattedText.size(), QTextCursor::KeepAnchor);
    m_editor->setTextCursor(selection);
    m_editor->setFocus();
}

void MainWindow::applyLineFormat(const QString &prefix)
{
    if (!m_editor || m_currentNote.isEmpty())
        return;

    QTextCursor cursor = m_editor->textCursor();
    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    const QTextBlock lastBlock = m_editor->document()->findBlock(selectionEnd);
    QList<int> blockPositions;
    bool allPrefixed = true;
    for (QTextBlock block = m_editor->document()->findBlock(selectionStart);
         block.isValid() && block.position() <= lastBlock.position(); block = block.next()) {
        blockPositions.append(block.position());
        allPrefixed = allPrefixed && block.text().startsWith(prefix);
    }

    for (auto it = blockPositions.crbegin(); it != blockPositions.crend(); ++it) {
        QTextCursor lineCursor(m_editor->document());
        lineCursor.setPosition(*it);
        if (allPrefixed) {
            lineCursor.setPosition(*it + prefix.size(), QTextCursor::KeepAnchor);
            lineCursor.removeSelectedText();
        } else if (!m_editor->document()->findBlock(*it).text().startsWith(prefix)) {
            lineCursor.insertText(prefix);
        }
    }

    m_editor->setTextCursor(cursor);
    m_editor->setFocus();
}

void MainWindow::createFolder()
{
    if (m_vaultPath.isEmpty())
        return;

    bool accepted = false;
    const QString requested = QInputDialog::getText(this, localized("New folder"), localized("Folder name"), QLineEdit::Normal,
                                                     {}, &accepted).trimmed();
    if (!accepted || requested.isEmpty())
        return;

    const QString relativePath = QDir::cleanPath(requested);
    if (QDir::isAbsolutePath(relativePath) || relativePath == QStringLiteral("..") ||
        relativePath.startsWith(QStringLiteral("../")) || relativePath == QStringLiteral(".")) {
        QMessageBox::warning(this, QStringLiteral("evidence"), localized("Choose a folder path inside the current notes folder."));
        return;
    }
    if (relativePath.compare(QStringLiteral("attachments"), Qt::CaseInsensitive) == 0 ||
        relativePath.startsWith(QStringLiteral("attachments/"), Qt::CaseInsensitive)) {
        QMessageBox::warning(this, QStringLiteral("evidence"), localized("That folder name is reserved for attached files."));
        return;
    }
    for (const QString &part : relativePath.split('/')) {
        if (part == QStringLiteral(".evidence")) {
            QMessageBox::warning(this, QStringLiteral("evidence"), localized("That folder name is reserved."));
            return;
        }
    }
    if (!QDir(m_vaultPath).mkpath(relativePath)) {
        QMessageBox::warning(this, QStringLiteral("evidence"), localized("Could not create the folder."));
        return;
    }
    refreshVault();
    const int folderIndex = m_folderFilter->findData(relativePath);
    if (folderIndex >= 0)
        m_folderFilter->setCurrentIndex(folderIndex);
}

void MainWindow::attachFile()
{
    if (m_currentNote.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("evidence"), localized("Open a note before attaching a file."));
        return;
    }

    const QString sourcePath = QFileDialog::getOpenFileName(this, localized("Attach file"));
    if (sourcePath.isEmpty())
        return;

    const QString attachmentDirectory = QDir(m_vaultPath).filePath("attachments");
    if (!QDir().mkpath(attachmentDirectory)) {
        QMessageBox::warning(this, QStringLiteral("evidence"), localized("Could not create the attachments folder."));
        return;
    }
    const QFileInfo sourceInfo(sourcePath);
    QString destinationPath = QDir(attachmentDirectory).filePath(sourceInfo.fileName());
    int number = 2;
    while (QFileInfo::exists(destinationPath)) {
        destinationPath = QDir(attachmentDirectory).filePath(
            QStringLiteral("%1 (%2)%3").arg(sourceInfo.completeBaseName()).arg(number++).arg(
                sourceInfo.suffix().isEmpty() ? QString() : QStringLiteral(".") + sourceInfo.suffix()));
    }
    if (!QFile::copy(sourcePath, destinationPath)) {
        QMessageBox::warning(this, QStringLiteral("evidence"), localized("Could not copy the attachment into this notes folder."));
        return;
    }

    const QString relativePath = QDir(m_vaultPath).relativeFilePath(destinationPath);
    const QString noteFolder = QFileInfo(m_currentNote).path();
    const QString linkPath = QDir(noteFolder).relativeFilePath(relativePath);
    m_editor->appendPlainText(QStringLiteral("\n[%1](<%2>)").arg(sourceInfo.fileName(), linkPath));
    refreshVault();
}

void MainWindow::saveNote()
{
    if (m_currentNote.isEmpty() || m_vaultPath.isEmpty())
        return;

    const QString path = QDir(m_vaultPath).filePath(m_currentNote);
    QByteArray content = m_editor->toPlainText().toUtf8();
    if (isEncryptedNote(m_currentNote)) {
        QByteArray envelope;
        if (m_notePassword.isEmpty() || !SecureNote::encrypt(content, m_notePassword, &envelope)) {
            QMessageBox::warning(this, QStringLiteral("evidence"), localized("Could not encrypt the note."));
            return;
        }
        content = envelope;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("evidence"), localized("Could not save the note."));
        return;
    }
    if (file.write(content) != content.size() || !file.commit()) {
        QMessageBox::warning(this, QStringLiteral("evidence"), localized("Could not save the note."));
        return;
    }
    m_editor->document()->setModified(false);
    setWindowTitle(QStringLiteral("evidence - ") + QFileInfo(m_currentNote).completeBaseName());
    saveMetadata();
    refreshVault();
    statusBar()->showMessage(localized("Saved %1").arg(m_currentNote), 2500);
}

void MainWindow::encryptCurrentNote()
{
    if (m_currentNote.isEmpty() || isEncryptedNote(m_currentNote))
        return;
    if (m_editor->document()->isModified()) {
        saveNote();
        if (m_editor->document()->isModified())
            return;
    }

    bool accepted = false;
    const QString password = QInputDialog::getText(this, localized("Encrypt note"), localized("Password"), QLineEdit::Password,
                                                    {}, &accepted);
    if (!accepted)
        return;
    if (password.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("evidence"), localized("The password cannot be empty."));
        return;
    }
    const QString confirmation = QInputDialog::getText(this, localized("Encrypt note"), localized("Confirm password"),
                                                        QLineEdit::Password, {}, &accepted);
    if (!accepted)
        return;
    if (password != confirmation) {
        QMessageBox::warning(this, QStringLiteral("evidence"), localized("The passwords do not match."));
        return;
    }

    const QByteArray plaintext = m_editor->toPlainText().toUtf8();
    QByteArray envelope;
    if (!SecureNote::encrypt(plaintext, password, &envelope)) {
        QMessageBox::warning(this, QStringLiteral("evidence"), localized("Could not encrypt the note."));
        return;
    }

    const QString previousPath = m_currentNote;
    const qsizetype suffix = previousPath.lastIndexOf(QLatin1Char('.'));
    if (suffix < 0)
        return;
    const QString encryptedPath = previousPath.left(suffix) + QStringLiteral(".evnote");
    const QString absoluteEncryptedPath = QDir(m_vaultPath).filePath(encryptedPath);
    if (QFileInfo::exists(absoluteEncryptedPath)) {
        QMessageBox::warning(this, QStringLiteral("evidence"), localized("An encrypted note with that name already exists."));
        return;
    }

    QSaveFile encryptedFile(absoluteEncryptedPath);
    if (!encryptedFile.open(QIODevice::WriteOnly) || encryptedFile.write(envelope) != envelope.size() ||
        !encryptedFile.commit()) {
        QMessageBox::warning(this, QStringLiteral("evidence"), localized("Could not write the encrypted note."));
        return;
    }
    if (!m_index.forgetNote(previousPath)) {
        QFile::remove(absoluteEncryptedPath);
        refreshVault();
        QMessageBox::warning(this, QStringLiteral("evidence"), localized("The search index could not be securely updated; encryption was cancelled."));
        return;
    }
    if (!QFile::remove(QDir(m_vaultPath).filePath(previousPath))) {
        QFile::remove(absoluteEncryptedPath);
        refreshVault();
        QMessageBox::warning(this, QStringLiteral("evidence"), localized("The original note could not be removed; encryption was cancelled."));
        return;
    }

    m_tags.insert(encryptedPath, m_tags.take(previousPath));
    m_currentNote = encryptedPath;
    m_notePassword = password;
    m_editor->document()->setModified(false);
    m_encryptionAction->setText(localized("Lock note"));
    setWindowTitle("evidence - " + QFileInfo(m_currentNote).completeBaseName());
    saveMetadata();
    refreshVault();
    statusBar()->showMessage(localized("Note encrypted"), 2500);
}

void MainWindow::lockCurrentNote()
{
    if (!isEncryptedNote(m_currentNote))
        return;
    if (m_editor->document()->isModified()) {
        saveNote();
        if (m_editor->document()->isModified())
            return;
    }

    m_links.remove(m_currentNote);
    m_notePassword.fill(QChar(0));
    m_notePassword.clear();
    m_currentNote.clear();
    m_editor->clear();
    m_tagsInput->clear();
    m_editor->document()->setModified(false);
    m_encryptionAction->setText(localized("Encrypt note"));
    m_encryptionAction->setEnabled(false);
    m_renameAction->setEnabled(false);
    m_deleteAction->setEnabled(false);
    m_linkAction->setEnabled(false);
    updateNoteHeader();
    setWindowTitle(QStringLiteral("evidence - ") + QFileInfo(m_vaultPath).fileName());
    m_graph->setGraph(m_titles.keys(), m_links, {});
    updateBacklinks();
    updatePreview();
}

void MainWindow::openNote(const QString &relativePath)
{
    if (relativePath.isEmpty() || relativePath == m_currentNote)
        return;
    if (m_editor->document()->isModified()) {
        const auto answer = QMessageBox::question(this, localized("Unsaved changes"),
                               localized("Save changes before opening another note?"),
                                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                                   QMessageBox::Save);
        if (answer == QMessageBox::Cancel)
            return;
        if (answer == QMessageBox::Save) {
            saveNote();
            if (m_editor->document()->isModified())
                return;
        }
    }

    QFile file(QDir(m_vaultPath).filePath(relativePath));
    if (!file.open(QIODevice::ReadOnly))
        return;
    QByteArray content = file.readAll();
    QString password;
    if (isEncryptedNote(relativePath)) {
        bool accepted = false;
        password = QInputDialog::getText(this, localized("Unlock note"), localized("Password"),
                         QLineEdit::Password, {}, &accepted);
        if (!accepted)
            return;
        QByteArray plaintext;
        if (!SecureNote::decrypt(content, password, &plaintext)) {
            QMessageBox::warning(this, QStringLiteral("evidence"), localized("The password is incorrect or the note is damaged."));
            return;
        }
        content = plaintext;
    }

    if (isEncryptedNote(m_currentNote))
        m_links.remove(m_currentNote);
    m_notePassword.fill(QChar(0));
    m_notePassword.clear();
    m_currentNote = relativePath;
    m_notePassword = password;
    m_editor->setPlainText(QString::fromUtf8(content));
    m_tagsInput->setText(m_tags.value(relativePath).join(QStringLiteral(", ")));
    updateNoteHeader();
    m_editor->document()->setModified(false);
    m_encryptionAction->setEnabled(true);
    m_renameAction->setEnabled(true);
    m_deleteAction->setEnabled(true);
    m_linkAction->setEnabled(true);
    m_encryptionAction->setText(localized(isEncryptedNote(relativePath) ? "Lock note" : "Encrypt note"));
    setWindowTitle("evidence - " + QFileInfo(relativePath).completeBaseName());
    updatePreview();
    for (int i = 0; i < m_noteList->count(); ++i) {
        if (m_noteList->item(i)->data(Qt::UserRole).toString() == relativePath) {
            m_noteList->setCurrentRow(i);
            break;
        }
    }
    updateCurrentLinks();
}

void MainWindow::updatePreview()
{
    if (!m_preview)
        return;
    QString markdown = m_editor->toPlainText();
    const QRegularExpression linkPattern("\\[\\[([^]|#]+)(?:\\|([^]]+))?\\]\\]");
    auto matches = linkPattern.globalMatch(markdown);
    QString rendered;
    qsizetype previous = 0;
    while (matches.hasNext()) {
        const auto match = matches.next();
        rendered += markdown.mid(previous, match.capturedStart() - previous);
        const QString target = match.captured(1).trimmed();
        const QString label = match.captured(2).isEmpty() ? target : match.captured(2).trimmed();
        const QString resolved = resolveWikiLink(target);
        if (!resolved.isEmpty()) {
            const QString url = QStringLiteral("evidence://note/%1")
                                    .arg(QString::fromLatin1(QUrl::toPercentEncoding(resolved)));
            rendered += QStringLiteral("[%1](%2)").arg(label, url);
        } else {
            rendered += label;
        }
        previous = match.capturedEnd();
    }
    rendered += markdown.mid(previous);
    QTextDocument document;
    document.setMarkdown(rendered);
    m_preview->setHtml(document.toHtml());
}

QString MainWindow::resolveWikiLink(const QString &target) const
{
    const QString folded = target.toCaseFolded();
    for (auto it = m_titles.cbegin(); it != m_titles.cend(); ++it) {
        if (it.value().toCaseFolded() == folded || it.key().toCaseFolded() == folded)
            return it.key();
    }
    QString relative = target;
    if (!relative.endsWith(".md", Qt::CaseInsensitive))
        relative += ".md";
    for (const QString &path : m_titles.keys()) {
        if (path.compare(relative, Qt::CaseInsensitive) == 0)
            return path;
    }
    const QString encryptedPath = relative.left(relative.lastIndexOf(QLatin1Char('.'))) + QStringLiteral(".evnote");
    for (const QString &path : m_titles.keys()) {
        if (path.compare(encryptedPath, Qt::CaseInsensitive) == 0)
            return path;
    }
    return {};
}

void MainWindow::updateSearchResults()
{
    if (!m_noteList || !m_searchInput)
        return;
    const QString query = m_searchInput->text().trimmed();
    const QStringList matches = query.isEmpty() ? QStringList() : m_index.search(query);
    const QString folder = m_folderFilter->currentData().toString();
    int visibleCount = 0;
    for (int i = 0; i < m_noteList->count(); ++i) {
        QListWidgetItem *item = m_noteList->item(i);
        const QString path = item->data(Qt::UserRole).toString();
        const bool attachment = item->data(Qt::UserRole + 1).toBool();
        const bool inFolder = !query.isEmpty() || folder.isEmpty() || attachment || QFileInfo(path).path() == folder;
        const bool visible = inFolder && (query.isEmpty() || matches.contains(path));
        item->setHidden(!visible);
        if (visible && !query.isEmpty())
            ++visibleCount;
    }
    if (!query.isEmpty())
        statusBar()->showMessage(localized("%1 search results").arg(visibleCount));
    else
        statusBar()->showMessage(localized("Ready"));
}

void MainWindow::scheduleRefresh()
{
    m_refreshTimer->start();
}

#include "main_window.moc"
