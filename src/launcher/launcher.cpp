/**
 * @file launcher.cpp
 * @brief Реализация логики лаунчера Empty Client (платформонезависимая часть).
 */

#include "launcher.h"

#include <base/mem.h>
#include <base/str.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

namespace launcher {

CLauncher::CLauncher()
{
    m_aCurrentVersion[0] = '\0';
    m_aExePath[0] = '\0';
    m_aDemosDir[0] = '\0';
    m_aStatus[0] = '\0';
    mem_zero(&m_Latest, sizeof(m_Latest));
    str_copy(m_aCurrentVersion, "1.0");
    DetectPaths();
}

CLauncher::~CLauncher() = default;

void CLauncher::DetectPaths()
{
#if defined(CONF_FAMILY_WINDOWS)
    // Получить путь к каталогу, где находится лаунчер
    char aModule[512];
    GetModuleFileNameA(nullptr, aModule, sizeof(aModule));
    char *pSlash = strrchr(aModule, '\\');
    if(pSlash) *pSlash = '\0';
    str_format(m_aExePath, sizeof(m_aExePath), "%s\\EClient.exe", aModule);
    str_format(m_aDemosDir, sizeof(m_aDemosDir), "%s\\demos", aModule);
#else
    // Linux: получить путь к исполняемому файлу через /proc/self/exe
    char aModule[512];
    ssize_t n = readlink("/proc/self/exe", aModule, sizeof(aModule) - 1);
    if(n > 0) { aModule[n] = '\0'; char *pSlash = strrchr(aModule, '/'); if(pSlash) *pSlash = '\0'; }
    else str_copy(aModule, ".");
    str_format(m_aExePath, sizeof(m_aExePath), "%s/EClient", aModule);
    str_format(m_aDemosDir, sizeof(m_aDemosDir), "%s/demos", aModule);
#endif
}

bool CLauncher::LaunchClient()
{
    if(m_aExePath[0] == '\0') return false;
#if defined(CONF_FAMILY_WINDOWS)
    HINSTANCE h = ShellExecuteA(nullptr, "open", m_aExePath, nullptr, nullptr, SW_SHOWDEFAULT);
    return (INT_PTR)h > 32;
#else
    int rc = system((std::string(m_aExePath) + " &").c_str());
    return rc == 0;
#endif
}

bool CLauncher::LaunchSettings()
{
    if(m_aExePath[0] == '\0') return false;
#if defined(CONF_FAMILY_WINDOWS)
    HINSTANCE h = ShellExecuteA(nullptr, "open", m_aExePath, "-s settings", nullptr, SW_SHOWDEFAULT);
    return (INT_PTR)h > 32;
#else
    int rc = system((std::string(m_aExePath) + " -s settings &").c_str());
    return rc == 0;
#endif
}

void CLauncher::OpenDemosFolder()
{
    if(m_aDemosDir[0] == '\0') return;
#if defined(CONF_FAMILY_WINDOWS)
    ShellExecuteA(nullptr, "explore", m_aDemosDir, nullptr, nullptr, SW_SHOWNORMAL);
#else
    system(("xdg-open '" + std::string(m_aDemosDir) + "' &").c_str());
#endif
}

void CLauncher::CheckForUpdates()
{
    str_copy(m_aStatus, "Проверка обновлений...");

    // Используем curl через system() — самый простой кросс-платформенный способ
    // без добавления зависимостей в лаунчер.
    const char *pRepo = "emptyy129/eclient"; // ЗАМЕНИТЕ на ваш GitHub repo
    char aUrl[256];
    str_format(aUrl, sizeof(aUrl),
        "https://api.github.com/repos/%s/releases/latest", pRepo);

#if defined(CONF_FAMILY_WINDOWS)
    char aCmd[1024];
    str_format(aCmd, sizeof(aCmd),
        "curl -s -H \"User-Agent: EClient\" \"%s\" > %%TEMP%%\\eclient_release.json 2>nul", aUrl);
    int rc = system(aCmd);
    char aPath[MAX_PATH];
    GetTempPathA(MAX_PATH, aPath);
    str_append(aPath, "eclient_release.json");
    std::ifstream F(aPath);
#else
    char aCmd[512];
    str_format(aCmd, sizeof(aCmd),
        "curl -s -H 'User-Agent: EClient' '%s' > /tmp/eclient_release.json 2>/dev/null", aUrl);
    int rc = system(aCmd);
    std::ifstream F("/tmp/eclient_release.json");
#endif
    if(rc != 0 || !F.is_open())
    {
        str_copy(m_aStatus, "Не удалось проверить обновления (нет сети?)");
        return;
    }
    std::stringstream SS;
    SS << F.rdbuf();
    F.close();

    if(!ParseReleaseJson(SS.str()))
    {
        str_copy(m_aStatus, "Не удалось разобрать ответ GitHub");
        return;
    }

    // Сравнить с текущей версией
    // Простое сравнение строк — для семантического сравнения нужны sscanf
    if(str_comp(m_Latest.m_aTagName + (m_Latest.m_aTagName[0] == 'v' ? 1 : 0),
                m_aCurrentVersion) > 0)
    {
        m_Latest.m_bAvailable = true;
        str_format(m_aStatus, sizeof(m_aStatus),
            "Доступна новая версия %s (у вас %s)",
            m_Latest.m_aTagName, m_aCurrentVersion);
    }
    else
    {
        m_Latest.m_bAvailable = false;
        str_format(m_aStatus, sizeof(m_aStatus),
            "У вас последняя версия %s", m_aCurrentVersion);
    }
}

bool CLauncher::ParseReleaseJson(const std::string &Body)
{
    // Простейший JSON-парсер: ищем конкретные ключи через строковый поиск.
    // Для production лучше использовать json-parser, но для лаунчера этого
    // достаточно — формат GitHub API стабилен.
    auto FindString = [&Body](const char *pKey, char *pOut, size_t OutSize) -> bool {
        std::string Needle = std::string("\"") + pKey + "\":\"";
        size_t Pos = Body.find(Needle);
        if(Pos == std::string::npos) return false;
        Pos += Needle.size();
        size_t End = Body.find("\"", Pos);
        if(End == std::string::npos) return false;
        size_t Len = End - Pos;
        if(Len >= OutSize) Len = OutSize - 1;
        memcpy(pOut, Body.c_str() + Pos, Len);
        pOut[Len] = '\0';
        return true;
    };

    if(!FindString("tag_name", m_Latest.m_aTagName, sizeof(m_Latest.m_aTagName)))
        return false;
    FindString("name", m_Latest.m_aName, sizeof(m_Latest.m_aName));
    FindString("html_url", m_Latest.m_aUrl, sizeof(m_Latest.m_aUrl));
    // body может содержать escape-последовательности — простая обработка \n
    std::string BodyStr;
    {
        std::string Needle = "\"body\":\"";
        size_t Pos = Body.find(Needle);
        if(Pos != std::string::npos)
        {
            Pos += Needle.size();
            // body заканчивается на ", но может содержать escaped кавычки
            std::string Out;
            while(Pos < Body.size())
            {
                char c = Body[Pos];
                if(c == '\\' && Pos + 1 < Body.size())
                {
                    char n = Body[Pos + 1];
                    if(n == 'n') Out += '\n';
                    else if(n == '"') Out += '"';
                    else if(n == '\\') Out += '\\';
                    else if(n == 'r') Out += '\r';
                    else if(n == 't') Out += '\t';
                    else Out += n;
                    Pos += 2;
                    continue;
                }
                if(c == '"') break;
                Out += c;
                ++Pos;
            }
            size_t Len = Out.size();
            if(Len >= sizeof(m_Latest.m_aBody)) Len = sizeof(m_Latest.m_aBody) - 1;
            memcpy(m_Latest.m_aBody, Out.c_str(), Len);
            m_Latest.m_aBody[Len] = '\0';
        }
    }
    return true;
}

} // namespace launcher
