/**
 * @file launcher.h
 * @brief Заголовок лаунчера Empty Client.
 *
 * Лаунчер — отдельное приложение, запускаемое перед основным клиентом.
 * Отвечает за:
 *   - Проверку обновлений через GitHub Releases
 *   - Отображение changelog последнего релиза
 *   - Кнопки: Play, Import Demos, Settings, Exit
 *   - Запуск EClient.exe с аргументами
 *
 * На Windows — диалоговое окно Win32 API.
 * На Linux — консольное меню.
 */

#ifndef LAUNCHER_LAUNCHER_H
#define LAUNCHER_LAUNCHER_H

#include <string>
#include <vector>

namespace launcher {

struct SRelease {
    char m_aTagName[32];
    char m_aName[128];
    char m_aBody[4096];
    char m_aUrl[256];
    bool m_bAvailable;
};

class CLauncher {
public:
    CLauncher();
    ~CLauncher();

    /** Запустить лаунчер (блокирующий вызов — открывает окно). */
    int Run(int argc, const char *argv[]);

    /** Проверить обновления через GitHub API (синхронно). */
    void CheckForUpdates();

    /** Запустить основной клиент EClient.exe. */
    bool LaunchClient();

    /** Открыть папку demos/ в файловом менеджере. */
    void OpenDemosFolder();

    /** Запустить клиент с переходом в настройки. */
    bool LaunchSettings();

    const SRelease &LatestRelease() const { return m_Latest; }
    const char *GetCurrentVersion() const { return m_aCurrentVersion; }
    const char *GetStatusText() const { return m_aStatus; }

private:
    char m_aCurrentVersion[32];
    char m_aExePath[512];
    char m_aDemosDir[512];
    char m_aStatus[256];
    SRelease m_Latest;

    /** Найти исполняемый файл клиента рядом с лаунчером. */
    void DetectPaths();

    /** Парсинг JSON релиза (минимальная реализация). */
    bool ParseReleaseJson(const std::string &Body);
};

} // namespace launcher

#endif // LAUNCHER_LAUNCHER_H
