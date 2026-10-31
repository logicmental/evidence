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

[Setup]
AppId={{C7EF16BC-1B9B-4A92-8C9E-6BF8A65F2B80}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=evidence
DefaultLanguage=english
ShowLanguageDialog=yes
DefaultDirName={autopf}\evidence
DisableDirPage=no
DefaultGroupName=evidence
PrivilegesRequired=lowest
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir=..\dist
OutputBaseFilename=evidence-setup-{#AppVersion}-windows-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\evidence.exe

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Code]
var
  ApplicationLanguagePage: TInputOptionWizardPage;

procedure InitializeWizard;
begin
  if ActiveLanguage = 'russian' then
    ApplicationLanguagePage := CreateInputOptionPage(wpSelectDir,
      'Язык приложения', 'Выберите язык приложения',
      'Этот язык будет использоваться в интерфейсе evidence.', True, False)
  else
    ApplicationLanguagePage := CreateInputOptionPage(wpSelectDir,
      'Application language', 'Choose the application language',
      'Select the language used by the evidence application.', True, False);
  if ActiveLanguage = 'russian' then
  begin
    ApplicationLanguagePage.Add('Английский');
    ApplicationLanguagePage.Add('Русский');
  end
  else
  begin
    ApplicationLanguagePage.Add('English');
    ApplicationLanguagePage.Add('Russian');
  end;
  if ActiveLanguage = 'russian' then
    ApplicationLanguagePage.SelectedValueIndex := 1
  else
    ApplicationLanguagePage.SelectedValueIndex := 0;
end;

function GetApplicationLanguage(Param: String): String;
begin
  if ApplicationLanguagePage.SelectedValueIndex = 1 then
    Result := 'ru'
  else
    Result := 'en';
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

[Run]
Filename: "{app}\evidence.exe"; Description: "Launch evidence"; Flags: postinstall nowait skipifsilent