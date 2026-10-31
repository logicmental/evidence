using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Diagnostics;
using System.Security.Cryptography;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using Markdig;

namespace Evidence;

public partial class MainWindow : Window
{
    private readonly ObservableCollection<NoteItem> _notes = [];
    private readonly Dictionary<string, List<string>> _links = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, string> _titles = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, string> _unlockedFolders = new(StringComparer.OrdinalIgnoreCase);
    private string _vaultPath = string.Empty;
    private string _currentPath = string.Empty;
    private string _language = "en";
    private string? _notePassword;
    private VaultService _vault = null!;
    private FileSystemWatcher? _watcher;
    private readonly DispatcherTimer _refreshTimer = new() { Interval = TimeSpan.FromMilliseconds(350) };
    private bool _loadingNote;
    private bool _dirty;
    private bool _allowClose;

    public MainWindow()
    {
        InitializeComponent();
        NotesList.ItemsSource = _notes;

        var settings = LoadSettings();
        _language = ReadBootstrapLanguage() ??
            (settings.Language is "ru" or "en" ? settings.Language : DetectLanguage());
        if (_language is not ("ru" or "en"))
            _language = "en";
        var applicationVault = Path.Combine(AppContext.BaseDirectory, "Notes");
        var legacyVault = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "evidence");
        var hasCustomVault = !string.IsNullOrWhiteSpace(settings.VaultPath) &&
                             !PathsEqual(settings.VaultPath, legacyVault) &&
                             !PathsEqual(settings.VaultPath, applicationVault);
        _vaultPath = hasCustomVault ? settings.VaultPath : applicationVault;
        Directory.CreateDirectory(_vaultPath);
        if (!hasCustomVault)
            TryMigrateVault(legacyVault, _vaultPath);
        _vault = new VaultService(_vaultPath);

        LanguageSelector.SelectedIndex = _language == "ru" ? 1 : 0;
        ApplyLanguage();
        VaultPathLabel.Text = _vaultPath;
        VaultLabel.Text = Path.GetFileName(_vaultPath.TrimEnd(Path.DirectorySeparatorChar,
            Path.AltDirectorySeparatorChar));

        OpenNoteButton.Click += async (_, _) => await OpenSelectedNoteAsync();
        NotesList.DoubleTapped += async (_, _) => await OpenSelectedNoteAsync();
        SearchBox.TextChanged += (_, _) => RefreshNotes();
        FolderFilter.SelectionChanged += (_, _) =>
        {
            RefreshNotes();
            UpdateFolderProtectionAction();
        };
        Editor.TextChanged += OnEditorTextChanged;
        TagsInput.LostFocus += (_, _) => SaveCurrentTags();
        NewNoteButton.Click += async (_, _) => await CreateNoteAsync();
        NewFolderButton.Click += async (_, _) => await CreateFolderAsync();
        SaveButton.Click += async (_, _) => await SaveNoteAsync();
        OpenFolderButton.Click += async (_, _) => await OpenFolderAsync();
        TrashButton.Click += async (_, _) => await OpenTrashAsync();
        RenameButton.Click += async (_, _) => await RenameNoteAsync();
        DeleteButton.Click += async (_, _) => await MoveNoteToTrashAsync();
        AttachButton.Click += async (_, _) => await AttachFileAsync();
        TemplatesButton.Click += async (_, _) => await ManageTemplatesAsync();
        EncryptButton.Click += async (_, _) => await EncryptOrUnlockAsync();
        ProtectFolderButton.Click += async (_, _) => await ProtectFolderAsync();
        LinkButton.Click += async (_, _) => await InsertNoteLinkAsync();
        GraphButton.Click += async (_, _) => await ShowGraphAsync();
        ExportButton.Click += async (_, _) => await ExportAsync();
        BoldButton.Click += (_, _) => ApplyInlineFormat("**", "**");
        ItalicButton.Click += (_, _) => ApplyInlineFormat("*", "*");
        CodeButton.Click += (_, _) => ApplyInlineFormat("`", "`");
        LinkFormatButton.Click += (_, _) => ApplyInlineFormat("[", "](url)");
        Heading1Button.Click += (_, _) => ApplyLineFormat("# ");
        Heading2Button.Click += (_, _) => ApplyLineFormat("## ");
        ListButton.Click += (_, _) => ApplyLineFormat("- ");
        QuoteButton.Click += (_, _) => ApplyLineFormat("> ");
        UndoButton.Click += (_, _) => Editor.Undo();
        RedoButton.Click += (_, _) => Editor.Redo();
        BacklinksList.SelectionChanged += async (_, _) =>
        {
            if (BacklinksList.SelectedItem is NoteItem backlink && backlink.RelativePath != _currentPath)
                await LoadNoteAsync(backlink.RelativePath);
        };
        LanguageSelector.SelectionChanged += OnLanguageChanged;
        _refreshTimer.Tick += (_, _) => { _refreshTimer.Stop(); RefreshNotes(); };

        SetCurrentNote(null);
        RefreshFolderFilter();
        RefreshNotes();
        SetupWatcher();
        SaveSettings();
        TryRemoveBootstrapFile();
    }

    protected override async void OnClosing(WindowClosingEventArgs e)
    {
        if (!_allowClose && _dirty)
        {
            e.Cancel = true;
            if (await PrepareToLeaveNoteAsync())
            {
                _allowClose = true;
                Close();
            }
        }
        base.OnClosing(e);
    }

    private void ApplyLanguage()
    {
        Title = "evidence";
        VaultLabel.Text = Path.GetFileName(_vaultPath.TrimEnd(Path.DirectorySeparatorChar,
            Path.AltDirectorySeparatorChar));
        SidebarEyebrow.Text = T("YOUR NOTES");
        SearchBox.PlaceholderText = T("Search notes");
        NotesHeading.Text = T("Notes");
        NewNoteButton.Content = T("New note");
        SaveButton.Content = T("Save");
        OpenFolderButton.Content = T("Open folder");
        TrashButton.Content = T("Trash");
        EditorHeading.Text = T("EDITOR");
        PreviewHeading.Text = T("PREVIEW");
        Editor.PlaceholderText = T("Start writing in Markdown...");
        RenameButton.Content = T("Rename");
        DeleteButton.Content = T("Move to trash");
        DirtyIndicator.Text = T("Unsaved");
        FooterStatus.Text = T("Markdown · UTF-8");
        NewFolderButton.Content = T("New category");
        OpenNoteButton.Content = T("Open note");
        AttachButton.Content = T("Attach file");
        TemplatesButton.Content = T("Templates");
        EncryptButton.Content = T(IsEncrypted(_currentPath)
            ? (_notePassword is null ? "Unlock note" : "Lock note") : "Encrypt note");
        UpdateFolderProtectionAction();
        LinkButton.Content = T("Link note");
        GraphButton.Content = T("Graph");
        ExportButton.Content = T("Export");
        BacklinksHeading.Text = T("BACKLINKS");
        FolderFilter.PlaceholderText = T("All folders");
        RefreshFolderFilter();
        StatusLabel.Text = T("Ready");
        if (_currentPath.Length == 0)
            NoteTitle.Text = T("Select a note");
    }

    private string T(string text)
    {
        if (_language != "ru")
            return text;

        return text switch
        {
            "YOUR NOTES" => "ВАШИ ЗАМЕТКИ",
            "Search notes" => "Поиск заметок",
            "Notes" => "Заметки",
            "New note" => "Новая заметка",
            "Save" => "Сохранить",
            "Open folder" => "Открыть папку",
            "Trash" => "Корзина",
            "New folder" => "Новая папка",
            "New category" => "Новая категория",
            "Open note" => "Открыть заметку",
            "Attach file" => "Прикрепить файл",
            "Templates" => "Шаблоны",
            "Encrypt note" => "Зашифровать заметку",
                        "Protect folder" => "Защитить папку",
                        "Unlock folder" => "Разблокировать папку",
                        "Lock folder" => "Заблокировать папку",
                        "Unlock the folder before creating a note." => "Разблокируйте папку перед созданием заметки.",
                        "The password is incorrect." => "Неверный пароль.",
                        "A note in this folder uses a different password." => "Одна из заметок в папке защищена другим паролем.",
                        "Folder protected." => "Папка защищена.",
                        "Could not protect the folder." => "Не удалось защитить папку.",
            "Unlock note" => "Разблокировать заметку",
            "Lock note" => "Заблокировать заметку",
            "Link note" => "Связать заметку",
            "Graph" => "Граф",
            "Editor" => "Редактор",
            "Export" => "Экспорт",
            "BACKLINKS" => "ОБРАТНЫЕ ССЫЛКИ",
            "All folders" => "Все папки",
            "Tags, separated by commas" => "Теги через запятую",
            "No backlinks" => "Нет обратных ссылок",
            "Choose a note to link" => "Выберите заметку для связи",
            "Folder name" => "Название папки",
            "Enter a valid template name." => "Введите допустимое название шаблона.",
            "Password" => "Пароль",
            "Confirm password" => "Повторите пароль",
            "Passwords do not match." => "Пароли не совпадают.",
            "The password cannot be empty." => "Пароль не может быть пустым.",
            "The password is incorrect or the note is damaged." => "Неверный пароль или файл заметки повреждён.",
            "No linked notes yet" => "Связанных заметок пока нет",
            "Current note" => "Текущая заметка",
            "Selected notes" => "Выбранные заметки",
            "Current folder" => "Текущая папка",
            "Entire vault" => "Всё хранилище",
            "Choose what to export" => "Что экспортировать",
            "Export notes" => "Экспорт заметок",
            "Choose export format" => "Выберите формат экспорта",
            "No notes selected for export." => "Нет заметок для экспорта.",
            "Could not export the selected notes." => "Не удалось экспортировать выбранные заметки.",
            "Saved %1" => "Сохранено: %1",
            "%1 notes · %2 files · %3" => "%1 заметок · %2 файлов · %3",
            "EDITOR" => "РЕДАКТОР",
            "PREVIEW" => "ПРЕДПРОСМОТР",
            "Start writing in Markdown..." => "Начните писать в Markdown...",
            "Rename" => "Переименовать",
            "Move to trash" => "В корзину",
            "Markdown · UTF-8" => "Markdown · UTF-8",
            "Ready" => "Готово",
            "Select a note" => "Выберите заметку",
            "No note open" => "Заметка не выбрана",
            "Unsaved" => "Не сохранено",
            "Unsaved changes" => "Несохранённые изменения",
            "Save changes?" => "Сохранить изменения?",
            "Discard" => "Не сохранять",
            "Cancel" => "Отмена",
            "Note name" => "Название заметки",
            "Rename note" => "Переименовать заметку",
            "Delete note?" => "Удалить заметку?",
            "Move this note to the trash?" => "Переместить эту заметку в корзину?",
            "Could not save the note." => "Не удалось сохранить заметку.",
            "Could not rename the note." => "Не удалось переименовать заметку.",
            "A note with that name already exists." => "Заметка с таким названием уже существует.",
            "Could not move the note to the trash." => "Не удалось переместить заметку в корзину.",
            "Could not delete the note permanently." => "Не удалось удалить заметку навсегда.",
            "The folder could not be opened." => "Не удалось открыть заметку.",
            "The note name is invalid." => "Недопустимое название заметки.",
            "No notes in trash" => "Корзина пуста",
            "Restore" => "Восстановить",
            "Delete permanently" => "Удалить навсегда",
            "Close" => "Закрыть",
            "Restore this note?" => "Восстановить эту заметку?",
            "Delete this note permanently?" => "Удалить эту заметку навсегда?",
            "A note already exists at that location." => "В этом месте уже есть заметка.",
            "Could not restore the note." => "Не удалось восстановить заметку.",
            "Open notes folder" => "Открыть папку заметок",
            "Continue" => "Продолжить",
            _ => text
        };
    }

    private void OnLanguageChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (LanguageSelector.SelectedItem is not ComboBoxItem item || item.Tag is not string language)
            return;
        _language = language;
        ApplyLanguage();
        SaveSettings();
    }

    private void OnEditorTextChanged(object? sender, TextChangedEventArgs e)
    {
        if (_loadingNote || _currentPath.Length == 0)
            return;
        _dirty = true;
        DirtyIndicator.IsVisible = true;
        StatusLabel.Text = T("Unsaved changes");
        UpdatePreview();
        UpdateCurrentLinks();
    }

    private async Task OpenSelectedNoteAsync()
    {
        if (NotesList.SelectedItems?.OfType<NoteItem>().FirstOrDefault() is not NoteItem note ||
            note.RelativePath == _currentPath)
            return;
        if (!await PrepareToLeaveNoteAsync())
        {
            return;
        }
        await LoadNoteAsync(note.RelativePath);
    }

    private async Task<bool> PrepareToLeaveNoteAsync()
    {
        if (!_dirty)
            return true;
        var choice = await ShowUnsavedDialogAsync();
        if (choice == UnsavedChoice.Cancel)
            return false;
        return choice == UnsavedChoice.Discard || await SaveNoteAsync();
    }

    private void RefreshNotes()
    {
        var selectedPath = _currentPath;
        var folder = FolderFilter.SelectedItem?.ToString() ?? string.Empty;
        if (folder == T("All folders"))
            folder = string.Empty;
        var found = _vault.GetItems(SearchBox.Text?.Trim() ?? string.Empty, folder,
            IsEncrypted(_currentPath) && _notePassword is not null ? _currentPath : string.Empty,
            IsEncrypted(_currentPath) && _notePassword is not null ? Editor.Text ?? string.Empty : string.Empty);
        _notes.Clear();
        _titles.Clear();
        foreach (var note in found.Where(note => note.IsAttachment ||
                     _vault.FindProtectedFolder(note.RelativePath) is not { } protectedFolder ||
                     _unlockedFolders.ContainsKey(protectedFolder)))
        {
            _notes.Add(note);
            if (!note.IsAttachment)
                _titles[note.RelativePath] = note.Title;
        }
        NoteCount.Text = _notes.Count.ToString();
        NotesList.SelectedItem = _notes.FirstOrDefault(note => note.RelativePath == selectedPath);
        RebuildLinks();
        UpdateBacklinks();
        UpdateFolderProtectionAction();
    }

    private string SelectedFolderPath()
    {
        var folder = FolderFilter.SelectedItem?.ToString() ?? string.Empty;
        return folder == T("All folders") ? string.Empty : folder;
    }

    private static bool IsInsideFolder(string relativePath, string folder)
    {
        var path = relativePath.Replace('\\', '/');
        var normalizedFolder = folder.Replace('\\', '/').TrimEnd('/');
        return path.StartsWith(normalizedFolder + "/", StringComparison.OrdinalIgnoreCase);
    }

    private void UpdateFolderProtectionAction()
    {
        var folder = SelectedFolderPath();
        var protectedFolder = folder.Length == 0 ? null : _vault.FindProtectedFolder(folder);
        ProtectFolderButton.IsEnabled = folder.Length > 0;
        ProtectFolderButton.Content = T(protectedFolder is null
            ? "Protect folder"
            : _unlockedFolders.ContainsKey(protectedFolder) ? "Lock folder" : "Unlock folder");
    }

    private void RefreshFolderFilter()
    {
        var previous = FolderFilter.SelectedItem?.ToString();
        var options = _vault.GetFolders().Select(folder =>
            folder.Length == 0 ? T("All folders") : folder).ToArray();
        FolderFilter.ItemsSource = options;
        FolderFilter.SelectedItem = options.Contains(previous) ? previous : options.FirstOrDefault();
    }

    private void SetupWatcher()
    {
        _watcher?.Dispose();
        _watcher = new FileSystemWatcher(_vaultPath)
        {
            IncludeSubdirectories = true,
            NotifyFilter = NotifyFilters.FileName | NotifyFilters.DirectoryName | NotifyFilters.LastWrite,
            EnableRaisingEvents = true
        };
        FileSystemEventHandler changed = (_, _) => Dispatcher.UIThread.Post(ScheduleRefresh);
        RenamedEventHandler renamed = (_, _) => Dispatcher.UIThread.Post(ScheduleRefresh);
        _watcher.Changed += changed;
        _watcher.Created += changed;
        _watcher.Deleted += changed;
        _watcher.Renamed += renamed;
    }

    private void ScheduleRefresh()
    {
        _refreshTimer.Stop();
        _refreshTimer.Start();
    }

    private bool IsEncrypted(string path) =>
        Path.GetExtension(path).Equals(".evnote", StringComparison.OrdinalIgnoreCase);

    private void UpdateEncryptionAction()
    {
        var source = IsEncrypted(_currentPath)
            ? (_notePassword is null ? "Unlock note" : "Lock note")
            : "Encrypt note";
        EncryptButton.Content = T(source);
        EncryptButton.IsEnabled = _currentPath.Length > 0;
    }

    private void UpdateCurrentLinks()
    {
        if (_currentPath.Length == 0 || IsEncrypted(_currentPath) && _notePassword is null)
        {
            UpdateBacklinks();
            return;
        }
        var outgoing = WikiLinkPattern().Matches(Editor.Text ?? string.Empty)
            .Select(match => ResolveWikiLink(match.Groups[1].Value.Trim()))
            .Where(path => path is not null).Cast<string>()
            .Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        _links[_currentPath] = outgoing;
        UpdateBacklinks();
    }

    private void RebuildLinks()
    {
        _links.Clear();
        foreach (var note in _notes.Where(item => !item.IsAttachment && !item.IsEncrypted))
        {
            var text = ReadTextFile(_vault.FullPath(note.RelativePath));
            _links[note.RelativePath] = WikiLinkPattern().Matches(text)
                .Select(match => ResolveWikiLink(match.Groups[1].Value.Trim()))
                .Where(path => path is not null).Cast<string>()
                .Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        }
        if (_currentPath.Length > 0 && IsEncrypted(_currentPath) && _notePassword is not null)
            UpdateCurrentLinks();
    }

    private void UpdateBacklinks()
    {
        var incoming = _links.Where(pair => pair.Value.Contains(_currentPath, StringComparer.OrdinalIgnoreCase))
            .Select(pair => new NoteItem(pair.Key, _titles.GetValueOrDefault(pair.Key, pair.Key)))
            .ToList();
        BacklinksList.ItemsSource = incoming;
    }

    private string? ResolveWikiLink(string target)
    {
        var match = _titles.FirstOrDefault(pair =>
            pair.Value.Equals(target, StringComparison.OrdinalIgnoreCase) ||
            pair.Key.Equals(target, StringComparison.OrdinalIgnoreCase));
        if (!string.IsNullOrEmpty(match.Key))
            return match.Key;
        var normalized = target.EndsWith(".md", StringComparison.OrdinalIgnoreCase) ? target : target + ".md";
        return _titles.Keys.FirstOrDefault(path => path.Equals(normalized,
            StringComparison.OrdinalIgnoreCase));
    }

    private static Regex WikiLinkPattern() => new("\\[\\[([^]|#]+)(?:\\|([^]]+))?\\]\\]",
        RegexOptions.Compiled | RegexOptions.CultureInvariant);

    private static string ReadTextFile(string path)
    {
        try { return File.ReadAllText(path, Encoding.UTF8); }
        catch (IOException) { return string.Empty; }
    }

    private async Task LoadNoteAsync(string relativePath)
    {
        if (_notes.FirstOrDefault(note => note.RelativePath == relativePath) is { IsAttachment: true })
        {
            try
            {
                Process.Start(new ProcessStartInfo(_vault.FullPath(relativePath)) { UseShellExecute = true });
            }
            catch (Exception exception) when (exception is IOException or InvalidOperationException)
            {
                StatusLabel.Text = T("The folder could not be opened.");
            }
            return;
        }
        try
        {
            var fullPath = _vault.FullPath(relativePath);
            var bytes = await File.ReadAllBytesAsync(fullPath);
            string? password = null;
            if (IsEncrypted(relativePath))
            {
                var protectedFolder = _vault.FindProtectedFolder(relativePath);
                var folderPassword = protectedFolder is not null &&
                                     _unlockedFolders.TryGetValue(protectedFolder, out var savedPassword)
                    ? savedPassword : null;
                password = _notePassword ?? folderPassword ??
                           await ShowPasswordDialogAsync(T("Unlock note"), T("Password"));
                if (password is null)
                    return;
                if (!SecureNote.TryDecrypt(bytes, password, out bytes))
                {
                    StatusLabel.Text = T("The password is incorrect or the note is damaged.");
                    return;
                }
            }
            var contents = Encoding.UTF8.GetString(bytes);
            _loadingNote = true;
            _currentPath = relativePath;
            _notePassword = password;
            Editor.Text = contents;
            NoteTitle.Text = Path.GetFileNameWithoutExtension(fullPath);
            NoteSubtitle.Text = relativePath.Replace(Path.DirectorySeparatorChar, '/');
            TagsInput.Text = string.Join(", ", _vault.Tags.GetValueOrDefault(relativePath) ?? []);
            PreviewText.Markdown = RenderPreview(contents);
            _dirty = false;
            DirtyIndicator.IsVisible = false;
            SetNoteActionsEnabled(true);
            UpdateEncryptionAction();
            UpdateCurrentLinks();
            StatusLabel.Text = T("Ready");
        }
        catch (IOException)
        {
            StatusLabel.Text = T("The folder could not be opened.");
        }
        finally
        {
            _loadingNote = false;
        }
    }

    private void SetCurrentNote(string? relativePath)
    {
        _loadingNote = true;
        _currentPath = relativePath ?? string.Empty;
        _notePassword = null;
        Editor.Text = string.Empty;
        PreviewText.Markdown = T("No note open");
        NoteTitle.Text = T("Select a note");
        NoteSubtitle.Text = string.Empty;
        _dirty = false;
        DirtyIndicator.IsVisible = false;
        SetNoteActionsEnabled(relativePath is not null);
        UpdateEncryptionAction();
        _loadingNote = false;
    }

    private void SetNoteActionsEnabled(bool enabled)
    {
        Editor.IsEnabled = enabled;
        TagsInput.IsEnabled = enabled;
        SaveButton.IsEnabled = enabled;
        RenameButton.IsEnabled = enabled;
        DeleteButton.IsEnabled = enabled;
    }

    private async Task CreateNoteAsync()
    {
        if (!await PrepareToLeaveNoteAsync())
            return;
        var templates = _vault.GetTemplateNames();
        string? selectedTemplate = null;
        if (templates.Count > 0)
        {
            var choices = new[] { T("Blank") }.Concat(templates).ToArray();
            var choice = await ShowChoiceDialogAsync(T("New note"), T("Template"), choices);
            if (choice is null)
                return;
            if (choice != T("Blank"))
                selectedTemplate = choice;
        }
        var folder = SelectedFolderPath();
        var protectedFolder = folder.Length == 0 ? null : _vault.FindProtectedFolder(folder);
        if (protectedFolder is not null && !_unlockedFolders.ContainsKey(protectedFolder))
        {
            StatusLabel.Text = T("Unlock the folder before creating a note.");
            return;
        }
        var parent = folder.Length == 0 ? _vault.Root : _vault.FullPath(folder);
        var extension = protectedFolder is null ? ".md" : ".evnote";
        Directory.CreateDirectory(parent);
        var number = 1;
        string title;
        string path;
        do
        {
            title = number == 1 ? "Untitled" : $"Untitled {number}";
            path = Path.Combine(parent, title + extension);
            number++;
        } while (File.Exists(path));

        try
        {
            var content = selectedTemplate is null ? string.Empty : _vault.ReadTemplate(selectedTemplate)
                .Replace("{{title}}", title, StringComparison.Ordinal)
                .Replace("{{date}}", DateTime.Now.ToString("yyyy-MM-dd"), StringComparison.Ordinal);
            var bytes = new UTF8Encoding(false).GetBytes(content);
            if (protectedFolder is not null)
                bytes = SecureNote.Encrypt(bytes, _unlockedFolders[protectedFolder]);
            VaultService.WriteAtomic(path, bytes);
            var relative = Path.GetRelativePath(_vaultPath, path);
            RefreshNotes();
            await LoadNoteAsync(relative);
            NotesList.SelectedItem = _notes.FirstOrDefault(note => note.RelativePath == relative);
            Editor.Focus();
        }
        catch (IOException)
        {
            StatusLabel.Text = T("Could not save the note.");
        }
    }

    private async Task<bool> SaveNoteAsync()
    {
        if (_currentPath.Length == 0)
            return false;
        try
        {
            var path = _vault.FullPath(_currentPath);
            var content = Encoding.UTF8.GetBytes(Editor.Text ?? string.Empty);
            if (IsEncrypted(_currentPath))
            {
                if (_notePassword is null)
                    return false;
                content = SecureNote.Encrypt(content, _notePassword);
            }
            VaultService.WriteAtomic(path, content);
            _vault.SetTags(_currentPath, (TagsInput.Text ?? string.Empty).Split(','));
            UpdateCurrentLinks();
            _dirty = false;
            DirtyIndicator.IsVisible = false;
            StatusLabel.Text = T("Ready");
            PreviewText.Markdown = RenderPreview(Editor.Text ?? string.Empty);
            RefreshNotes();
            return true;
        }
        catch (IOException)
        {
            StatusLabel.Text = T("Could not save the note.");
            return false;
        }
    }

    private async Task RenameNoteAsync()
    {
        if (_currentPath.Length == 0 || !await PrepareToLeaveNoteAsync())
            return;
        var currentFullPath = Path.Combine(_vaultPath, _currentPath);
        var oldTitle = Path.GetFileNameWithoutExtension(currentFullPath);
        var newTitle = await ShowTextDialogAsync(T("Rename note"), T("Note name"), oldTitle);
        if (string.IsNullOrWhiteSpace(newTitle) || newTitle == oldTitle)
            return;
        if (newTitle.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
            newTitle.IndexOfAny("<>:\"/\\|?*".ToCharArray()) >= 0 || newTitle is "." or "..")
        {
            StatusLabel.Text = T("The note name is invalid.");
            return;
        }

        var extension = Path.GetExtension(currentFullPath);
        var destination = Path.Combine(Path.GetDirectoryName(currentFullPath)!, newTitle + extension);
        if (File.Exists(destination))
        {
            StatusLabel.Text = T("A note with that name already exists.");
            return;
        }
        try
        {
            File.Move(currentFullPath, destination);
            _currentPath = Path.GetRelativePath(_vaultPath, destination);
            _vault.MoveTags(Path.GetRelativePath(_vaultPath, currentFullPath), _currentPath);
            RefreshNotes();
            await LoadNoteAsync(_currentPath);
            NotesList.SelectedItem = _notes.FirstOrDefault(note => note.RelativePath == _currentPath);
        }
        catch (IOException)
        {
            StatusLabel.Text = T("Could not rename the note.");
        }
    }

    private async Task MoveNoteToTrashAsync()
    {
        if (_currentPath.Length == 0 ||
            !await ShowConfirmationAsync(this, T("Delete note?"), T("Move this note to the trash?")))
            return;
        if (_dirty && !await SaveNoteAsync())
            return;

        var source = Path.Combine(_vaultPath, _currentPath);
        var trashRoot = Path.Combine(_vaultPath, ".evidence", "trash");
        var entryName = $"{DateTime.UtcNow:yyyyMMdd'T'HHmmssfff}-{Guid.NewGuid():N}";
        var entryDirectory = Path.Combine(trashRoot, entryName);
        try
        {
            Directory.CreateDirectory(entryDirectory);
            var storedName = Path.GetFileName(source);
            var tags = _vault.Tags.GetValueOrDefault(_currentPath) ?? [];
            await File.WriteAllTextAsync(Path.Combine(entryDirectory, "manifest.json"),
                JsonSerializer.Serialize(new TrashManifest(_currentPath, storedName, tags)), new UTF8Encoding(false));
            File.Move(source, Path.Combine(entryDirectory, storedName));
            _vault.RemoveTags(_currentPath);
            SetCurrentNote(null);
            RefreshNotes();
        }
        catch (IOException)
        {
            if (Directory.Exists(entryDirectory) &&
                !File.Exists(Path.Combine(entryDirectory, Path.GetFileName(source))))
                Directory.Delete(entryDirectory, true);
            StatusLabel.Text = T("Could not move the note to the trash.");
        }
    }

    private async Task OpenFolderAsync()
    {
        if (!await PrepareToLeaveNoteAsync())
            return;
        var selection = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = T("Open notes folder"),
            AllowMultiple = false
        });
        if (selection.Count == 0)
            return;
        _vaultPath = selection[0].Path.LocalPath;
        Directory.CreateDirectory(_vaultPath);
        _vault = new VaultService(_vaultPath);
        VaultPathLabel.Text = _vaultPath;
        VaultLabel.Text = Path.GetFileName(_vaultPath.TrimEnd(Path.DirectorySeparatorChar,
            Path.AltDirectorySeparatorChar));
        SetCurrentNote(null);
        RefreshFolderFilter();
        RefreshNotes();
        SetupWatcher();
        SaveSettings();
    }

    private async Task OpenTrashAsync()
    {
        var root = Path.Combine(_vaultPath, ".evidence", "trash");
        Directory.CreateDirectory(root);
        var entries = ReadTrashEntries(root);
        var list = new ListBox { ItemsSource = entries, MinHeight = 180 };
        var empty = new TextBlock { Text = T("No notes in trash"), IsVisible = entries.Count == 0,
            Foreground = Avalonia.Media.Brushes.Gray };
        var restore = new Button { Content = T("Restore"), IsEnabled = false };
        var delete = new Button { Content = T("Delete permanently"), IsEnabled = false };
        delete.Classes.Add("danger");
        var close = new Button { Content = T("Close") };
        var dialog = new Window
        {
            Title = T("Trash"), Width = 560, Height = 410, MinWidth = 460, MinHeight = 320,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
            Content = new StackPanel
            {
                Margin = new Avalonia.Thickness(20), Spacing = 12,
                Children =
                {
                    new TextBlock { Text = T("Trash"), FontSize = 21,
                        FontWeight = Avalonia.Media.FontWeight.SemiBold },
                    list, empty,
                    new StackPanel { Orientation = Avalonia.Layout.Orientation.Horizontal, Spacing = 8,
                        HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Right,
                        Children = { restore, delete, close } }
                }
            }
        };
        list.SelectionChanged += (_, _) =>
        {
            var selected = list.SelectedItem is TrashEntry;
            restore.IsEnabled = selected;
            delete.IsEnabled = selected;
        };
        restore.Click += async (_, _) =>
        {
            if (list.SelectedItem is not TrashEntry entry ||
                !await ShowConfirmationAsync(dialog, T("Restore"), T("Restore this note?")))
                return;
            var destination = ResolveTrashDestination(entry.Manifest.OriginalPath);
            if (destination is null)
                return;
            try
            {
                if (File.Exists(destination))
                {
                    StatusLabel.Text = T("A note already exists at that location.");
                    return;
                }
                Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                File.Move(Path.Combine(entry.Directory, entry.Manifest.StoredName), destination);
                _vault.RestoreTags(entry.Manifest.OriginalPath, entry.Manifest.Tags ?? []);
                Directory.Delete(entry.Directory, true);
                dialog.Close();
                RefreshNotes();
            }
            catch (IOException)
            {
                StatusLabel.Text = T("Could not restore the note.");
            }
        };
        delete.Click += async (_, _) =>
        {
            if (list.SelectedItem is not TrashEntry entry ||
                !await ShowConfirmationAsync(dialog, T("Delete permanently"), T("Delete this note permanently?")))
                return;
            try
            {
                Directory.Delete(entry.Directory, true);
                entries.Remove(entry);
                empty.IsVisible = entries.Count == 0;
                restore.IsEnabled = list.SelectedItem is TrashEntry;
                delete.IsEnabled = list.SelectedItem is TrashEntry;
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
                StatusLabel.Text = T("Could not delete the note permanently.");
            }
        };
        close.Click += (_, _) => dialog.Close();
        await dialog.ShowDialog(this);
    }

    private string? ResolveTrashDestination(string relativePath)
    {
        var normalized = relativePath.Replace('\\', '/');
        if (Path.IsPathRooted(relativePath) || normalized.Split('/').Any(part => part is ".." or ".evidence" or "attachments"))
            return null;
        var destination = Path.GetFullPath(Path.Combine(_vaultPath, relativePath));
        var root = Path.GetFullPath(_vaultPath).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        return destination.StartsWith(root, OperatingSystem.IsWindows()
            ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal) ? destination : null;
    }

    private static ObservableCollection<TrashEntry> ReadTrashEntries(string trashRoot)
    {
        var entries = new ObservableCollection<TrashEntry>();
        foreach (var directory in Directory.EnumerateDirectories(trashRoot).OrderByDescending(path => path))
        {
            var manifestPath = Path.Combine(directory, "manifest.json");
            if (!File.Exists(manifestPath))
                continue;
            try
            {
                var manifest = JsonSerializer.Deserialize<TrashManifest>(File.ReadAllText(manifestPath),
                    new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
                if (manifest is not null && Path.GetFileName(manifest.StoredName) == manifest.StoredName &&
                    File.Exists(Path.Combine(directory, manifest.StoredName)))
                    entries.Add(new TrashEntry(directory, manifest));
            }
            catch (JsonException)
            {
            }
            catch (IOException)
            {
            }
            catch (UnauthorizedAccessException)
            {
            }
        }
        return entries;
    }

    private async Task<UnsavedChoice> ShowUnsavedDialogAsync()
    {
        var completion = new TaskCompletionSource<UnsavedChoice>();
        var dialog = new Window
        {
            Title = T("Unsaved changes"), Width = 440, Height = 210, CanResize = false,
            WindowStartupLocation = WindowStartupLocation.CenterOwner
        };
        var save = MakeDialogButton(T("Save"), () => CloseWith(UnsavedChoice.Save), "primary");
        var discard = MakeDialogButton(T("Discard"), () => CloseWith(UnsavedChoice.Discard));
        var cancel = MakeDialogButton(T("Cancel"), () => CloseWith(UnsavedChoice.Cancel));
        dialog.Content = new StackPanel
        {
            Margin = new Avalonia.Thickness(22), Spacing = 18,
            Children =
            {
                new TextBlock { Text = T("Save changes?"), FontSize = 20,
                    FontWeight = Avalonia.Media.FontWeight.SemiBold },
                new StackPanel { Orientation = Avalonia.Layout.Orientation.Horizontal, Spacing = 8,
                    HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Right,
                    Children = { save, discard, cancel } }
            }
        };
        dialog.Closed += (_, _) => completion.TrySetResult(UnsavedChoice.Cancel);
        void CloseWith(UnsavedChoice choice)
        {
            completion.TrySetResult(choice);
            dialog.Close();
        }
        await dialog.ShowDialog(this);
        return await completion.Task;
    }

    private async Task<string?> ShowTextDialogAsync(string title, string prompt, string initialValue)
    {
        var completion = new TaskCompletionSource<string?>();
        var input = new TextBox { Text = initialValue };
        var dialog = new Window
        {
            Title = title, Width = 440, Height = 220, CanResize = false,
            WindowStartupLocation = WindowStartupLocation.CenterOwner
        };
        var cancel = MakeDialogButton(T("Cancel"), () => CloseWith(null));
        var save = MakeDialogButton(T("Save"), () => CloseWith(input.Text?.Trim()), "primary");
        dialog.Content = new StackPanel
        {
            Margin = new Avalonia.Thickness(22), Spacing = 14,
            Children =
            {
                new TextBlock { Text = prompt, FontSize = 14 }, input,
                new StackPanel { Orientation = Avalonia.Layout.Orientation.Horizontal, Spacing = 8,
                    HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Right,
                    Children = { cancel, save } }
            }
        };
        dialog.Closed += (_, _) => completion.TrySetResult(null);
        void CloseWith(string? value)
        {
            completion.TrySetResult(value);
            dialog.Close();
        }
        await dialog.ShowDialog(this);
        return await completion.Task;
    }

    private static async Task<bool> ShowConfirmationAsync(Window owner, string title, string message)
    {
        var completion = new TaskCompletionSource<bool>();
        var dialog = new Window
        {
            Title = title, Width = 430, Height = 210, CanResize = false,
            WindowStartupLocation = WindowStartupLocation.CenterOwner
        };
        var cancel = MakeDialogButton(owner is MainWindow main ? main.T("Cancel") : "Cancel",
            () => CloseWith(false));
        var confirm = MakeDialogButton(owner is MainWindow current ? current.T("Continue") : "Continue",
            () => CloseWith(true), "primary");
        dialog.Content = new StackPanel
        {
            Margin = new Avalonia.Thickness(22), Spacing = 16,
            Children =
            {
                new TextBlock { Text = message, TextWrapping = Avalonia.Media.TextWrapping.Wrap, FontSize = 15 },
                new StackPanel { Orientation = Avalonia.Layout.Orientation.Horizontal, Spacing = 8,
                    HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Right,
                    Children = { cancel, confirm } }
            }
        };
        dialog.Closed += (_, _) => completion.TrySetResult(false);
        void CloseWith(bool result)
        {
            completion.TrySetResult(result);
            dialog.Close();
        }
        await dialog.ShowDialog(owner);
        return await completion.Task;
    }

    private static Button MakeDialogButton(string label, Action click, string? styleClass = null)
    {
        var button = new Button { Content = label };
        if (!string.IsNullOrWhiteSpace(styleClass))
            button.Classes.Add(styleClass);
        button.Click += (_, _) => click();
        return button;
    }

    private void SaveCurrentTags()
    {
        if (_currentPath.Length == 0 || IsEncrypted(_currentPath) && _notePassword is null)
            return;
        _vault.SetTags(_currentPath, (TagsInput.Text ?? string.Empty).Split(','));
        RefreshNotes();
    }

    private async Task CreateFolderAsync()
    {
        var requested = await ShowTextDialogAsync(T("New folder"), T("Folder name"), string.Empty);
        if (string.IsNullOrWhiteSpace(requested))
            return;
        try
        {
            var parent = SelectedFolderPath();
            var folder = _vault.CreateFolder(parent.Length == 0 ? requested : $"{parent}/{requested}");
            RefreshFolderFilter();
            FolderFilter.SelectedItem = folder;
            RefreshNotes();
        }
        catch (ArgumentException)
        {
            StatusLabel.Text = T("The note name is invalid.");
        }
        catch (IOException)
        {
            StatusLabel.Text = T("Could not create the folder.");
        }
    }

    private async Task ProtectFolderAsync()
    {
        var folder = SelectedFolderPath();
        if (folder.Length == 0)
            return;

        var protectedFolder = _vault.FindProtectedFolder(folder);
        if (protectedFolder is not null)
        {
            if (_unlockedFolders.ContainsKey(protectedFolder))
            {
                if (IsInsideFolder(_currentPath, protectedFolder) &&
                    !await PrepareToLeaveNoteAsync())
                    return;
                if (IsInsideFolder(_currentPath, protectedFolder))
                    SetCurrentNote(null);
                _unlockedFolders.Remove(protectedFolder);
                RefreshNotes();
                return;
            }

            var unlockPassword = await ShowPasswordDialogAsync(T("Unlock folder"), T("Password"));
            if (unlockPassword is null)
                return;
            if (!_vault.VerifyFolderPassword(protectedFolder, unlockPassword))
            {
                StatusLabel.Text = T("The password is incorrect.");
                return;
            }
            _unlockedFolders[protectedFolder] = unlockPassword;
            RefreshNotes();
            return;
        }

        if (!await PrepareToLeaveNoteAsync())
            return;
        var password = await ShowPasswordDialogAsync(T("Protect folder"), T("Password"));
        if (string.IsNullOrEmpty(password))
        {
            if (password is not null)
                StatusLabel.Text = T("The password cannot be empty.");
            return;
        }
        var confirmation = await ShowPasswordDialogAsync(T("Protect folder"), T("Confirm password"));
        if (password != confirmation)
        {
            StatusLabel.Text = T("Passwords do not match.");
            return;
        }

        try
        {
            var absoluteFolder = _vault.FullPath(folder);
            var noteFiles = Directory.EnumerateFiles(absoluteFolder, "*", SearchOption.AllDirectories)
                .Where(path => Path.GetExtension(path) is var extension &&
                    (extension.Equals(".md", StringComparison.OrdinalIgnoreCase) ||
                     extension.Equals(".markdown", StringComparison.OrdinalIgnoreCase) ||
                     extension.Equals(".evnote", StringComparison.OrdinalIgnoreCase)))
                .ToArray();
            var pending = new List<(string Source, string Destination, byte[] Contents)>();
            foreach (var source in noteFiles)
            {
                var bytes = await File.ReadAllBytesAsync(source);
                if (Path.GetExtension(source).Equals(".evnote", StringComparison.OrdinalIgnoreCase))
                {
                    if (!SecureNote.TryDecrypt(bytes, password, out _))
                    {
                        StatusLabel.Text = T("A note in this folder uses a different password.");
                        return;
                    }
                    continue;
                }
                var destination = Path.ChangeExtension(source, ".evnote");
                if (File.Exists(destination))
                {
                    StatusLabel.Text = T("A note with that name already exists.");
                    return;
                }
                pending.Add((source, destination, bytes));
            }

            foreach (var (source, destination, contents) in pending)
            {
                VaultService.WriteAtomic(destination, SecureNote.Encrypt(contents, password));
                var oldRelative = _vault.Relative(source);
                var newRelative = _vault.Relative(destination);
                File.Delete(source);
                _vault.MoveTags(oldRelative, newRelative);
            }
            _vault.SetFolderPassword(folder, password);
            _unlockedFolders[_vault.FindProtectedFolder(folder) ?? folder.Replace('\\', '/')] = password;
            SetCurrentNote(null);
            RefreshNotes();
            StatusLabel.Text = T("Folder protected.");
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or CryptographicException)
        {
            StatusLabel.Text = T("Could not protect the folder.");
        }
    }

    private async Task AttachFileAsync()
    {
        if (_currentPath.Length == 0 || IsEncrypted(_currentPath) && _notePassword is null)
            return;
        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = T("Attach file"), AllowMultiple = false
        });
        if (files.Count == 0)
            return;
        try
        {
            var relativeAttachment = _vault.CopyAttachment(files[0].Path.LocalPath);
            var noteDirectory = Path.GetDirectoryName(_vault.FullPath(_currentPath))!;
            var linkPath = Path.GetRelativePath(noteDirectory, _vault.FullPath(relativeAttachment))
                .Replace(Path.DirectorySeparatorChar, '/');
            var label = Path.GetFileName(relativeAttachment).Replace("]", "\\]");
            Editor.Text = (Editor.Text ?? string.Empty).TrimEnd() +
                          $"\n\n[{label}](<{linkPath}>)\n";
            RefreshNotes();
            StatusLabel.Text = T("Ready");
        }
        catch (IOException)
        {
            StatusLabel.Text = T("Could not copy the attachment into this notes folder.");
        }
    }

    private void ApplyInlineFormat(string prefix, string suffix)
    {
        if (_currentPath.Length == 0 || IsEncrypted(_currentPath) && _notePassword is null)
            return;
        var text = Editor.Text ?? string.Empty;
        var start = Editor.SelectionStart;
        var end = Editor.SelectionEnd;
        var selected = text[start..end];
        if (selected.Length == 0)
            selected = "text";
        var remove = selected.StartsWith(prefix, StringComparison.Ordinal) &&
                     selected.EndsWith(suffix, StringComparison.Ordinal) &&
                     selected.Length >= prefix.Length + suffix.Length;
        var formatted = remove
            ? selected[prefix.Length..^suffix.Length]
            : selected;
        var replacement = remove ? formatted : prefix + selected + suffix;
        Editor.Text = text[..start] + replacement + text[end..];
        Editor.SelectionStart = start + (remove ? 0 : prefix.Length);
        Editor.SelectionEnd = Editor.SelectionStart + formatted.Length;
        Editor.Focus();
    }

    private void ApplyLineFormat(string prefix)
    {
        if (_currentPath.Length == 0 || IsEncrypted(_currentPath) && _notePassword is null)
            return;
        var text = Editor.Text ?? string.Empty;
        var selectionStart = Editor.SelectionStart;
        var selectionEnd = Editor.SelectionEnd;
        var lineStart = text.LastIndexOf('\n', Math.Max(0, selectionStart - 1)) + 1;
        var lineEnd = text.IndexOf('\n', selectionEnd);
        if (lineEnd < 0)
            lineEnd = text.Length;
        var selectedLines = text[lineStart..lineEnd].Split('\n');
        var remove = selectedLines.All(line => line.StartsWith(prefix, StringComparison.Ordinal));
        for (var index = 0; index < selectedLines.Length; index++)
            selectedLines[index] = remove ? selectedLines[index][prefix.Length..] :
                selectedLines[index].StartsWith(prefix, StringComparison.Ordinal)
                    ? selectedLines[index] : prefix + selectedLines[index];
        var replacement = string.Join('\n', selectedLines);
        Editor.Text = text[..lineStart] + replacement + text[lineEnd..];
        Editor.SelectionStart = lineStart;
        Editor.SelectionEnd = lineStart + replacement.Length;
        Editor.Focus();
    }

    private async Task<string?> ShowPasswordDialogAsync(string title, string prompt)
    {
        var completion = new TaskCompletionSource<string?>();
        var input = new TextBox { PasswordChar = '\u2022' };
        var dialog = new Window
        {
            Title = title, Width = 440, Height = 220, CanResize = false,
            WindowStartupLocation = WindowStartupLocation.CenterOwner
        };
        var cancel = MakeDialogButton(T("Cancel"), () => CloseWith(null));
        var confirm = MakeDialogButton(T("Continue"), () => CloseWith(input.Text), "primary");
        dialog.Content = new StackPanel
        {
            Margin = new Avalonia.Thickness(22), Spacing = 14,
            Children =
            {
                new TextBlock { Text = prompt, FontSize = 14 }, input,
                new StackPanel { Orientation = Avalonia.Layout.Orientation.Horizontal, Spacing = 8,
                    HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Right,
                    Children = { cancel, confirm } }
            }
        };
        dialog.Closed += (_, _) => completion.TrySetResult(null);
        void CloseWith(string? value)
        {
            completion.TrySetResult(value);
            dialog.Close();
        }
        await dialog.ShowDialog(this);
        return await completion.Task;
    }

    private async Task EncryptOrUnlockAsync()
    {
        if (_currentPath.Length == 0)
            return;
        if (IsEncrypted(_currentPath))
        {
            if (_notePassword is null)
            {
                await LoadNoteAsync(_currentPath);
                return;
            }
            if (_dirty && !await SaveNoteAsync())
                return;
            _notePassword = null;
            _links.Remove(_currentPath);
            _loadingNote = true;
            Editor.Text = string.Empty;
            TagsInput.Text = string.Empty;
            _loadingNote = false;
            _dirty = false;
            DirtyIndicator.IsVisible = false;
            Editor.IsEnabled = false;
            TagsInput.IsEnabled = false;
            SaveButton.IsEnabled = false;
            EncryptButton.Content = T("Unlock note");
            PreviewText.Markdown = T("Unlock note");
            UpdateBacklinks();
            return;
        }
        if (_dirty && !await SaveNoteAsync())
            return;
        var password = await ShowPasswordDialogAsync(T("Encrypt note"), T("Password"));
        if (string.IsNullOrEmpty(password))
        {
            if (password is not null)
                StatusLabel.Text = T("The password cannot be empty.");
            return;
        }
        var confirmation = await ShowPasswordDialogAsync(T("Encrypt note"), T("Confirm password"));
        if (password != confirmation)
        {
            StatusLabel.Text = T("Passwords do not match.");
            return;
        }
        var oldPath = _currentPath;
        var oldFile = _vault.FullPath(oldPath);
        var newPath = Path.ChangeExtension(oldPath, ".evnote");
        var newFile = _vault.FullPath(newPath);
        if (File.Exists(newFile))
        {
            StatusLabel.Text = T("A note with that name already exists.");
            return;
        }
        try
        {
            var envelope = SecureNote.Encrypt(Encoding.UTF8.GetBytes(Editor.Text ?? string.Empty), password);
            VaultService.WriteAtomic(newFile, envelope);
            File.Delete(oldFile);
            _vault.MoveTags(oldPath, newPath);
            _currentPath = newPath;
            _notePassword = password;
            RefreshNotes();
            await LoadNoteAsync(newPath);
        }
        catch (Exception exception) when (exception is IOException or CryptographicException)
        {
            StatusLabel.Text = T("Could not encrypt the note.");
        }
    }

    private async Task InsertNoteLinkAsync()
    {
        if (_currentPath.Length == 0 || IsEncrypted(_currentPath) && _notePassword is null)
            return;
        var choices = _titles.Where(item => item.Key != _currentPath)
            .Select(item => new LinkChoice(item.Value, item.Key)).ToArray();
        if (choices.Length == 0)
            return;
        var selected = await ShowChoiceDialogAsync(T("Link note"), T("Choose a note to link"),
            choices.Select(item => item.Title + "  ·  " + item.RelativePath).ToArray());
        if (selected is null)
            return;
        var target = choices.First(item =>
            (item.Title + "  ·  " + item.RelativePath) == selected).RelativePath;
        InsertAtCaret($"[[{target}]]");
        UpdateCurrentLinks();
    }

    private void InsertAtCaret(string text)
    {
        var content = Editor.Text ?? string.Empty;
        var start = Editor.SelectionStart;
        var end = Editor.SelectionEnd;
        Editor.Text = content[..start] + text + content[end..];
        Editor.CaretIndex = start + text.Length;
        Editor.Focus();
    }

    private async Task<string?> ShowChoiceDialogAsync(string title, string prompt, IReadOnlyList<string> choices)
    {
        if (choices.Count == 0)
            return null;
        var completion = new TaskCompletionSource<string?>();
        var selection = new ComboBox { ItemsSource = choices, SelectedIndex = 0 };
        var dialog = new Window
        {
            Title = title, Width = 480, Height = 230, CanResize = false,
            WindowStartupLocation = WindowStartupLocation.CenterOwner
        };
        var cancel = MakeDialogButton(T("Cancel"), () => CloseWith(null));
        var choose = MakeDialogButton(T("Continue"), () => CloseWith(selection.SelectedItem?.ToString()), "primary");
        dialog.Content = new StackPanel
        {
            Margin = new Avalonia.Thickness(22), Spacing = 14,
            Children =
            {
                new TextBlock { Text = prompt, FontSize = 14 }, selection,
                new StackPanel { Orientation = Avalonia.Layout.Orientation.Horizontal, Spacing = 8,
                    HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Right,
                    Children = { cancel, choose } }
            }
        };
        dialog.Closed += (_, _) => completion.TrySetResult(null);
        void CloseWith(string? value)
        {
            completion.TrySetResult(value);
            dialog.Close();
        }
        await dialog.ShowDialog(this);
        return await completion.Task;
    }

    private async Task ManageTemplatesAsync()
    {
        var templates = _vault.GetTemplateNames().ToList();
        var selector = new ComboBox { ItemsSource = templates, SelectedIndex = templates.Count > 0 ? 0 : -1 };
        var name = new TextBox();
        var body = new TextBox { AcceptsReturn = true, AcceptsTab = true, MinHeight = 230,
            TextWrapping = Avalonia.Media.TextWrapping.Wrap };
        var dialog = new Window
        {
            Title = T("Templates"), Width = 680, Height = 560, MinWidth = 520, MinHeight = 400,
            WindowStartupLocation = WindowStartupLocation.CenterOwner
        };
        var save = new Button { Content = T("Save") };
        var delete = new Button { Content = T("Delete permanently"), IsEnabled = templates.Count > 0 };
        var fresh = new Button { Content = T("New note") };
        var close = new Button { Content = T("Close") };
        var form = new StackPanel
        {
            Margin = new Avalonia.Thickness(22), Spacing = 12,
            Children =
            {
                new TextBlock { Text = T("Templates"), FontSize = 22,
                    FontWeight = Avalonia.Media.FontWeight.SemiBold },
                new TextBlock { Text = T("Existing template") }, selector,
                new TextBlock { Text = T("Name") }, name,
                new TextBlock { Text = T("Markdown") }, body,
                new StackPanel { Orientation = Avalonia.Layout.Orientation.Horizontal, Spacing = 8,
                    HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Right,
                    Children = { fresh, delete, save, close } }
            }
        };
        dialog.Content = form;
        selector.SelectionChanged += (_, _) =>
        {
            var selected = selector.SelectedItem?.ToString() ?? string.Empty;
            name.Text = selected;
            body.Text = selected.Length == 0 ? string.Empty : _vault.ReadTemplate(selected);
        };
        fresh.Click += (_, _) => { selector.SelectedItem = null; name.Text = string.Empty; body.Text = string.Empty; };
        save.Click += (_, _) =>
        {
            var oldName = selector.SelectedItem?.ToString();
            if (!_vault.SaveTemplate(name.Text ?? string.Empty, body.Text ?? string.Empty, oldName))
            {
                StatusLabel.Text = T("Enter a valid template name.");
                return;
            }
            dialog.Close();
        };
        delete.Click += async (_, _) =>
        {
            if (selector.SelectedItem is not string selected ||
                !await ShowConfirmationAsync(dialog, T("Delete permanently"), T("Delete template?")))
                return;
            _vault.DeleteTemplate(selected);
            templates.Remove(selected);
            selector.ItemsSource = templates;
            selector.SelectedIndex = templates.Count > 0 ? 0 : -1;
            delete.IsEnabled = templates.Count > 0;
        };
        close.Click += (_, _) => dialog.Close();
        await dialog.ShowDialog(this);
    }

    private async Task ShowGraphAsync()
    {
        var paths = _titles.Keys.ToArray();
        if (paths.Length == 0)
            return;
        const double graphWidth = 900;
        const double graphHeight = 620;
        const double radius = 235;
        var canvas = new Canvas { Width = graphWidth, Height = graphHeight };
        Window? graph = null;
        var points = paths.Select((path, index) => (path, point: new Avalonia.Point(
            graphWidth / 2 + radius * Math.Cos(2 * Math.PI * index / paths.Length - Math.PI / 2),
            graphHeight / 2 + radius * Math.Sin(2 * Math.PI * index / paths.Length - Math.PI / 2))))
            .ToDictionary(item => item.path, item => item.point, StringComparer.OrdinalIgnoreCase);
        foreach (var edge in _links)
        {
            if (!points.TryGetValue(edge.Key, out var from))
                continue;
            foreach (var target in edge.Value)
            {
                if (!points.TryGetValue(target, out var to))
                    continue;
                var line = new Avalonia.Controls.Shapes.Line
                {
                    StartPoint = from,
                    EndPoint = to,
                    Stroke = new SolidColorBrush(Color.Parse("#49685C")),
                    StrokeThickness = 1.4
                };
                canvas.Children.Add(line);
            }
        }
        foreach (var (path, point) in points)
        {
            var title = _titles.GetValueOrDefault(path, path);
            var isCurrent = path.Equals(_currentPath, StringComparison.OrdinalIgnoreCase);
            var node = new Border
            {
                Width = 150,
                Padding = new Avalonia.Thickness(10, 7),
                CornerRadius = new Avalonia.CornerRadius(8),
                Background = new SolidColorBrush(Color.Parse(isCurrent ? "#315B4D" : "#202A27")),
                BorderBrush = new SolidColorBrush(Color.Parse(isCurrent ? "#73B7A1" : "#35423D")),
                BorderThickness = new Avalonia.Thickness(1),
                Child = new TextBlock { Text = title, TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis,
                    Foreground = Brushes.White, HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Center }
            };
            Canvas.SetLeft(node, point.X - 75);
            Canvas.SetTop(node, point.Y - 18);
            node.PointerPressed += async (_, e) =>
            {
                e.Handled = true;
                graph?.Close();
                if (path != _currentPath)
                    await LoadNoteAsync(path);
            };
            canvas.Children.Add(node);
        }
        graph = new Window
        {
            Title = T("Graph"), Width = 960, Height = 700, MinWidth = 640, MinHeight = 480,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
            Content = new Border
            {
                Margin = new Avalonia.Thickness(14), Padding = new Avalonia.Thickness(10),
                Background = new SolidColorBrush(Color.Parse("#141B19")),
                Child = new ScrollViewer { Content = canvas }
            }
        };
        await graph.ShowDialog(this);
    }

    private async Task ExportAsync()
    {
        var selectedNotes = NotesList.SelectedItems?.OfType<NoteItem>()
            .Where(item => !item.IsAttachment).ToArray() ?? [];
        var folder = SelectedFolderPath();
        var scopes = new List<string>();
        if (_currentPath.Length > 0)
            scopes.Add(T("Current note"));
        if (selectedNotes.Length > 0)
            scopes.Add(T("Selected notes"));
        if (folder.Length > 0)
            scopes.Add(T("Current folder"));
        scopes.Add(T("Entire vault"));
        var scope = await ShowChoiceDialogAsync(T("Export notes"), T("Choose what to export"), scopes);
        if (scope is null)
            return;
        IEnumerable<string> selectedPaths = scope switch
        {
            var value when value == T("Current note") && _currentPath.Length > 0 => [_currentPath],
            var value when value == T("Selected notes") => selectedNotes.Select(item => item.RelativePath),
            var value when value == T("Current folder") && folder.Length > 0 =>
                _vault.GetItems(string.Empty, folder).Where(item => !item.IsAttachment)
                    .Select(item => item.RelativePath),
            _ => _vault.GetItems(string.Empty, string.Empty).Where(item => !item.IsAttachment)
                .Select(item => item.RelativePath)
        };
        var paths = selectedPaths.ToArray();
        if (paths.Length == 0)
        {
            StatusLabel.Text = T("No notes selected for export.");
            return;
        }

        var records = new List<ExportRecord>();
        foreach (var path in paths.Distinct(StringComparer.OrdinalIgnoreCase))
        {
            string content;
            if (path == _currentPath && (!IsEncrypted(path) || _notePassword is not null))
                content = Editor.Text ?? string.Empty;
            else
            {
                var bytes = await File.ReadAllBytesAsync(_vault.FullPath(path));
                if (IsEncrypted(path))
                {
                    var protectedFolder = _vault.FindProtectedFolder(path);
                    var password = path == _currentPath ? _notePassword : null;
                    if (password is null && protectedFolder is not null)
                        _unlockedFolders.TryGetValue(protectedFolder, out password);
                    password ??= await ShowPasswordDialogAsync(T("Export notes"), T("Password"));
                    if (password is null || !SecureNote.TryDecrypt(bytes, password, out bytes))
                    {
                        StatusLabel.Text = T("The password is incorrect or the note is damaged.");
                        return;
                    }
                }
                content = Encoding.UTF8.GetString(bytes);
            }
            records.Add(new ExportRecord(path, _titles.GetValueOrDefault(path,
                Path.GetFileNameWithoutExtension(path)), content, _vault.Tags.GetValueOrDefault(path) ?? []));
        }

        var formats = new[]
        {
            new FilePickerFileType("Markdown (*.md)") { Patterns = ["*.md"] },
            new FilePickerFileType("Plain text (*.txt)") { Patterns = ["*.txt"] },
            new FilePickerFileType("HTML (*.html)") { Patterns = ["*.html"] },
            new FilePickerFileType("PDF (*.pdf)") { Patterns = ["*.pdf"] },
            new FilePickerFileType("XML (*.xml)") { Patterns = ["*.xml"] },
            new FilePickerFileType("CSV (*.csv)") { Patterns = ["*.csv"] },
            new FilePickerFileType("Word (*.docx)") { Patterns = ["*.docx"] },
            new FilePickerFileType("Word 97 (*.doc)") { Patterns = ["*.doc"] },
            new FilePickerFileType("Rich text (*.rtf)") { Patterns = ["*.rtf"] },
            new FilePickerFileType("EPUB (*.epub)") { Patterns = ["*.epub"] },
            new FilePickerFileType("FictionBook (*.fb2)") { Patterns = ["*.fb2"] }
        };
        var destination = await StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
        {
            Title = T("Export notes"),
            SuggestedFileName = records.Count == 1 ? records[0].Title + ".md" : "notes.md",
            DefaultExtension = "md",
            FileTypeChoices = formats
        });
        if (destination is null)
            return;
        var output = destination.Path.LocalPath;
        var extension = Path.GetExtension(output).TrimStart('.').ToLowerInvariant();
        if (extension.Length == 0)
        {
            extension = "md";
            output += ".md";
        }
        var markdown = string.Join("\n\n", records.Select(record =>
            $"# {record.Title}\n\n" + (record.Tags.Count > 0 ? $"**Tags:** {string.Join(", ", record.Tags)}\n\n" : "") +
            record.Content));
        try
        {
            switch (extension)
            {
                case "md":
                    await File.WriteAllTextAsync(output, markdown, new UTF8Encoding(false));
                    break;
                case "txt":
                    var plainText = System.Net.WebUtility.HtmlDecode(Regex.Replace(
                        global::Markdig.Markdown.ToHtml(markdown), "<[^>]+>", string.Empty));
                    await File.WriteAllTextAsync(output, plainText, new UTF8Encoding(false));
                    break;
                case "html":
                    await File.WriteAllTextAsync(output, global::Markdig.Markdown.ToHtml(markdown), new UTF8Encoding(false));
                    break;
                case "xml":
                    var xml = new System.Xml.Linq.XDocument(new System.Xml.Linq.XElement("notes",
                        records.Select(record => new System.Xml.Linq.XElement("note",
                            new System.Xml.Linq.XAttribute("path", record.Path),
                            new System.Xml.Linq.XElement("title", record.Title),
                            new System.Xml.Linq.XElement("content", record.Content),
                            new System.Xml.Linq.XElement("tags", record.Tags.Select(tag =>
                                new System.Xml.Linq.XElement("tag", tag)))))));
                    await File.WriteAllTextAsync(output, xml.ToString(), new UTF8Encoding(false));
                    break;
                case "csv":
                    static string Csv(string value) => "\"" + value.Replace("\"", "\"\"") + "\"";
                    var csv = "path,title,tags,content\r\n" + string.Join("\r\n", records.Select(record =>
                        string.Join(',', Csv(record.Path), Csv(record.Title), Csv(string.Join(", ", record.Tags)),
                            Csv(record.Content))));
                    await File.WriteAllTextAsync(output, csv, new UTF8Encoding(false));
                    break;
                default:
                    await ExportWithPandocAsync(markdown, output, extension);
                    break;
            }
            StatusLabel.Text = T("Export complete");
        }
        catch (Exception exception) when (exception is IOException or InvalidOperationException or
                          UnauthorizedAccessException or System.ComponentModel.Win32Exception)
        {
            StatusLabel.Text = T("Could not export the selected notes.");
        }
    }

    private static async Task ExportWithPandocAsync(string markdown, string outputPath, string extension)
    {
        var temporary = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N") + ".md");
        string? pandocOutput = null;
        await File.WriteAllTextAsync(temporary, markdown, new UTF8Encoding(false));
        try
        {
            var needsOffice = extension is "doc" or "pdf";
            pandocOutput = needsOffice
                ? Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N") + ".docx")
                : outputPath;
            var start = new ProcessStartInfo(FindTool("pandoc.exe", "tools/pandoc.exe"))
            {
                UseShellExecute = false,
                CreateNoWindow = true
            };
            start.ArgumentList.Add(temporary);
            start.ArgumentList.Add("--from=markdown");
            start.ArgumentList.Add("--to=" + (needsOffice ? "docx" : extension));
            start.ArgumentList.Add("--standalone");
            start.ArgumentList.Add("--output");
            start.ArgumentList.Add(pandocOutput);
            using var process = Process.Start(start) ?? throw new InvalidOperationException();
            await process.WaitForExitAsync();
            if (process.ExitCode != 0)
                throw new InvalidOperationException("Pandoc failed.");
            if (needsOffice)
            {
                var converter = new ProcessStartInfo(FindTool("soffice.exe", "tools/LibreOffice/program/soffice.exe"))
                {
                    UseShellExecute = false,
                    CreateNoWindow = true
                };
                converter.ArgumentList.Add("--headless");
                converter.ArgumentList.Add("-env:UserInstallation=" +
                    new Uri(Path.Combine(Path.GetTempPath(), "evidence-libreoffice-profile-" + Guid.NewGuid())).AbsoluteUri);
                converter.ArgumentList.Add("--convert-to");
                converter.ArgumentList.Add(extension);
                converter.ArgumentList.Add("--outdir");
                converter.ArgumentList.Add(Path.GetDirectoryName(outputPath)!);
                converter.ArgumentList.Add(pandocOutput);
                using var conversion = Process.Start(converter) ?? throw new InvalidOperationException();
                await conversion.WaitForExitAsync();
                if (conversion.ExitCode != 0)
                    throw new InvalidOperationException("Document conversion failed.");
                var convertedPath = Path.Combine(Path.GetDirectoryName(outputPath)!,
                    Path.GetFileNameWithoutExtension(pandocOutput) + "." + extension);
                if (!File.Exists(convertedPath))
                    throw new InvalidOperationException("Converted document was not created.");
                File.Move(convertedPath, outputPath, true);
            }
        }
        finally
        {
            File.Delete(temporary);
            if (pandocOutput is not null && pandocOutput != outputPath && File.Exists(pandocOutput))
                File.Delete(pandocOutput);
        }
    }

    private static string FindTool(string executableName, string bundledRelativePath)
    {
        var bundled = Path.Combine(AppContext.BaseDirectory, bundledRelativePath.Replace('/', Path.DirectorySeparatorChar));
        if (File.Exists(bundled))
            return bundled;
        var pathEntries = (Environment.GetEnvironmentVariable("PATH") ?? string.Empty)
            .Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries);
        foreach (var entry in pathEntries)
        {
            var candidate = Path.Combine(entry, executableName);
            if (File.Exists(candidate))
                return candidate;
        }
        return OperatingSystem.IsWindows() ? executableName : Path.GetFileNameWithoutExtension(executableName);
    }

    private sealed record LinkChoice(string Title, string RelativePath);
    private sealed record ExportRecord(string Path, string Title, string Content, List<string> Tags);

    private void UpdatePreview() => PreviewText.Markdown = RenderPreview(Editor.Text ?? string.Empty);

    private string RenderPreview(string markdown)
    {
        return WikiLinkPattern().Replace(markdown, match =>
            match.Groups[2].Success ? match.Groups[2].Value.Trim() : match.Groups[1].Value.Trim());
    }

    private static string? ReadBootstrapLanguage()
    {
        var path = Path.Combine(AppContext.BaseDirectory, "initial-language.ini");
        if (!File.Exists(path))
            return null;
        foreach (var line in File.ReadLines(path))
        {
            var separator = line.IndexOf('=');
            if (separator <= 0 || !line[..separator].Trim().Equals("language", StringComparison.OrdinalIgnoreCase))
                continue;
            var language = line[(separator + 1)..].Trim().Trim('"');
            if (language is "ru" or "en")
                return language;
        }
        return null;
    }

    private static UserSettings LoadSettings()
    {
        try
        {
            var path = SettingsPath();
            if (File.Exists(path))
                return JsonSerializer.Deserialize<UserSettings>(File.ReadAllText(path)) ?? new UserSettings();
        }
        catch (IOException)
        {
        }
        catch (JsonException)
        {
        }
        return new UserSettings();
    }

    private static string DetectLanguage() =>
        System.Globalization.CultureInfo.CurrentUICulture.TwoLetterISOLanguageName == "ru" ? "ru" : "en";

    private static bool PathsEqual(string first, string second)
    {
        if (string.IsNullOrWhiteSpace(first) || string.IsNullOrWhiteSpace(second))
            return false;
        var comparison = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        return Path.GetFullPath(first).TrimEnd(Path.DirectorySeparatorChar)
            .Equals(Path.GetFullPath(second).TrimEnd(Path.DirectorySeparatorChar), comparison);
    }

    private static void TryMigrateVault(string source, string destination)
    {
        if (!Directory.Exists(source) || PathsEqual(source, destination))
            return;
        try
        {
            foreach (var directory in Directory.EnumerateDirectories(source, "*", SearchOption.AllDirectories))
                Directory.CreateDirectory(Path.Combine(destination, Path.GetRelativePath(source, directory)));
            foreach (var file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
            {
                var target = Path.Combine(destination, Path.GetRelativePath(source, file));
                Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                if (!File.Exists(target))
                    File.Copy(file, target);
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
        }
    }

    private static string SettingsPath() => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "evidence", "settings.json");

    private void SaveSettings()
    {
        try
        {
            var path = SettingsPath();
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, JsonSerializer.Serialize(new UserSettings(_language, _vaultPath)),
                new UTF8Encoding(false));
        }
        catch (IOException)
        {
        }
    }

    private static void TryRemoveBootstrapFile()
    {
        try
        {
            File.Delete(Path.Combine(AppContext.BaseDirectory, "initial-language.ini"));
        }
        catch (IOException)
        {
        }
    }

    private enum UnsavedChoice
    {
        Cancel,
        Save,
        Discard
    }

    private sealed record UserSettings(string Language = "", string VaultPath = "");
    private sealed record TrashManifest(string OriginalPath, string StoredName, List<string>? Tags = null);
    private sealed record TrashEntry(string Directory, TrashManifest Manifest)
    {
        public override string ToString() => Manifest.OriginalPath.Replace(Path.DirectorySeparatorChar, '/');
    }
}