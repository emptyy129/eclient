/**
 * @file pfa_settings_ui.h
 * @brief Держатель состояния UI Personal Flight Assistant.
 *
 * ВАЖНО: рендеринг UI выполняется в CMenus::RenderSettingsPersonalFlightAssistant
 * (файл menus_settings_pfa.cpp), потому что только CMenus имеет доступ
 * к своим защищённым методам DoButton_Menu / DoButton_CheckBox / Ui()->...
 * Этот класс хранит только состояние между кадрами: выбранную демку,
 * прогресс анализа, фильтр поиска и т.д.
 *
 * Раздел ТЗ: 1. Настройки.
 */

#ifndef GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_SETTINGS_UI_H
#define GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_SETTINGS_UI_H

#include "pfa_types.h"
#include "pfa_database.h"

#include <string>
#include <vector>

namespace pfa {

class CSettingsUi {
public:
    // Состояние списка импортированных демок (в БД)
    int m_SelectedDemoIndex;
    int m_ScrollDemoList;
    char m_aSearchFilter[64];

    // Состояние списка демок в папке demos (для импорта)
    int m_ImportSelectedIndex;
    std::vector<std::string> m_vImportCandidates; // пути относительно storage

    // Состояние прогресса анализа
    bool m_bAnalyzing;
    int m_AnalyzeProgress;      // 0..100
    char m_aAnalyzeStatus[128];
    int64_t m_AnalyzeStartedMs;
    int64_t m_AnalyzeFinishedMs;
    int m_AnalyzeTotalDemos;
    int m_AnalyzeDoneDemos;

    // Профили
    int m_SelectedProfileId;

    // Вкладки внутри панели PFA
    enum ETab {
        TAB_DEMOS = 0,
        TAB_BINDS,
        TAB_PROFILES,
        TAB_UPDATES,
        TAB_STATISTICS,
    };
    int m_ActiveTab;

    // Список импортированных демок (кэш для UI — обновляется при Render)
    std::vector<SDemoMeta> m_vCachedDemos;

    CSettingsUi();

    void SetDatabase(CDatabase *pDb) { m_pDb = pDb; }

    /** Обновить кэш m_vCachedDemos из БД. */
    void RefreshDemoList();

    /** Обновить список m_vImportCandidates — сканировать папку demos/. */
    void RefreshImportCandidates(IStorage *pStorage);

    /**
     * Обновить прогресс анализа (вызывается каждый кадр из CMenus::OnUpdate
     * через GameClient->m_PersonalFlightAssistant->SettingsUi()).
     */
    void UpdateAnalysisProgress();

    /** Сбросить состояние прогресса анализа. */
    void ResetAnalysisState();

    /** Старт анализа — помечает состояние как «анализ идёт». */
    void StartAnalysis(int TotalDemos);

    /** Отметить одну демку как проанализированную. */
    void OnOneDemoAnalyzed();

    /** Завершить анализ. */
    void FinishAnalysis(const char *pStatus);

    /** Получить статус для UI. */
    void GetStatusText(char *pBuf, size_t BufSize) const;

private:
    CDatabase *m_pDb;
};

} // namespace pfa

#endif // GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_SETTINGS_UI_H
