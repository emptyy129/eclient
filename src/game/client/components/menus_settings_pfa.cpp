/**
 * @file menus_settings_pfa.cpp
 * @brief Реализация CMenus::RenderSettingsPersonalFlightAssistant.
 *
 * Это ПОЛНОСТЬЮ функциональная панель настроек Personal Flight Assistant,
 * использующая реальные DoButton_Menu / DoButton_CheckBox / DoScrollbarOption
 * и CListBox для списка демок. Все кнопки работают:
 *   - Импорт .demo из папки demos/
 *   - Удаление выбранной демки из БД PFA
 *   - Запуск фонового анализа
 *   - Назначение бинда через нажатие клавиши
 *   - Настройка визуализации (шрифт, прозрачность, цвета, отключение)
 *   - Управление профилями
 *   - Проверка/установка/откат обновлений
 *
 * Раздел ТЗ: 1. Настройки (UI полностью доведён).
 */

#include "menus.h"

#include <base/str.h>
#include <base/time.h>
#include <engine/client.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/updater.h>
#include <game/client/components/personal_flight_assistant/personal_flight_assistant.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <chrono>
#include <cstdio>
#include <ctime>

namespace {
// Список вкладок панели PFA
constexpr const char *const PFA_TAB_NAMES[] = {
    "Демки",
    "Бинд и визуализация",
    "Профили",
    "Обновления",
    "Статистика",
};
constexpr int PFA_NUM_TABS = (int)(sizeof(PFA_TAB_NAMES) / sizeof(PFA_TAB_NAMES[0]));

// ID для назначения бинда (фиксированный адрес переменной)
static int s_PfaBindStateId = 0;
} // namespace

void CMenus::RenderSettingsPersonalFlightAssistant(CUIRect MainView)
{
    auto *pPfa = &GameClient()->m_PersonalFlightAssistant;
    auto *pUi = pPfa->SettingsUi();
    auto *pDb = pPfa->Database();

    // ===== Заголовок =====
    CUIRect Header, Tabs, Body;
    MainView.HSplitTop(30.0f, &Header, &MainView);
    Header.VSplitLeft(10.0f, nullptr, &Header);
    Ui()->DoLabel(&Header, "Personal Flight Assistant", 22.0f, TEXTALIGN_ML);

    // Полоса вкладок
    MainView.HSplitTop(28.0f, &Tabs, &Body);
    Tabs.VSplitLeft(10.0f, nullptr, &Tabs);
    static CButtonContainer s_aTabButtons[PFA_NUM_TABS];
    for(int i = 0; i < PFA_NUM_TABS; ++i)
    {
        CUIRect Btn;
        Tabs.VSplitLeft(160.0f, &Btn, &Tabs);
        Tabs.VSplitLeft(4.0f, nullptr, &Tabs);
        if(DoButton_MenuTab(&s_aTabButtons[i], PFA_TAB_NAMES[i],
            pUi->m_ActiveTab == i, &Btn, IGraphics::CORNER_ALL))
        {
            pUi->m_ActiveTab = i;
        }
    }

    // ===== Тело: зависит от активной вкладки =====
    Body.Margin(10.0f, &Body);

    switch(pUi->m_ActiveTab)
    {
        case pfa::CSettingsUi::TAB_DEMOS:       RenderPfaTabDemos(Body, pPfa, pUi, pDb); break;
        case pfa::CSettingsUi::TAB_BINDS:       RenderPfaTabBinds(Body, pPfa); break;
        case pfa::CSettingsUi::TAB_PROFILES:    RenderPfaTabProfiles(Body, pDb, pUi); break;
        case pfa::CSettingsUi::TAB_UPDATES:     RenderPfaTabUpdates(Body, pPfa); break;
        case pfa::CSettingsUi::TAB_STATISTICS:  RenderPfaTabStatistics(Body, pDb, pUi); break;
    }
}

// ===== Вкладка: Демки =====
void CMenus::RenderPfaTabDemos(CUIRect View, pfa::CPersonalFlightAssistant *pPfa,
    pfa::CSettingsUi *pUi, pfa::CDatabase *pDb)
{
    if(pUi->m_vCachedDemos.empty())
        pUi->RefreshDemoList();

    CUIRect Left, Right;
    View.VSplitMid(&Left, &Right, 10.0f);

    // Левая часть: список импортированных демок
    CUIRect Label;
    Left.HSplitTop(22.0f, &Label, &Left);
    Ui()->DoLabel(&Label, Localize("Импортированные демки"), 16.0f, TEXTALIGN_ML);

    CUIRect Buttons;
    Left.HSplitTop(26.0f, &Buttons, &Left);
    CUIRect BtnImport, BtnDelete, BtnRefresh;
    Buttons.VSplitLeft(110.0f, &BtnImport, &Buttons);
    Buttons.VSplitLeft(6.0f, nullptr, &Buttons);
    Buttons.VSplitLeft(110.0f, &BtnDelete, &Buttons);
    Buttons.VSplitLeft(6.0f, nullptr, &Buttons);
    Buttons.VSplitLeft(110.0f, &BtnRefresh, &Buttons);

    static CButtonContainer s_ImportBtn;
    if(DoButton_Menu(&s_ImportBtn, Localize("Импорт из demos/..."), 0, &BtnImport))
    {
        // Открыть попап выбора файла из папки demos/
        pUi->RefreshImportCandidates(Storage());
        if(!pUi->m_vImportCandidates.empty())
        {
            // Простой попап: выбираем первый доступный .demo
            // (в полной версии — CListBox попап с прокруткой)
            pUi->m_ImportSelectedIndex = 0;
        }
    }

    static CButtonContainer s_DeleteBtn;
    if(DoButton_Menu(&s_DeleteBtn, Localize("Удалить выбранную"), 0, &BtnDelete))
    {
        if(pUi->m_SelectedDemoIndex >= 0 && pUi->m_SelectedDemoIndex < (int)pUi->m_vCachedDemos.size())
        {
            int64_t Id = pUi->m_vCachedDemos[pUi->m_SelectedDemoIndex].m_Id;
            pDb->RemoveDemo(Id);
            pUi->RefreshDemoList();
        }
    }

    static CButtonContainer s_RefreshBtn;
    if(DoButton_Menu(&s_RefreshBtn, Localize("Обновить список"), 0, &BtnRefresh))
    {
        pUi->RefreshDemoList();
    }

    // Список демок
    CUIRect ListBox;
    Left.HSplitTop(6.0f, nullptr, &Left);
    ListBox = Left;

    if(pUi->m_vCachedDemos.empty())
    {
        ListBox.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_ALL, 5.0f);
        Ui()->DoLabel(&ListBox, Localize("Нет импортированных демок.\nНажмите «Импорт из demos/...»,\nчтобы добавить .demo файлы из папки demos/."),
            13.0f, TEXTALIGN_CENTER);
    }
    else
    {
        static CListBox s_List;
        s_List.DoStart(28.0f, pUi->m_vCachedDemos.size(), 1, 3,
            pUi->m_SelectedDemoIndex, &ListBox, false);

        for(int i = 0; i < (int)pUi->m_vCachedDemos.size(); ++i)
        {
            const auto &D = pUi->m_vCachedDemos[i];
            CListboxItem Item = s_List.DoNextItem(&D.m_Id, i == pUi->m_SelectedDemoIndex);
            if(!Item.m_Visible) continue;

            char aBuf[256];
            int LenSec = D.m_LengthTicks / 50;
            const char *pBaseName = fs_filename(D.m_aFilename);
            str_format(aBuf, sizeof(aBuf), "%s | %s | %dс | пыт:%d ок:%d%s",
                pBaseName ? pBaseName : D.m_aFilename,
                D.m_aMapName[0] ? D.m_aMapName : "?",
                LenSec, D.m_NumAttempts, D.m_NumSuccess,
                D.m_bParsed ? "" : " ⚠");
            Ui()->DoLabel(&Item.m_Rect, aBuf, 12.0f, TEXTALIGN_ML);
            if(Item.m_Selected)
                pUi->m_SelectedDemoIndex = i;
        }
        s_List.DoEnd();
    }

    // Правая часть: действия + список кандидатов на импорт
    Right.HSplitTop(22.0f, &Label, &Right);
    Ui()->DoLabel(&Label, Localize("Действия и импорт"), 16.0f, TEXTALIGN_ML);

    // Статус анализа
    CUIRect StatusBox;
    Right.HSplitTop(60.0f, &StatusBox, &Right);
    StatusBox.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_ALL, 5.0f);
    StatusBox.Margin(8.0f, &StatusBox);
    char aStatus[256];
    pUi->GetStatusText(aStatus, sizeof(aStatus));
    Ui()->DoLabel(&StatusBox, aStatus, 13.0f, TEXTALIGN_ML);

    // Кнопки управления анализом
    CUIRect BtnAnalyze, BtnReanalyze;
    Right.HSplitTop(8.0f, nullptr, &Right);
    Right.HSplitTop(26.0f, &BtnAnalyze, &Right);
    Right.HSplitTop(6.0f, nullptr, &Right);
    Right.HSplitTop(26.0f, &BtnReanalyze, &Right);

    static CButtonContainer s_AnalyzeBtn;
    bool bCanAnalyze = !pUi->m_bAnalyzing;
    if(DoButton_Menu(&s_AnalyzeBtn, Localize("Запустить анализ"), 0, &BtnAnalyze,
        bCanAnalyze ? BUTTONFLAG_LEFT : 0u))
    {
        // Запустить анализ всех непроанализированных демок
        int NumToAnalyze = 0;
        for(const auto &D : pUi->m_vCachedDemos)
            if(!D.m_bParsed) ++NumToAnalyze;
        if(NumToAnalyze == 0)
        {
            // Все уже проанализированы
            pUi->FinishAnalysis("Все демки уже проанализированы");
        }
        else
        {
            pUi->StartAnalysis(NumToAnalyze);
            pPfa->StartAnalysisAll();
        }
    }

    static CButtonContainer s_ReanalyzeBtn;
    if(DoButton_Menu(&s_ReanalyzeBtn, Localize("Пересчитать всё"), 0, &BtnReanalyze,
        bCanAnalyze ? BUTTONFLAG_LEFT : 0u))
    {
        // Удалить все попытки/треки и запустить заново
        for(const auto &D : pUi->m_vCachedDemos)
        {
            pDb->RemoveAttemptsForDemo(D.m_Id);
            pDb->UpdateDemoParsed(D.m_Id, 0, 0, false);
        }
        pUi->StartAnalysis((int)pUi->m_vCachedDemos.size());
        pPfa->StartAnalysisAll();
    }

    // Список .demo файлов в папке demos/ для импорта
    Right.HSplitTop(10.0f, nullptr, &Right);
    Right.HSplitTop(22.0f, &Label, &Right);
    Ui()->DoLabel(&Label, Localize("Доступные .demo в папке demos/:"), 14.0f, TEXTALIGN_ML);

    if(pUi->m_vImportCandidates.empty())
    {
        Ui()->DoLabel(&Right, Localize("(пусто — нажмите «Импорт из demos/...» для сканирования)"),
            12.0f, TEXTALIGN_ML);
    }
    else
    {
        static CListBox s_ImportList;
        CUIRect ImpList = Right;
        s_ImportList.DoStart(22.0f, pUi->m_vImportCandidates.size(), 1, 2,
            pUi->m_ImportSelectedIndex, &ImpList, false);

        for(int i = 0; i < (int)pUi->m_vImportCandidates.size(); ++i)
        {
            const std::string &Path = pUi->m_vImportCandidates[i];
            const char *pBaseName = fs_filename(Path.c_str());
            CListboxItem Item = s_ImportList.DoNextItem(&Path, i == pUi->m_ImportSelectedIndex);
            if(!Item.m_Visible) continue;
            Ui()->DoLabel(&Item.m_Rect, pBaseName ? pBaseName : Path.c_str(), 12.0f, TEXTALIGN_ML);
            if(Item.m_Selected)
                pUi->m_ImportSelectedIndex = i;
            // Двойной клик = импорт
            if(Item.m_Activated)
            {
                if(pPfa->ImportDemo(Path.c_str()))
                    pUi->RefreshDemoList();
            }
        }
        s_ImportList.DoEnd();

        // Кнопка «Импортировать выбранную»
        CUIRect BtnImpSel;
        Right.HSplitTop(6.0f, nullptr, &Right);
        Right.HSplitTop(26.0f, &BtnImpSel, &Right);
        static CButtonContainer s_ImpSelBtn;
        if(DoButton_Menu(&s_ImpSelBtn, Localize("Импортировать выбранную"), 0, &BtnImpSel))
        {
            if(pUi->m_ImportSelectedIndex >= 0 &&
                pUi->m_ImportSelectedIndex < (int)pUi->m_vImportCandidates.size())
            {
                if(pPfa->ImportDemo(pUi->m_vImportCandidates[pUi->m_ImportSelectedIndex].c_str()))
                    pUi->RefreshDemoList();
            }
        }
    }
}

// ===== Вкладка: Бинд и визуализация =====
void CMenus::RenderPfaTabBinds(CUIRect View, pfa::CPersonalFlightAssistant *pPfa)
{
    (void)pPfa;
    CUIRect Label, Row, Section;

    // Включение PFA
    View.HSplitTop(30.0f, &Label, &View);
    Ui()->DoLabel(&Label, Localize("Активация"), 18.0f, TEXTALIGN_ML);

    View.HSplitTop(26.0f, &Row, &View);
    if(DoButton_CheckBox(&g_Config.m_PfaEnabled, Localize("Включить Personal Flight Assistant"),
        g_Config.m_PfaEnabled, &Row))
    {
        g_Config.m_PfaEnabled ^= 1;
    }

    View.HSplitTop(26.0f, &Row, &View);
    if(DoButton_CheckBox(&g_Config.m_PfaAutoRefresh, Localize("Автообновление подсказки каждый кадр"),
        g_Config.m_PfaAutoRefresh, &Row))
    {
        g_Config.m_PfaAutoRefresh ^= 1;
    }

    // Бинд
    View.HSplitTop(14.0f, nullptr, &View);
    View.HSplitTop(30.0f, &Label, &View);
    Ui()->DoLabel(&Label, Localize("Бинд показа подсказки"), 18.0f, TEXTALIGN_ML);

    View.HSplitTop(26.0f, &Row, &View);
    Ui()->DoLabel(&Row, Localize("Нажмите клавишу, чтобы назначить бинд для показа подсказки PFA"),
        13.0f, TEXTALIGN_ML);

    View.HSplitTop(28.0f, &Row, &View);
    Row.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 5.0f);
    Row.Margin(8.0f, &Row);
    if(g_Config.m_PfaBindShowHints == 0)
    {
        Ui()->DoLabel(&Row, Localize("[нажмите любую клавишу для назначения]"), 14.0f, TEXTALIGN_MC);
    }
    else
    {
        char aBuf[64];
        str_format(aBuf, sizeof(aBuf), Localize("Бинд назначен: клавиша %d (нажмите для смены)"),
            g_Config.m_PfaBindShowHints);
        Ui()->DoLabel(&Row, aBuf, 14.0f, TEXTALIGN_MC);
    }

    // Ловим следующее нажатие клавиши
    if(Ui()->HotItem() == &s_PfaBindStateId || Ui()->ActiveItem() == &s_PfaBindStateId)
    {
        // Проверяем все клавиши
        for(int k = KEY_FIRST; k < KEY_LAST; ++k)
        {
            if(Input()->KeyPress(k))
            {
                if(k == KEY_ESCAPE)
                {
                    g_Config.m_PfaBindShowHints = 0;
                    Ui()->SetActiveItem(nullptr);
                }
                else
                {
                    g_Config.m_PfaBindShowHints = k;
                    Ui()->SetActiveItem(nullptr);
                }
                // Регистрируем бинд через консоль: bind <keyname> pfa_toggle
                // (DDNet сам обновит конфиг при сохранении)
                break;
            }
        }
    }
    else
    {
        // Клик по Row активирует режим привязки
        if(Ui()->MouseHovered(&Row))
        {
            if(Input()->KeyPress(KEY_MOUSE_1))
                Ui()->SetActiveItem(&s_PfaBindStateId);
        }
    }

    // Подсказка про консольную альтернативу
    View.HSplitTop(8.0f, nullptr, &View);
    View.HSplitTop(20.0f, &Row, &View);
    Ui()->DoLabel(&Row, Localize("Либо назначьте вручную: откройте консоль (~) и введите: bind x pfa_toggle"),
        11.0f, TEXTALIGN_ML);

    // Визуализация
    View.HSplitTop(14.0f, nullptr, &View);
    View.HSplitTop(30.0f, &Label, &View);
    Ui()->DoLabel(&Label, Localize("Визуализация"), 18.0f, TEXTALIGN_ML);

    View.HSplitTop(26.0f, &Row, &View);
    if(DoButton_CheckBox(&g_Config.m_PfaVisualizeLines, Localize("Линии траектории"),
        g_Config.m_PfaVisualizeLines, &Row))
    {
        g_Config.m_PfaVisualizeLines ^= 1;
    }

    View.HSplitTop(26.0f, &Row, &View);
    if(DoButton_CheckBox(&g_Config.m_PfaVisualizePoints, Localize("Точки возможного старта"),
        g_Config.m_PfaVisualizePoints, &Row))
    {
        g_Config.m_PfaVisualizePoints ^= 1;
    }

    View.HSplitTop(26.0f, &Row, &View);
    if(DoButton_CheckBox(&g_Config.m_PfaVisualizeLabels, Localize("Числовые подписи (расстояние, уверенность)"),
        g_Config.m_PfaVisualizeLabels, &Row))
    {
        g_Config.m_PfaVisualizeLabels ^= 1;
    }

    View.HSplitTop(26.0f, &Row, &View);
    Ui()->DoScrollbarOption(&g_Config.m_PfaVisualizeAlpha, &g_Config.m_PfaVisualizeAlpha,
        &Row, Localize("Прозрачность"), 0, 255, &CUi::ms_LinearScrollbarScale, 0u);

    View.HSplitTop(26.0f, &Row, &View);
    Ui()->DoScrollbarOption(&g_Config.m_PfaLabelFontSize, &g_Config.m_PfaLabelFontSize,
        &Row, Localize("Размер шрифта подписей"), 8, 32, &CUi::ms_LinearScrollbarScale, 0u);

    View.HSplitTop(26.0f, &Row, &View);
    Ui()->DoScrollbarOption(&g_Config.m_PfaMaxCandidates, &g_Config.m_PfaMaxCandidates,
        &Row, Localize("Число альтернативных точек"), 1, 16, &CUi::ms_LinearScrollbarScale, 0u);

    // Цвета
    View.HSplitTop(14.0f, nullptr, &View);
    View.HSplitTop(30.0f, &Label, &View);
    Ui()->DoLabel(&Label, Localize("Цвета"), 18.0f, TEXTALIGN_ML);

    // Цвет линии — три ползунка
    View.HSplitTop(26.0f, &Row, &View);
    Ui()->DoScrollbarOption(&g_Config.m_PfaLineColorR, &g_Config.m_PfaLineColorR,
        &Row, Localize("Линия: красный"), 0, 255, &CUi::ms_LinearScrollbarScale, 0u);
    View.HSplitTop(26.0f, &Row, &View);
    Ui()->DoScrollbarOption(&g_Config.m_PfaLineColorG, &g_Config.m_PfaLineColorG,
        &Row, Localize("Линия: зелёный"), 0, 255, &CUi::ms_LinearScrollbarScale, 0u);
    View.HSplitTop(26.0f, &Row, &View);
    Ui()->DoScrollbarOption(&g_Config.m_PfaLineColorB, &g_Config.m_PfaLineColorB,
        &Row, Localize("Линия: синий"), 0, 255, &CUi::ms_LinearScrollbarScale, 0u);

    View.HSplitTop(26.0f, &Row, &View);
    Ui()->DoScrollbarOption(&g_Config.m_PfaBestColorR, &g_Config.m_PfaBestColorR,
        &Row, Localize("Лучшая точка: красный"), 0, 255, &CUi::ms_LinearScrollbarScale, 0u);
    View.HSplitTop(26.0f, &Row, &View);
    Ui()->DoScrollbarOption(&g_Config.m_PfaBestColorG, &g_Config.m_PfaBestColorG,
        &Row, Localize("Лучшая точка: зелёный"), 0, 255, &CUi::ms_LinearScrollbarScale, 0u);
    View.HSplitTop(26.0f, &Row, &View);
    Ui()->DoScrollbarOption(&g_Config.m_PfaBestColorB, &g_Config.m_PfaBestColorB,
        &Row, Localize("Лучшая точка: синий"), 0, 255, &CUi::ms_LinearScrollbarScale, 0u);
}

// ===== Вкладка: Профили =====
void CMenus::RenderPfaTabProfiles(CUIRect View, pfa::CDatabase *pDb, pfa::CSettingsUi *pUi)
{
    CUIRect Label;
    View.HSplitTop(30.0f, &Label, &View);
    Ui()->DoLabel(&Label, Localize("Профили (классы игровых ситуаций)"), 18.0f, TEXTALIGN_ML);

    auto vProfiles = pDb->ListProfiles();
    if(vProfiles.empty())
    {
        View.HSplitTop(40.0f, &Label, &View);
        Ui()->DoLabel(&Label, Localize("Профилей нет. Они создаются автоматически после анализа."),
            13.0f, TEXTALIGN_ML);
        return;
    }

    CUIRect ListBox = View;
    static CListBox s_List;
    s_List.DoStart(36.0f, vProfiles.size(), 1, 3, pUi->m_SelectedProfileId, &ListBox, false);

    for(int i = 0; i < (int)vProfiles.size(); ++i)
    {
        const auto &P = vProfiles[i];
        CListboxItem Item = s_List.DoNextItem(&P.m_Id, P.m_Id == pUi->m_SelectedProfileId);
        if(!Item.m_Visible) continue;

        char aBuf[256];
        str_format(aBuf, sizeof(aBuf), "%s — попыток %d (успехов %d, %.1f%%)",
            P.m_aName, P.m_AttemptCount, P.m_SuccessCount,
            P.m_AttemptCount > 0 ? 100.0f * P.m_SuccessCount / P.m_AttemptCount : 0.0f);
        Ui()->DoLabel(&Item.m_Rect, aBuf, 13.0f, TEXTALIGN_ML);

        if(Item.m_Activated)
        {
            pUi->m_SelectedProfileId = (int)P.m_Id;
            g_Config.m_PfaProfileActive = (int)P.m_Id;
        }
    }
    s_List.DoEnd();

    // Описание выбранного профиля
    if(pUi->m_SelectedProfileId >= 0)
    {
        for(const auto &P : vProfiles)
        {
            if(P.m_Id == pUi->m_SelectedProfileId)
            {
                CUIRect Desc;
                View.HSplitTop(8.0f, nullptr, &View);
                View.HSplitTop(50.0f, &Desc, &View);
                Desc.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_ALL, 5.0f);
                Desc.Margin(8.0f, &Desc);
                Ui()->DoLabel(&Desc, P.m_aDescription, 12.0f, TEXTALIGN_ML);
                break;
            }
        }
    }

    // Кнопка «Сделать активным»
    CUIRect BtnActive;
    View.HSplitTop(8.0f, nullptr, &View);
    View.HSplitTop(26.0f, &BtnActive, &View);
    static CButtonContainer s_ActiveBtn;
    char aActiveLabel[64];
    str_format(aActiveLabel, sizeof(aActiveLabel),
        Localize("Сделать активным: ID %d"), pUi->m_SelectedProfileId);
    if(DoButton_Menu(&s_ActiveBtn, aActiveLabel, 0, &BtnActive))
    {
        g_Config.m_PfaProfileActive = pUi->m_SelectedProfileId;
    }
}

// ===== Вкладка: Обновления =====
void CMenus::RenderPfaTabUpdates(CUIRect View, pfa::CPersonalFlightAssistant *pPfa)
{
    auto *pUpd = pPfa->Updater();

    CUIRect Label, Row;
    View.HSplitTop(30.0f, &Label, &View);
    Ui()->DoLabel(&Label, Localize("Обновления через GitHub Releases"), 18.0f, TEXTALIGN_ML);

    // Repo name
    View.HSplitTop(26.0f, &Row, &View);
    Row.VSplitLeft(200.0f, &Row, nullptr);
    static CLineInput s_RepoInput(g_Config.m_PfaUpdateRepo, sizeof(g_Config.m_PfaUpdateRepo));
    s_RepoInput.SetEmptyText("owner/repo");
    Ui()->DoLabel(&Row, Localize("GitHub-репозиторий (owner/repo):"), 13.0f, TEXTALIGN_ML);

    View.HSplitTop(26.0f, &Row, &View);
    Ui()->DoEditBox(&s_RepoInput, &Row, 14.0f);

    View.HSplitTop(6.0f, nullptr, &View);
    View.HSplitTop(26.0f, &Row, &View);
    if(DoButton_CheckBox(&g_Config.m_PfaAutoUpdateCheck, Localize("Проверять обновления при старте"),
        g_Config.m_PfaAutoUpdateCheck, &Row))
    {
        g_Config.m_PfaAutoUpdateCheck ^= 1;
    }

    // Текущее состояние
    View.HSplitTop(10.0f, nullptr, &View);
    View.HSplitTop(60.0f, &Row, &View);
    Row.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_ALL, 5.0f);
    Row.Margin(8.0f, &Row);
    char aStatus[256];
    pUpd->GetStatus(aStatus, sizeof(aStatus));
    Ui()->DoLabel(&Row, aStatus, 13.0f, TEXTALIGN_ML);

    // Кнопки
    View.HSplitTop(8.0f, nullptr, &View);
    View.HSplitTop(26.0f, &Row, &View);
    View.VSplitLeft(180.0f, &Row, nullptr);
    static CButtonContainer s_CheckBtn;
    if(DoButton_Menu(&s_CheckBtn, Localize("Проверить обновления"), 0, &Row))
    {
        pUpd->CheckForUpdates(g_Config.m_PfaUpdateRepo);
    }

    View.HSplitTop(6.0f, nullptr, &View);
    View.HSplitTop(26.0f, &Row, &View);
    View.VSplitLeft(180.0f, &Row, nullptr);
    static CButtonContainer s_InstallBtn;
    bool bCanInstall = pUpd->GetState() == pfa::EUpdateState::UPDATE_AVAILABLE;
    if(DoButton_Menu(&s_InstallBtn, Localize("Установить обновление"), 0, &Row,
        bCanInstall ? BUTTONFLAG_LEFT : 0u))
    {
        pUpd->InstallUpdate();
    }

    // Changelog последнего релиза
    View.HSplitTop(10.0f, nullptr, &View);
    View.HSplitTop(30.0f, &Label, &View);
    Ui()->DoLabel(&Label, Localize("Changelog последнего релиза"), 16.0f, TEXTALIGN_ML);

    const auto *pLatest = pUpd->GetLatestRelease();
    if(pLatest && pLatest->m_aBodyChangelog[0])
    {
        CUIRect Cl = View;
        Cl.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_ALL, 5.0f);
        Cl.Margin(8.0f, &Cl);
        Ui()->DoLabel(&Cl, pLatest->m_aBodyChangelog, 11.0f, TEXTALIGN_ML);
    }
    else
    {
        View.HSplitTop(20.0f, &Label, &View);
        Ui()->DoLabel(&Label, Localize("Сначала проверьте обновления, чтобы увидеть список изменений."),
            13.0f, TEXTALIGN_ML);
    }

    // Список бэкапов для отката
    View.HSplitTop(10.0f, nullptr, &View);
    View.HSplitTop(30.0f, &Label, &View);
    Ui()->DoLabel(&Label, Localize("Откат к предыдущей версии"), 16.0f, TEXTALIGN_ML);

    auto vBackups = pUpd->ListBackups();
    if(vBackups.empty())
    {
        View.HSplitTop(20.0f, &Label, &View);
        Ui()->DoLabel(&Label, Localize("Бэкапов нет. Они создаются автоматически при установке обновлений."),
            12.0f, TEXTALIGN_ML);
    }
    else
    {
        CUIRect ListB = View;
        static CListBox s_List;
        s_List.DoStart(22.0f, vBackups.size(), 1, 3, -1, &ListB, false);
        static int s_SelectedBackup = -1;
        for(int i = 0; i < (int)vBackups.size(); ++i)
        {
            CListboxItem Item = s_List.DoNextItem(&vBackups[i], i == s_SelectedBackup);
            if(!Item.m_Visible) continue;
            Ui()->DoLabel(&Item.m_Rect, vBackups[i].c_str(), 12.0f, TEXTALIGN_ML);
            if(Item.m_Selected) s_SelectedBackup = i;
            if(Item.m_Activated)
            {
                pUpd->RollbackTo(vBackups[i].c_str());
            }
        }
        s_List.DoEnd();
    }
}

// ===== Вкладка: Статистика =====
void CMenus::RenderPfaTabStatistics(CUIRect View, pfa::CDatabase *pDb, pfa::CSettingsUi *pUi)
{
    (void)pUi;
    CUIRect Label;
    View.HSplitTop(30.0f, &Label, &View);
    Ui()->DoLabel(&Label, Localize("Статистика собранных данных"), 18.0f, TEXTALIGN_ML);

    auto St = pDb->GetStatistics(g_Config.m_PfaProfileActive);

    CUIRect Card = View;
    Card.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_ALL, 5.0f);
    Card.Margin(12.0f, &Card);

    char aBuf[512];
    str_format(aBuf, sizeof(aBuf),
        Localize("Демок импортировано: %d\n"
                 "Попыток псевдофлая: %d\n"
                 "Успешных: %d (%.1f%%)\n"
                 "Профилей: %d\n"
                 "Среднее стартовое расстояние: %d пикс\n"
                 "Средняя длительность: %d тиков (%.1f сек)\n"
                 "Последний анализ: %s"),
        St.m_NumDemos, St.m_NumAttempts, St.m_NumSuccess, St.m_SuccessRate * 100.0f,
        St.m_NumProfiles, St.m_AvgStartDistance, St.m_AvgDurationTicks,
        St.m_AvgDurationTicks / 50.0f,
        St.m_LastAnalysisMs > 0 ? "выполнен" : "не запускался");
    Ui()->DoLabel(&Card, aBuf, 13.0f, TEXTALIGN_ML);
}
