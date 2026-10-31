using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using UglyToad.PdfPig;

namespace Evidence;

internal sealed class VaultService
{
    private readonly string _root;

    public VaultService(string root)
    {
        _root = Path.GetFullPath(root);
        Directory.CreateDirectory(_root);
        Directory.CreateDirectory(Path.Combine(_root, ".evidence"));
        Tags = LoadMetadata();
    }

    public Dictionary<string, List<string>> Tags { get; }
    public string Root => _root;

    public IReadOnlyList<NoteItem> GetItems(string query, string folder,
        string unlockedPath = "", string unlockedContent = "")
    {
        var terms = query.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        var items = new List<NoteItem>();
        foreach (var file in EnumerateNoteFiles())
        {
            var relative = Relative(file);
            var isEncrypted = Path.GetExtension(file).Equals(".evnote", StringComparison.OrdinalIgnoreCase);
            if (!InFolder(relative, folder))
                continue;
            var title = Path.GetFileNameWithoutExtension(file);
            var tags = Tags.TryGetValue(relative, out var savedTags) ? savedTags : [];
            var content = isEncrypted
                ? (relative.Equals(unlockedPath, StringComparison.OrdinalIgnoreCase) ? unlockedContent : string.Empty)
                : ReadText(file);
            if (!Matches(terms, relative, title, content, string.Join(' ', tags)))
                continue;
            items.Add(new NoteItem(relative, title, isEncrypted, false, string.Join("  #", tags)));
        }

        foreach (var file in EnumerateAttachments())
        {
            var relative = Relative(file);
            var title = Path.GetFileName(file);
            var content = ReadAttachmentText(file);
            if (!Matches(terms, relative, title, content, string.Empty))
                continue;
            items.Add(new NoteItem(relative, title, false, true, string.Empty));
        }

        return items.OrderBy(item => item.IsAttachment).ThenBy(item => item.Title,
            StringComparer.CurrentCultureIgnoreCase).ToArray();
    }

    public IReadOnlyList<string> GetFolders()
    {
        var folders = Directory.EnumerateDirectories(_root, "*", SearchOption.AllDirectories)
            .Select(Relative)
            .Where(path => !path.Split(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
                .Any(part => part.Equals(".evidence", StringComparison.OrdinalIgnoreCase) ||
                             part.Equals("attachments", StringComparison.OrdinalIgnoreCase)))
            .OrderBy(path => path, StringComparer.CurrentCultureIgnoreCase)
            .ToList();
        folders.Insert(0, string.Empty);
        return folders;
    }

    public IReadOnlyList<string> GetTemplateNames()
    {
        var directory = Path.Combine(_root, ".evidence", "templates");
        if (!Directory.Exists(directory))
            return [];
        return Directory.EnumerateFiles(directory, "*.md")
            .Select(Path.GetFileNameWithoutExtension)
            .Where(name => !string.IsNullOrWhiteSpace(name))
            .Cast<string>()
            .OrderBy(name => name, StringComparer.CurrentCultureIgnoreCase).ToArray();
    }

    public string ReadTemplate(string name)
    {
        var safeName = SafeTemplateName(name);
        var path = Path.Combine(_root, ".evidence", "templates", safeName + ".md");
        return File.Exists(path) ? File.ReadAllText(path, Encoding.UTF8) : string.Empty;
    }

    public bool SaveTemplate(string name, string content, string? oldName = null)
    {
        var safeName = SafeTemplateName(name);
        if (safeName.Length == 0)
            return false;
        var directory = Path.Combine(_root, ".evidence", "templates");
        Directory.CreateDirectory(directory);
        WriteAtomic(Path.Combine(directory, safeName + ".md"), new UTF8Encoding(false).GetBytes(content));
        if (!string.IsNullOrWhiteSpace(oldName) &&
            !oldName.Equals(safeName, StringComparison.OrdinalIgnoreCase))
            File.Delete(Path.Combine(directory, SafeTemplateName(oldName) + ".md"));
        return true;
    }

    public void DeleteTemplate(string name) =>
        File.Delete(Path.Combine(_root, ".evidence", "templates", SafeTemplateName(name) + ".md"));

    public string CreateFolder(string relativePath)
    {
        var clean = relativePath.Replace('\\', '/').Trim('/');
        if (clean.Length == 0 || Path.IsPathRooted(clean) ||
            clean.Split('/').Any(part => part is "" or "." or ".." or ".evidence" or "attachments"))
            throw new ArgumentException("The folder path is invalid.", nameof(relativePath));
        var fullPath = Path.GetFullPath(Path.Combine(_root, clean.Replace('/', Path.DirectorySeparatorChar)));
        EnsureInsideRoot(fullPath);
        Directory.CreateDirectory(fullPath);
        return Relative(fullPath);
    }

    public string CopyAttachment(string sourcePath)
    {
        var directory = Path.Combine(_root, "attachments");
        Directory.CreateDirectory(directory);
        var name = Path.GetFileName(sourcePath);
        var destination = Path.Combine(directory, name);
        var number = 2;
        while (File.Exists(destination))
        {
            destination = Path.Combine(directory,
                $"{Path.GetFileNameWithoutExtension(name)} ({number++}){Path.GetExtension(name)}");
        }
        File.Copy(sourcePath, destination);
        return Relative(destination);
    }

    public void SetTags(string relativePath, IEnumerable<string> tags)
    {
        var cleanTags = tags.Select(tag => tag.Trim()).Where(tag => tag.Length > 0)
            .Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        if (cleanTags.Count == 0)
            Tags.Remove(relativePath);
        else
            Tags[relativePath] = cleanTags;
        SaveMetadata();
    }

    public void MoveTags(string oldPath, string newPath)
    {
        if (Tags.Remove(oldPath, out var tags))
            Tags[newPath] = tags;
        SaveMetadata();
    }

    public List<string> RemoveTags(string path)
    {
        var tags = Tags.Remove(path, out var saved) ? saved : [];
        SaveMetadata();
        return tags;
    }

    public void RestoreTags(string path, IEnumerable<string> tags)
    {
        var restored = tags.ToList();
        if (restored.Count > 0)
            Tags[path] = restored;
        SaveMetadata();
    }

    public void SaveMetadata()
    {
        var path = Path.Combine(_root, ".evidence", "metadata.json");
        WriteAtomic(path, JsonSerializer.SerializeToUtf8Bytes(Tags, new JsonSerializerOptions
        {
            WriteIndented = true
        }));
    }

    public string Relative(string absolutePath) => Path.GetRelativePath(_root, absolutePath);

    public string FullPath(string relativePath)
    {
        var full = Path.GetFullPath(Path.Combine(_root, relativePath));
        EnsureInsideRoot(full);
        return full;
    }

    public static void WriteAtomic(string path, ReadOnlySpan<byte> contents)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                stream.Write(contents);
            File.Move(temporary, path, true);
        }
        finally
        {
            if (File.Exists(temporary))
                File.Delete(temporary);
        }
    }

    private Dictionary<string, List<string>> LoadMetadata()
    {
        var path = Path.Combine(_root, ".evidence", "metadata.json");
        if (!File.Exists(path))
            return new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
        try
        {
            return JsonSerializer.Deserialize<Dictionary<string, List<string>>>(File.ReadAllText(path),
                       new JsonSerializerOptions { PropertyNameCaseInsensitive = true }) ??
                   new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
        }
        catch (JsonException)
        {
            return new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
        }
    }

    private IEnumerable<string> EnumerateNoteFiles() =>
        Directory.EnumerateFiles(_root, "*", SearchOption.AllDirectories)
            .Where(path => !HasReservedDirectory(Relative(path)))
            .Where(path => Path.GetExtension(path) is var extension &&
                (extension.Equals(".md", StringComparison.OrdinalIgnoreCase) ||
                 extension.Equals(".markdown", StringComparison.OrdinalIgnoreCase) ||
                 extension.Equals(".evnote", StringComparison.OrdinalIgnoreCase)));

    private IEnumerable<string> EnumerateAttachments()
    {
        var root = Path.Combine(_root, "attachments");
        return Directory.Exists(root)
            ? Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)
            : [];
    }

    private static bool HasReservedDirectory(string relative) =>
        relative.Split(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
            .Any(part => part.Equals(".evidence", StringComparison.OrdinalIgnoreCase) ||
                         part.Equals("attachments", StringComparison.OrdinalIgnoreCase));

    private static bool InFolder(string relative, string folder) =>
        string.IsNullOrEmpty(folder) || relative.StartsWith(folder + Path.DirectorySeparatorChar,
            OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);

    private static bool Matches(string[] terms, params string[] fields)
    {
        if (terms.Length == 0)
            return true;
        var haystack = string.Join('\n', fields);
        return terms.All(term => haystack.Contains(term, StringComparison.OrdinalIgnoreCase));
    }

    private static string ReadText(string path)
    {
        try
        {
            return File.ReadAllText(path, Encoding.UTF8);
        }
        catch (IOException)
        {
            return string.Empty;
        }
    }

    private static string ReadAttachmentText(string path)
    {
        var extension = Path.GetExtension(path);
        if (extension.Equals(".pdf", StringComparison.OrdinalIgnoreCase))
        {
            try
            {
                using var document = PdfDocument.Open(path);
                return string.Join('\n', document.GetPages().Select(page => page.Text));
            }
            catch (Exception exception) when (exception is IOException or InvalidOperationException)
            {
                return string.Empty;
            }
        }
        if (new[] { ".txt", ".md", ".markdown", ".csv", ".log" }
            .Contains(extension, StringComparer.OrdinalIgnoreCase))
            return ReadText(path);
        return string.Empty;
    }

    private static string SafeTemplateName(string name) =>
        string.Join('_', name.Trim().Split(Path.GetInvalidFileNameChars(), StringSplitOptions.RemoveEmptyEntries));

    private void EnsureInsideRoot(string fullPath)
    {
        var root = _root.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var comparison = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        if (!fullPath.StartsWith(root, comparison))
            throw new ArgumentException("The path must be inside the vault.", nameof(fullPath));
    }
}
