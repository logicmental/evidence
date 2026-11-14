#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

#define AppName "evidence"

#ifnexist "..\dist\payload\evidence.exe"
  #error "Application payload not found. Run installer\build.ps1 first."
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[CustomMessages]
english.UninstallVaultPrompt=Delete the notes vault and all its contents? Choose No to keep your notes.
russian.UninstallVaultPrompt=Удалить хранилище заметок и всё его содержимое? Нажмите «Нет», чтобы сохранить заметки.
english.UninstallVaultBlocked=The notes folder is a protected location and will be kept to prevent deleting unrelated files:
russian.UninstallVaultBlocked=Папка заметок находится в защищённом расположении. Она будет сохранена, чтобы не удалить посторонние файлы:
english.UninstallVaultDeleteFailed=The notes vault could not be completely deleted. The application will still be uninstalled:
russian.UninstallVaultDeleteFailed=Не удалось полностью удалить хранилище заметок. Приложение всё равно будет удалено:

[Setup]
AppId={{C7EF16BC-1B9B-4A92-8C9E-6BF8A65F2B80}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=evidence
ShowLanguageDialog=yes
DefaultDirName={autopf}\evidence
DisableDirPage=no
DefaultGroupName=evidence
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=evidence-install-core-{#AppVersion}-windows-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=evidence.ico
UninstallDisplayIcon={app}\evidence.exe

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Code]
var
  NotesVaultPath: String;

function GetApplicationLanguage(Param: String): String;
begin
  if ActiveLanguage = 'russian' then
    Result := 'ru'
  else
    Result := 'en';
end;

function IsSameOrParentPath(const Path, OtherPath: String): Boolean;
var
  NormalizedPath, NormalizedOtherPath: String;
begin
  NormalizedPath := AddBackslash(RemoveBackslashUnlessRoot(ExpandFileName(Path)));
  NormalizedOtherPath := AddBackslash(RemoveBackslashUnlessRoot(ExpandFileName(OtherPath)));
  Result := CompareText(
    Copy(NormalizedOtherPath, 1, Length(NormalizedPath)), NormalizedPath) = 0;
end;

function IsSafeVaultPath(const Path: String): Boolean;
var
  NormalizedPath, DriveRoot, AppPath, WindowsPath: String;
begin
  Result := False;
  if (Path = '') or (ExtractFileDrive(Path) = '') then
    Exit;

  NormalizedPath := RemoveBackslashUnlessRoot(ExpandFileName(Path));
  DriveRoot := AddBackslash(ExtractFileDrive(NormalizedPath));
  if (CompareText(NormalizedPath, DriveRoot) = 0) or
     (CompareText(NormalizedPath, ExtractFileDrive(NormalizedPath)) = 0) then
    Exit;

  AppPath := ExpandConstant('{app}');
  WindowsPath := ExpandConstant('{win}');
  if IsSameOrParentPath(WindowsPath, AppPath) or
     IsSameOrParentPath(Path, AppPath) or
     IsSameOrParentPath(Path, ExpandConstant('{userprofile}')) or
     IsSameOrParentPath(Path, ExpandConstant('{userappdata}')) or
     IsSameOrParentPath(Path, ExpandConstant('{localappdata}')) or
     IsSameOrParentPath(Path, ExpandConstant('{userdocs}')) or
     IsSameOrParentPath(Path, ExpandConstant('{userdesktop}')) or
     IsSameOrParentPath(Path, WindowsPath) or
     IsSameOrParentPath(Path, ExpandConstant('{commonpf}')) or
     IsSameOrParentPath(Path, ExpandConstant('{commonpf64}')) then
    Exit;

  if not IsSameOrParentPath(AppPath, Path) and
     (IsSameOrParentPath(WindowsPath, Path) or
      IsSameOrParentPath(ExpandConstant('{commonpf}'), Path) or
      IsSameOrParentPath(ExpandConstant('{commonpf64}'), Path)) then
    Exit;

  Result := True;
end;

function InitializeUninstall(): Boolean;
var
  VaultPathLines: TArrayOfString;
begin
  Result := True;
  NotesVaultPath := ExpandConstant('{app}\Notes');

  if LoadStringsFromFile(ExpandConstant('{userappdata}\evidence\vault-path.txt'), VaultPathLines) and
     (GetArrayLength(VaultPathLines) > 0) and (VaultPathLines[0] <> '') then
    NotesVaultPath := VaultPathLines[0];

  NotesVaultPath := RemoveBackslashUnlessRoot(ExpandFileName(NotesVaultPath));
  if not IsSafeVaultPath(NotesVaultPath) then begin
    if not UninstallSilent then
      MsgBox(CustomMessage('UninstallVaultBlocked') + #13#10#13#10 + NotesVaultPath,
        mbInformation, MB_OK);
    Exit;
  end;

  if not UninstallSilent and DirExists(NotesVaultPath) then begin
    if MsgBox(
         CustomMessage('UninstallVaultPrompt') + #13#10#13#10 + NotesVaultPath,
         mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      if not DelTree(NotesVaultPath, True, True, True) then
        MsgBox(CustomMessage('UninstallVaultDeleteFailed') + #13#10#13#10 + NotesVaultPath,
          mbError, MB_OK);
  end;
end;

[Dirs]
Name: "{app}\Notes"; Permissions: users-modify

[INI]
Filename: "{app}\initial-language.ini"; Section: "General"; Key: "language"; String: "{code:GetApplicationLanguage}"; Flags: uninsdeleteentry

[Files]
Source: "..\dist\payload\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\evidence"; Filename: "{app}\evidence.exe"
Name: "{autodesktop}\evidence"; Filename: "{app}\evidence.exe"; Tasks: desktopicon

[UninstallDelete]
Type: dirifempty; Name: "{app}\Notes"
Type: files; Name: "{userappdata}\evidence\settings.json"
Type: files; Name: "{userappdata}\evidence\vault-path.txt"
Type: dirifempty; Name: "{userappdata}\evidence"

[Run]
Filename: "{app}\evidence.exe"; Description: "Launch evidence"; Flags: postinstall nowait skipifsilent