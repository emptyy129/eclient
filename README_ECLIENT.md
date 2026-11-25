# Empty Client (E-Client)

Персональный форк DDNet-клиента с модулем **Personal Flight Assistant (PFA)** —
система анализа демок и автоматического определения оптимальной точки
начала псевдофлая (Aled).

## Что это

**Empty Client** — отдельное приложение на базе исходного кода DDNet.
Это не патч к DDNet, а полноценный форк со своим:

- Именем и брендингом (окно «Empty Client», конфиг `%APPDATA%/EClient/`)
- Исполняемым файлом (`EClient.exe` на Windows, `EClient` на Linux)
- Лаунчером (`EClientLauncher.exe`) — отдельным приложением с кнопками
  Play / Import Demos / Settings / Check Updates / Exit
- Встроенным PFA-модулем
- Системой автообновления через GitHub Releases
- Сценариями сборки релизов (Windows .bat и Linux .sh)
- CI/CD через GitHub Actions

## Состав

```
ddnet/                              # корень репозитория Empty Client
├── src/game/version.h              # GAME_NAME="Empty Client", version 1.0
├── src/game/client/components/
│   ├── personal_flight_assistant/  # PFA-модуль (19 файлов, ~4400 строк)
│   ├── menus_settings_pfa.cpp      # панель настроек PFA с реальными кнопками
│   ├── menus.h                     # интеграция вкладки SETTINGS_PERSONAL_FLIGHT_ASSISTANT
│   └── menus_settings.cpp          # диспетчер вкладок
├── src/engine/shared/
│   ├── config_variables.h          # 25 PFA-переменных + BRAND_REPO
│   └── storage.cpp                 # CONF_STORAGE_PATH="EClient" (отдельный конфиг)
├── src/engine/client/
│   ├── graphics_threaded.cpp       # заголовок окна "Empty Client"
│   └── ...
├── src/launcher/                   # ОТДЕЛЬНЫЙ лаунчер (4 файла)
│   ├── launcher.h
│   ├── launcher.cpp                # общая логика (HTTP через curl)
│   ├── launcher_main_win32.cpp     # Win32 диалог с кнопками
│   └── launcher_main_posix.cpp     # консольное меню Linux/Mac
├── CMakeLists.txt                  # project(EClient), target EClientLauncher
├── build_release.bat               # сборка Windows релиза + zip
├── build_release.sh                # сборка Linux релиза + tar.gz
└── .github/workflows/build_release.yml  # CI/CD для GitHub Releases
```

## Сборка на Windows

### Быстро (через скрипт)

1. Установите один раз:
   - **Visual Studio 2022** с компонентом «C++ Build Tools»
   - **vcpkg** в `C:\vcpkg`:
     ```cmd
     git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
     C:\vcpkg\bootstrap-vcpkg.bat
     C:\vcpkg\vcpkg install sdl2 openssl freetype opus opusfile libpng zlib sqlite3 --triplet x64-windows
     ```
   - **Rust**: https://rustup.rs/
   - **CMake** и **Git** (добавить в PATH)

2. Запустите:
   ```cmd
   build_release.bat
   ```

3. Готовый архив: `release\EClient-1.0-windows-x86_64.zip`

### Вручную

```cmd
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DAUTOUPDATE=ON -DVIDEORECORDER=OFF -DSTEAM=OFF -DDISCORD=OFF -DANTIBOT=OFF -DVULKAN=OFF
cmake --build build --config Release --target game-client EClientLauncher
```

Исполняемые файлы появятся в `build\Release\EClient.exe` и `build\Release\EClientLauncher.exe`.

## Сборка на Linux

```bash
sudo apt install cmake g++ git libsdl2-dev libfreetype-dev \
  libsqlite3-dev libpng-dev libopus-dev libogg-dev \
  libcurl4-openssl-dev libssl-dev zlib1g-dev
curl https://sh.rustup.rs -sSf | sh
./build_release.sh
```

Готовый архив: `release/EClient-1.0-linux-x86_64.tar.gz`

## Использование

1. Распакуйте архив в любую папку
2. Запустите **EClientLauncher.exe** (на Windows) или `./EClientLauncher` (на Linux)
3. Положите свои .demo-файлы в папку `demos/` (рядом с EClient.exe)
4. Нажмите **«Check Updates»** — увидите changelog последнего релиза
5. Нажмите **«Play»** — запустится основной клиент
6. Зайдите в **Settings → PFA** (последняя вкладка справа)
7. Включите **«Включить Personal Flight Assistant»**
8. Перейдите на вкладку **«Демки»**
9. Нажмите **«Импорт из demos/...»** — увидите список ваших демок
10. Двойным кликом импортируйте нужные
11. Нажмите **«Запустить анализ»**
12. Перейдите на вкладку **«Бинд и визуализация»**
13. Назначьте клавишу показа подсказки (клик по полю + нажатие)
14. Настройте цвета/прозрачность под себя
15. Зайдите на любой community-сервер DDNet (НЕ официальный — antibot)
16. В игре держите дамми рядом и зажмите назначенную клавишу

## Публикация обновлений

1. Создайте репозиторий на GitHub (например `yourusername/eclient`)
2. Запушьте туда исходный код
3. Замените `yourusername/eclient` в:
   - `src/launcher/launcher.cpp` (строка ~97)
   - `src/engine/shared/config_variables.h` (переменная `pfa_update_repo`)
4. Создайте тег: `git tag v1.1.0 && git push origin v1.1.0`
5. GitHub Actions автоматически соберёт Windows + Linux версии
   и опубликует их в Releases
6. Пользователи получат уведомление в лаунчере при следующем запуске

## Совместимость с DDNet

- **Протокол**: 100% совместим с DDNet 0.6 (используется тот же `GAME_NETVERSION`)
- **Серверы**: можно играть на всех DDNet-серверах (community)
- **Демки**: читает стандартные `.demo` файлы DDNet
- **Конфиг**: отдельный (`%APPDATA%/EClient/`) — не пересекается с вашим обычным DDNet
- **Внимание**: на **официальных** серверах DDNet (ddnet.org) модифицированный
  клиент может быть заблокирован antibot-системой. Используйте только community-сервера
  или локальный сервер.

## Структура PFA-модуля

Подробно описано в `src/game/client/components/personal_flight_assistant/README.md`.

Кратко:
- 19 файлов, ~4400 строк C++ кода
- SQLite-хранилище с миграциями
- Парсер демок через `CDemoPlayer`
- Детектор псевдофлая по `CNetObj_CharacterCore`
- Статистическая модель (гауссианы признаков)
- kNN-предиктор оптимальной точки
- Визуализация линий/точек/подписей с настройками
- Система обновлений через GitHub Releases с SHA256/бэкапом/откатом
- Полноценный UI в Settings → PFA с 5 вкладками

## Лицензия

Исходный код DDNet распространяется под лицензией, указанной в файле
`licence.txt` в корне репозитория. Модуль PFA распространяется на тех же
условиях.

## Контакты и обновления

Обновления распространяются через GitHub Releases репозитория `yourusername/eclient`.
Локальные бэкапы после обновлений хранятся в `%APPDATA%/EClient/personal_flight_assistant/backups/`.
Откат к предыдущей версии — через Settings → PFA → Updates → Rollback.
