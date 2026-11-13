# evidence

`evidence` is a cross-platform desktop app for Markdown notes. The active application is built with Avalonia UI and .NET 10. It stores notes as local files in a user-selected vault; there is no server or cloud account.

## Features

- Create, edit, rename, search, and organize Markdown notes in nested folders.
- Edit Markdown with formatting shortcuts, undo/redo, and a live rendered preview.
- Add comma-separated tags, filter by folder, and search case-insensitively across note titles, paths, text, and tags.
- Attach files to the vault and open them with the operating system's default application. Text, Markdown, CSV, log, and PDF attachments can also be searched.
- Create notes from reusable Markdown templates. `{{title}}` and `{{date}}` are replaced when a template is applied.
- Link notes with `[[note title]]` or `[[note title|display text]]`; view backlinks and an interactive graph of linked notes.
- Encrypt individual notes or protect a folder. Protected folders encrypt their supported note files and require a password to unlock in the app.
- Move notes to the in-app trash, restore them, or permanently delete them.
- Export the current note, selected notes, a folder, or the whole vault to Markdown, plain text, HTML, XML, CSV, PDF, DOCX, DOC, RTF, EPUB, or FB2.
- Use the interface in English or Russian. The app watches the vault for external file changes and refreshes the note list.

## Storage and security

The default vault is a `Notes` directory beside the application. Use **Open folder** to choose another location; the selected vault path and language are saved in the current user's application-data directory. On first launch, files from `Documents/evidence` are copied into the default vault when a matching destination file does not already exist.

Notes are UTF-8 Markdown files (`.md` or `.markdown`). The vault also uses:

```text
Notes/
├── attachments/             # copied attachments
└── .evidence/
    ├── metadata.json        # tags
    ├── folders.json         # protected-folder password verifiers
    ├── templates/           # Markdown templates
    └── trash/               # trashed notes and restore manifests
```

Encrypted notes use the `.evnote` format. The app derives a 256-bit key from the password with PBKDF2-HMAC-SHA-256 (310,000 iterations) and encrypts note contents with AES-256-GCM using a random salt and nonce. Folder passwords are stored as salted PBKDF2-HMAC-SHA-256 verifiers.

Protection applies to note contents, not the entire vault: filenames and tag metadata remain visible, and attachments are not encrypted. Keep backups of the vault and protect the device and its files accordingly.

## Build and run

Requirements: .NET 10 SDK. CMake 3.21 or newer is also required for the CMake build.

```sh
cmake -S . -B build
cmake --build build
```

Or run the Avalonia project directly:

```sh
dotnet restore app/Evidence.Avalonia/Evidence.csproj
dotnet run --project app/Evidence.Avalonia/Evidence.csproj
```

The main UI is in `app/Evidence.Avalonia`. CMake builds this project; the Qt 6/C++ sources in `src/` are retained for reference and are no longer built or shipped.

## Windows installer

Requirements: .NET 10 SDK, Inno Setup 6, Pandoc, and LibreOffice.

```powershell
& .\installer\build.ps1 -Version 0.1.0
```

The script publishes a self-contained `win-x64` Avalonia app, bundles Pandoc and LibreOffice for document export, and creates the installer in `dist`. End users do not need to install .NET separately. The setup wizard lets users select the installation directory and initial interface language.

The GitHub Actions workflow builds the installer on pushes to `main`, on demand, and for version tags (`v*`). For version tags, it also attaches the installer to the GitHub Release.
