# evidence

`evidence` is a cross-platform Markdown notes app. The current Avalonia UI uses a dark theme by default and supports English and Russian. The Windows installer publishes a self-contained .NET desktop application.

## Avalonia app

Requirements: .NET 10 SDK.

```sh
dotnet restore app/Evidence.Avalonia/Evidence.csproj
dotnet run --project app/Evidence.Avalonia/Evidence.csproj
```

The Avalonia version supports Markdown note creation and editing, nested categories, search, tags, attachments, templates, wiki links and backlinks, graph view, encrypted notes and password-protected categories, trash and restore, and export to common document formats. It reads the initial language selected by the Windows installer and remembers the language and vault path for the current user. The default notes vault is the `Notes` folder beside the application; categories are subfolders inside the vault.

## Windows installer

Requirements: .NET 10 SDK and Inno Setup 6.

```powershell
& .\installer\build.ps1 -Version 0.1.0
```

The installer is written to `dist` and contains the self-contained `win-x64` Avalonia app, so users do not need to install .NET separately. The setup wizard lets users choose an installation folder and the initial application language. English and Russian are supported. The Windows installer workflow builds on pushes to `main`, on demand, and on version tags (`v*`).

## Legacy Qt app

The original Qt 6/C++ implementation remains available while its features are migrated. Its build requirements and commands remain:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/evidence
```

The Qt app uses Markdown files in `~/Documents/evidence` by default. It includes encrypted notes, search indexing, tags, attachments, templates, wiki links, graph view, and export. See the C++ sources for the current legacy implementation.