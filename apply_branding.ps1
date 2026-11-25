# Empty Client branding - автоматическое применение правок
 $ErrorActionPreference = "Stop"

Write-Host "Apply Empty Client branding..." -ForegroundColor Cyan

# Файл 1: version.h
 $f = "src\game\version.h"
 $c = Get-Content $f -Raw
 $c = $c -replace '#define GAME_NAME "DDNet"', '#define GAME_NAME "Empty Client"'
 $c = $c -replace '#define DDNET_VERSION_NUMBER 20030', '#define ECLIENT_VERSION_NUMBER 10000'
 $c = $c -replace '#define GAME_RELEASE_VERSION_INTERNAL 20\.3', '#define GAME_RELEASE_VERSION_INTERNAL 1.0'
Set-Content $f -Value $c -NoNewline
Write-Host "  OK $f" -ForegroundColor Green

# Файл 2: storage.cpp
 $f = "src\engine\shared\storage.cpp"
 $c = Get-Content $f -Raw
 $c = $c -replace 'fs_storage_path\("DDNet"', 'fs_storage_path("EClient"'
Set-Content $f -Value $c -NoNewline
Write-Host "  OK $f" -ForegroundColor Green

# Файл 3: graphics_threaded.cpp
 $f = "src\engine\client\graphics_threaded.cpp"
 $c = Get-Content $f -Raw
 $c = $c -replace 'm_pBackend->Init\("DDNet Client"', 'm_pBackend->Init("Empty Client"'
Set-Content $f -Value $c -NoNewline
Write-Host "  OK $f" -ForegroundColor Green

# Файл 4: CMakeLists.txt
 $f = "CMakeLists.txt"
 $c = Get-Content $f -Raw
 $c = $c -replace 'project\(DDNet VERSION', 'project(EClient VERSION'
 $c = $c -replace 'set\(CLIENT_EXECUTABLE DDNet CACHE', 'set(CLIENT_EXECUTABLE EClient CACHE'
 $c = $c -replace 'set\(SERVER_EXECUTABLE DDNet-Server CACHE', 'set(SERVER_EXECUTABLE EClient-Server CACHE'
Set-Content $f -Value $c -NoNewline
Write-Host "  OK $f" -ForegroundColor Green

# Файл 5: config_variables.h
 $f = "src\engine\shared\config_variables.h"
 $c = Get-Content $f -Raw
 $c = $c -replace '"ddnet/ddnet"', '"yourusername/eclient"'
Set-Content $f -Value $c -NoNewline
Write-Host "  OK $f" -ForegroundColor Green

# Файл 6: menus.h — добавить SETTINGS_PERSONAL_FLIGHT_ASSISTANT
 $f = "src\game\client\components\menus.h"
 $c = Get-Content $f -Raw
 $c = $c -replace 'SETTINGS_ASSETS,', 'SETTINGS_ASSETS, SETTINGS_PERSONAL_FLIGHT_ASSISTANT,'
Set-Content $f -Value $c -NoNewline
Write-Host "  OK $f" -ForegroundColor Green

# Файл 7: menus_settings.cpp
 $f = "src\game\client\components\menus_settings.cpp"
 $c = Get-Content $f -Raw
 $c = $c -replace '#include <game/client/components/menu_background\.h>', "#include <game/client/components/menu_background.h>`n#include <game/client/components/personal_flight_assistant/personal_flight_assistant.h>"
 $c = $c -replace 'Localize\("Assets"\),', 'Localize("Assets"), Localize("PFA"),'
 $c = $c -replace 'RenderSettingsAssets\(MainView\);', "RenderSettingsAssets(MainView);`n        }`n        else if(g_Config.m_UiSettingsPage == SETTINGS_PERSONAL_FLIGHT_ASSISTANT)`n        {`n                RenderSettingsPersonalFlightAssistant(MainView);"
Set-Content $f -Value $c -NoNewline
Write-Host "  OK $f" -ForegroundColor Green

# Файл 8: gameclient.h
 $f = "src\game\client\gameclient.h"
 $c = Get-Content $f -Raw
 $c = $c -replace '#include "components/race_demo\.h"', "#include `"components/race_demo.h`"`n#include `"components/personal_flight_assistant/personal_flight_assistant.h`""
 $c = $c -replace 'CLocalServer m_LocalServer;', "CLocalServer m_LocalServer;`n`n        pfa::CPersonalFlightAssistant m_PersonalFlightAssistant;"
Set-Content $f -Value $c -NoNewline
Write-Host "  OK $f" -ForegroundColor Green

# Файл 9: gameclient.cpp
 $f = "src\game\client\gameclient.cpp"
 $c = Get-Content $f -Raw
 $c = $c -replace '&m_Censor,', '&m_Censor, &m_PersonalFlightAssistant,'
Set-Content $f -Value $c -NoNewline
Write-Host "  OK $f" -ForegroundColor Green

Write-Host ""
Write-Host "Done!" -ForegroundColor Green
Write-Host "Check launcher files:"
Get-ChildItem src\launcher\*.cpp, src\launcher\*.h | Format-Table Name, Length
Write-Host "Check PFA files:"
Get-ChildItem src\game\client\components\personal_flight_assistant\*.cpp, src\game\client\components\personal_flight_assistant\*.h | Format-Table Name, Length