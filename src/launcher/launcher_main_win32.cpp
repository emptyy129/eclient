/**
 * @file launcher_main_win32.cpp
 * @brief Win32-точка входа лаунчера Empty Client.
 *
 * Создаёт простое диалоговое окно с кнопками:
 *   - Play (запустить EClient.exe)
 *   - Import Demos (открыть папку demos/)
 *   - Settings (запустить клиент в режиме настроек)
 *   - Check Updates (проверить обновления)
 *   - Exit
 *
 * А также текстовое поле для отображения статуса и changelog'а.
 */

#if defined(CONF_FAMILY_WINDOWS)

#include "launcher.h"

#include <windows.h>
#include <commctrl.h>

#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

// Идентификаторы контролов
enum EControlId {
    IDC_TITLE = 100,
    IDC_VERSION_LABEL,
    IDC_STATUS_LABEL,
    IDC_CHANGELOG_TITLE,
    IDC_CHANGELOG_TEXT,
    IDC_PLAY_BTN,
    IDC_IMPORT_BTN,
    IDC_SETTINGS_BTN,
    IDC_UPDATE_BTN,
    IDC_EXIT_BTN,
};

static launcher::CLauncher *g_pLauncher = nullptr;

static HFONT g_hFontTitle = nullptr;
static HFONT g_hFontNormal = nullptr;
static HFONT g_hFontMono = nullptr;

static void CreateFonts(HWND hwnd)
{
    g_hFontTitle = CreateFontA(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_hFontNormal = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_hFontMono = CreateFontA(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_MODERN, "Consolas");
}

static HWND CreateButton(HWND hwndParent, const char *pText, int x, int y, int w, int h, int id)
{
    HWND h = CreateWindowA("BUTTON", pText, WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        x, y, w, h, hwndParent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageA(h, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    return h;
}

static HWND CreateLabel(HWND hwndParent, const char *pText, int x, int y, int w, int h, int id, HFONT hFont)
{
    HWND h = CreateWindowA("STATIC", pText, WS_VISIBLE | WS_CHILD | SS_LEFT,
        x, y, w, h, hwndParent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageA(h, WM_SETFONT, (WPARAM)hFont, TRUE);
    return h;
}

static HWND CreateEditMultiLine(HWND hwndParent, const char *pText, int x, int y, int w, int h, int id)
{
    HWND h = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", pText,
        WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        x, y, w, h, hwndParent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageA(h, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);
    return h;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
    case WM_CREATE:
    {
        CreateFonts(hwnd);

        // Заголовок
        CreateLabel(hwnd, "Empty Client", 20, 20, 500, 40, IDC_TITLE, g_hFontTitle);

        // Версия
        char aVer[64];
        snprintf(aVer, sizeof(aVer), "Version %s (Personal Flight Assistant)",
            g_pLauncher ? g_pLauncher->GetCurrentVersion() : "1.0");
        CreateLabel(hwnd, aVer, 20, 65, 500, 20, IDC_VERSION_LABEL, g_hFontNormal);

        // Статус
        CreateLabel(hwnd, g_pLauncher ? g_pLauncher->GetStatusText() : "",
            20, 90, 500, 20, IDC_STATUS_LABEL, g_hFontNormal);

        // Changelog
        CreateLabel(hwnd, "Changelog:", 20, 120, 500, 20, IDC_CHANGELOG_TITLE, g_hFontNormal);
        const char *pBody = g_pLauncher && g_pLauncher->LatestRelease().m_aTagName[0] ?
            g_pLauncher->LatestRelease().m_aBody :
            "Нажмите «Check Updates», чтобы получить список изменений последнего релиза.";
        CreateEditMultiLine(hwnd, pBody, 20, 145, 740, 200, IDC_CHANGELOG_TEXT);

        // Кнопки
        int BtnY = 360;
        CreateButton(hwnd, "▶  Play", 20, BtnY, 180, 50, IDC_PLAY_BTN);
        CreateButton(hwnd, "Import Demos", 210, BtnY, 140, 50, IDC_IMPORT_BTN);
        CreateButton(hwnd, "Settings", 360, BtnY, 120, 50, IDC_SETTINGS_BTN);
        CreateButton(hwnd, "Check Updates", 490, BtnY, 140, 50, IDC_UPDATE_BTN);
        CreateButton(hwnd, "Exit", 640, BtnY, 120, 50, IDC_EXIT_BTN);

        // Подсказка внизу
        CreateLabel(hwnd,
            "E-Client — форк DDNet с Personal Flight Assistant. "
            "Использует те же серверы и протокол, что и DDNet 0.6.",
            20, 425, 740, 30, 9999, g_hFontNormal);

        return 0;
    }
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        switch(id)
        {
        case IDC_PLAY_BTN:
            if(g_pLauncher->LaunchClient())
                PostQuitMessage(0); // закрыть лаунчер после запуска клиента
            else
                MessageBoxA(hwnd, "Не удалось запустить EClient.exe",
                    "Ошибка", MB_OK | MB_ICONERROR);
            return 0;
        case IDC_IMPORT_BTN:
            g_pLauncher->OpenDemosFolder();
            return 0;
        case IDC_SETTINGS_BTN:
            g_pLauncher->LaunchSettings();
            return 0;
        case IDC_UPDATE_BTN:
        {
            g_pLauncher->CheckForUpdates();
            HWND hStatus = GetDlgItem(hwnd, IDC_STATUS_LABEL);
            SetWindowTextA(hStatus, g_pLauncher->GetStatusText());
            HWND hLog = GetDlgItem(hwnd, IDC_CHANGELOG_TEXT);
            SetWindowTextA(hLog, g_pLauncher->LatestRelease().m_aBody[0] ?
                g_pLauncher->LatestRelease().m_aBody :
                "Changelog недоступен");
            if(g_pLauncher->LatestRelease().m_bAvailable)
            {
                int rc = MessageBoxA(hwnd,
                    (std::string("Доступна новая версия ") +
                     g_pLauncher->LatestRelease().m_aTagName +
                     ". Открыть страницу скачивания?").c_str(),
                    "Обновление доступно",
                    MB_YESNO | MB_ICONQUESTION);
                if(rc == IDYES)
                {
                    ShellExecuteA(nullptr, "open",
                        g_pLauncher->LatestRelease().m_aUrl, nullptr, nullptr, SW_SHOWNORMAL);
                }
            }
            return 0;
        }
        case IDC_EXIT_BTN:
            PostQuitMessage(0);
            return 0;
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
    {
        // Подсветка заголовка
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        if(GetDlgCtrlID(hCtrl) == IDC_TITLE)
        {
            SetTextColor(hdc, RGB(0, 180, 100));
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }
        break;
    }
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;

    // Инициализация Common Controls (для современных стилей кнопок)
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    launcher::CLauncher Launcher;
    g_pLauncher = &Launcher;

    // Проверка обновлений при старте (быстрая — через curl)
    Launcher.CheckForUpdates();

    // Регистрация класса окна
    WNDCLASSEXA wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "EClientLauncher";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExA(&wc);

    // Создание окна
    HWND hwnd = CreateWindowExA(
        WS_EX_APPWINDOW,
        "EClientLauncher",
        "Empty Client — Launcher",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 500,
        nullptr, nullptr, hInstance, nullptr);

    if(!hwnd)
    {
        MessageBoxA(nullptr, "Не удалось создать окно лаунчера",
            "Ошибка", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Цикл сообщений
    MSG msg;
    while(GetMessage(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

#endif // CONF_FAMILY_WINDOWS
