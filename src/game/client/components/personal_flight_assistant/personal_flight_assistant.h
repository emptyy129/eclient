/**
 * @file personal_flight_assistant.h
 * @brief Главный компонент Personal Flight Assistant (PFA).
 *
 * Наследует CComponent и регистрируется в CGameClient как полноценный
 * компонент: получает OnInit / OnRender / OnInput / OnNewSnapshot.
 *
 * Связывает между собой все подмодули:
 *   - CDatabase           — постоянное хранилище
 *   - CDemoParser         — извлечение телеметрии из демок
 *   - CStatistics         — статистическая модель
 *   - CPredictor          — расчёт точки старта
 *   - CVisualizer         — отрисовка подсказок
 *   - CSettingsUi         — панель настроек
 *   - CUpdater            — обновления через GitHub Releases
 *
 * Раздел ТЗ: 6. Требования к разработке — разделить на независимые модули.
 */

#ifndef GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PERSONAL_FLIGHT_ASSISTANT_H
#define GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PERSONAL_FLIGHT_ASSISTANT_H

#include "pfa_types.h"
#include "pfa_database.h"
#include "pfa_demo_parser.h"
#include "pfa_statistics.h"
#include "pfa_predictor.h"
#include "pfa_visualizer.h"
#include "pfa_settings_ui.h"
#include "pfa_update.h"

#include <engine/console.h>
#include <engine/input.h>
#include <game/client/component.h>

class CGameClient;

namespace pfa {

/**
 * @brief Главный компонент PFA.
 *
 * Один экземпляр на CGameClient. Не потокобезопасен для UI-вызовов
 * (вызываются из главного потока), но операции анализа демок
 * выполняются через CJobPool движка и сериализуются через CDatabase::m_pLock.
 */
class CPersonalFlightAssistant : public CComponent {
    CDatabase    m_Db;
    CDemoParser *m_pParser;        // владеем через указатель, т.к. требует CSnapshotDelta из GameClient
    CStatistics  m_Stats;
    CPredictor   m_Predictor;
    CVisualizer  m_Visualizer;
    CSettingsUi  m_SettingsUi;
    CUpdater     m_Updater;

    // Кеши текущей игровой ситуации
    SCurrentSituation m_CurrentSituation;
    SRecommendation   m_Recommendation;
    int64_t           m_LastRecommendationMs;
    bool              m_bBindActive; // зажат ли бинд показа подсказки

    // Команды консоли
    static void ConPfaToggle(IConsole::IResult *pResult, void *pUserData);
    static void ConPfaAnalyze(IConsole::IResult *pResult, void *pUserData);
    static void ConPfaImportDemo(IConsole::IResult *pResult, void *pUserData);
    static void ConPfaClearAll(IConsole::IResult *pResult, void *pUserData);
    static void ConPfaRebuildModel(IConsole::IResult *pResult, void *pUserData);
    static void ConPfaUpdateCheck(IConsole::IResult *pResult, void *pUserData);
    static void ConPfaUpdateInstall(IConsole::IResult *pResult, void *pUserData);
    static void ConPfaUpdateRollback(IConsole::IResult *pResult, void *pUserData);
    static void ConPfaStatus(IConsole::IResult *pResult, void *pUserData);

    /**
     * Собрать снимок текущей ситуации из снапшота:
     * позиция и скорость локального игрока + его дамми (если есть).
     */
    void UpdateCurrentSituation();

    /**
     * Пересчитать рекомендацию. Если AutoRefresh включён — каждый кадр;
     * иначе только при активации бинда.
     */
    void RefreshRecommendation();

public:
    CPersonalFlightAssistant();
    ~CPersonalFlightAssistant() override;
    int Sizeof() const override { return sizeof(*this); }

    // ---- CComponent interface ----
    void OnInterfacesInit(CGameClient *pClient) override;
    void OnConsoleInit() override;
    void OnInit() override;
    void OnShutdown() override;
    void OnReset() override;
    void OnRender() override;
    void OnNewSnapshot() override;
    bool OnInput(const IInput::CEvent &Event) override;
    void OnUpdate() override;

    // ---- Public API для других компонентов ----

    /** Доступ к подсистемам (для UI / интеграции). */
    CDatabase    *Database()   { return &m_Db; }
    CStatistics  *Statistics() { return &m_Stats; }
    CPredictor   *Predictor()  { return &m_Predictor; }
    CVisualizer  *Visualizer() { return &m_Visualizer; }
    CSettingsUi  *SettingsUi() { return &m_SettingsUi; }
    CUpdater     *Updater()    { return &m_Updater; }

    /** Вызывается из CMenus::RenderSettings при активной вкладке PFA. */
    void RenderSettingsPanel(CUIRect MainView);

    /** Импорт демки (для UI кнопки). */
    bool ImportDemo(const char *pPath);

    /** Запустить фоновый анализ всех непроанализированных демок. */
    void StartAnalysisAll();

    /**
     * Внутренняя функция анализа одной демки в фоновом потоке.
     * Вызывается из CAnalysisJob::Run().
     */
    void AnalyzeOneDemoInBackground(int64_t DemoId, const char *pFilename, int StorageType);

    /** Включить/выключить показ подсказки. */
    void ToggleHints() { m_bBindActive = !m_bBindActive; }

    /** Текущее состояние бинда показа (для визуализатора). */
    bool IsBindActive() const { return m_bBindActive; }
};

} // namespace pfa

#endif // GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PERSONAL_FLIGHT_ASSISTANT_H
