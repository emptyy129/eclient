/**
 * @file launcher_main_posix.cpp
 * @brief Linux/macOS точка входа лаунчера Empty Client (консольное меню).
 */

#if !defined(CONF_FAMILY_WINDOWS)

#include "launcher.h"

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, const char *argv[])
{
    launcher::CLauncher Launcher;
    Launcher.CheckForUpdates();

    printf("\n");
    printf("=========================================================\n");
    printf("       Empty Client — Personal Flight Assistant         \n");
    printf("=========================================================\n");
    printf("Version %s\n", Launcher.GetCurrentVersion());
    printf("Статус: %s\n\n", Launcher.GetStatusText());

    if(Launcher.LatestRelease().m_aTagName[0])
    {
        printf("Последний релиз: %s — %s\n",
            Launcher.LatestRelease().m_aTagName,
            Launcher.LatestRelease().m_aName);
        printf("\nChangelog:\n%s\n\n", Launcher.LatestRelease().m_aBody);
    }

    while(true)
    {
        printf("--------------------------------------------------------\n");
        printf("  1) Play\n");
        printf("  2) Import Demos (открыть папку)\n");
        printf("  3) Settings\n");
        printf("  4) Check Updates\n");
        printf("  5) Exit\n");
        printf("--------------------------------------------------------\n");
        printf("> ");

        char Buf[16];
        if(!fgets(Buf, sizeof(Buf), stdin)) break;
        int Choice = atoi(Buf);
        printf("\n");

        switch(Choice)
        {
        case 1:
            if(Launcher.LaunchClient()) { printf("Клиент запущен.\n"); return 0; }
            else printf("Ошибка запуска клиента.\n");
            break;
        case 2:
            Launcher.OpenDemosFolder();
            break;
        case 3:
            Launcher.LaunchSettings();
            break;
        case 4:
            Launcher.CheckForUpdates();
            printf("%s\n\n", Launcher.GetStatusText());
            if(Launcher.LatestRelease().m_aBody[0])
                printf("Changelog:\n%s\n\n", Launcher.LatestRelease().m_aBody);
            break;
        case 5:
            return 0;
        default:
            printf("Неизвестная опция.\n");
            break;
        }
    }
    return 0;
}

#endif // !CONF_FAMILY_WINDOWS
