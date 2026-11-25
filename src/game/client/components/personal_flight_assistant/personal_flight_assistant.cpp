/**
 * @file personal_flight_assistant.cpp
 * @brief Реализация главного компонента CPersonalFlightAssistant.
 *
 * Это точка входа в систему PFA. Все остальные модули общаются
 * только через этот класс. Компонент регистрируется в CGameClient
 * и получает штатные колбэки движка.
 */

#include "personal_flight_assistant.h"

#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <engine/client.h>
#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <generated/protocol.h>

#include <chrono>
#include <cmath>
#include <cstring>

namespace pfa {

// ---- Фоновая задача анализа ----

class CAnalysisJob : public IJob {
    CPersonalFlightAssistant *m_pPfa;
    int64_t m_DemoId;
    char m_aFilename[256];
    int m_StorageType;

public:
    CAnalysisJob(CPersonalFlightAssistant *pPfa, int64_t DemoId,
        const char *pFilename, int StorageType)
        : m_pPfa(pPfa)
        , m_DemoId(DemoId)
        , m_StorageType(StorageType)
    {
        str_copy(m_aFilename, pFilename);
    }

    void Run() override
    {
        // Полный анализ одной демки
        // Использует CDemoParser, сохраняет треки и попытки в CDatabase
        // через m_pPfa->Database().
        // ВНИМАНИЕ: CDatabase::m_pLock сериализует все SQL-запросы,
        // что безопасно для фонового исполнения.
        // Реальная логика:
        //   SParseResult Result;
        //   m_pPfa->Parser()->Parse(m_aFilename, m_StorageType, &Result);
        //   for (auto &Track : Result.m_vTracks) {
        //     Track.m_DemoId = m_DemoId;
        //     m_pPfa->Database()->AddTeeTrack(Track);
        //   }
        //   for (auto &Attempt : Result.m_vAttempts) {
        //     Attempt.m_DemoId = m_DemoId;
        //     m_pPfa->Database()->AddAttempt(Attempt);
        //   }
        //   m_pPfa->Database()->UpdateDemoParsed(m_DemoId, Result.m_NumAttemptsDetected,
        //                                        Result.m_NumSuccessDetected, true);
        //   m_pPfa->Statistics()->RebuildAll();
        // Делегируем в PFA:
        m_pPfa->AnalyzeOneDemoInBackground(m_DemoId, m_aFilename, m_StorageType);
    }
};

CPersonalFlightAssistant::CPersonalFlightAssistant()
    : m_pParser(nullptr)
{
    mem_zero(&m_CurrentSituation, sizeof(m_CurrentSituation));
    mem_zero(&m_Recommendation, sizeof(m_Recommendation));
    m_LastRecommendationMs = 0;
    m_bBindActive = false;
}

CPersonalFlightAssistant::~CPersonalFlightAssistant()
{
    delete m_pParser;
}

void CPersonalFlightAssistant::OnInterfacesInit(CGameClient *pClient)
{
    CComponent::OnInterfacesInit(pClient);
    // Получаем доступ к CSnapshotDelta из GameClient для парсера.
    // В реальной интеграции CGameClient exposes m_SnapshotDelta,
    // либо мы создаём свой через CNetObjHandler (как demo_extract_chat).
    if(!m_pParser)
    {
        // Создаём локальный CSnapshotDelta — независимый от GameClient
        auto pDelta = new CSnapshotDelta();
        CNetObjHandler NetObjHandler;
        for(int i = 0; i < NUM_NETOBJTYPES; ++i)
            pDelta->SetStaticsize(i, NetObjHandler.GetObjSize(i));
        m_pParser = new CDemoParser(Storage(), pDelta);
    }
}

void CPersonalFlightAssistant::OnConsoleInit()
{
    IConsole *pCon = Console();
    if(!pCon) return;

    // Регистрация консольных команд
    pCon->Register("pfa_toggle", "", "Включить/выключить показ подсказки PFA", CFGFLAG_CLIENT,
        ConPfaToggle, this);
    pCon->Register("pfa_analyze", "?s[filename]", "Запустить анализ демки (или всех непроанализированных)", CFGFLAG_CLIENT,
        ConPfaAnalyze, this);
    pCon->Register("pfa_import", "s[path]", "Импортировать демку в базу PFA", CFGFLAG_CLIENT,
        ConPfaImportDemo, this);
    pCon->Register("pfa_clear", "", "Очистить базу PFA (все демки/профили)", CFGFLAG_CLIENT,
        ConPfaClearAll, this);
    pCon->Register("pfa_rebuild", "", "Пересчитать все статистические модели", CFGFLAG_CLIENT,
        ConPfaRebuildModel, this);
    pCon->Register("pfa_update_check", "", "Проверить обновления через GitHub Releases", CFGFLAG_CLIENT,
        ConPfaUpdateCheck, this);
    pCon->Register("pfa_update_install", "", "Установить последнее доступное обновление", CFGFLAG_CLIENT,
        ConPfaUpdateInstall, this);
    pCon->Register("pfa_update_rollback", "s[backup_tag]", "Откатиться к указанному бэкапу", CFGFLAG_CLIENT,
        ConPfaUpdateRollback, this);
    pCon->Register("pfa_status", "", "Показать состояние PFA", CFGFLAG_CLIENT,
        ConPfaStatus, this);
}

void CPersonalFlightAssistant::OnInit()
{
    if(!m_Db.Init(Storage()))
    {
        log_error("pfa", "OnInit: не удалось открыть БД");
        return;
    }
    m_Stats = CStatistics(&m_Db);
    m_Predictor = CPredictor(&m_Db, &m_Stats);
    m_Visualizer.Init(Graphics(), TextRender());
    m_SettingsUi.SetDatabase(&m_Db);
    m_Updater.Init(Storage(), Http());

    // Загрузить сохранённые профили
    m_Stats.RebuildAll();
    log_info("pfa", "Personal Flight Assistant инициализирован");
}

void CPersonalFlightAssistant::OnShutdown()
{
    m_Db.Shutdown();
}

void CPersonalFlightAssistant::OnReset()
{
    mem_zero(&m_CurrentSituation, sizeof(m_CurrentSituation));
    mem_zero(&m_Recommendation, sizeof(m_Recommendation));
    m_bBindActive = false;
}

void CPersonalFlightAssistant::OnNewSnapshot()
{
    UpdateCurrentSituation();
}

bool CPersonalFlightAssistant::OnInput(const IInput::CEvent &Event)
{
    // Бинд «показать подсказку» проверяется здесь.
    // В реальной интеграции пользовательский бинд регистрируется
    // через CBinds и вызывает консольную команду pfa_toggle,
    // поэтому здесь мы только обрабатываем нажатие escape для скрытия.
    (void)Event;
    return false;
}

void CPersonalFlightAssistant::OnUpdate()
{
    // Если AutoRefresh включён — обновляем рекомендацию каждый кадр
    if(g_Config.m_PfaEnabled && g_Config.m_PfaAutoRefresh)
    {
        RefreshRecommendation();
    }
    m_SettingsUi.UpdateAnalysisProgress();
}

void CPersonalFlightAssistant::OnRender()
{
    if(!g_Config.m_PfaEnabled) return;
    // Показывать подсказку только если бинд активен (зажата клавиша)
    // либо если AutoRefresh включён (тогда постоянно)
    bool bShow = m_bBindActive || g_Config.m_PfaAutoRefresh;
    if(!bShow) return;

    // Получить центр камеры для перевода координат
    vec2 CameraCenter(0.0f, 0.0f);
    if(GameClient() && GameClient()->m_pCamera)
    {
        CameraCenter = GameClient()->m_pCamera->m_Center;
    }
    m_Visualizer.Render(m_Recommendation, CameraCenter, true);
}

void CPersonalFlightAssistant::UpdateCurrentSituation()
{
    m_CurrentSituation.m_bValid = false;
    if(!GameClient()) return;
    if(Client()->State() != IClient::STATE_ONLINE) return;

    // Локальный игрок
    int LocalId = Client()->m_LocalClientId;
    if(LocalId < 0 || LocalId >= 64) return;

    // Извлечь из снапшота GameClient позиции/скорости
    // CGameClient хранит m_Snap.m_aCharacters[id].m_Cur (CNetObj_Character)
    const auto &Snap = GameClient()->m_Snap;
    if(!Snap.m_aCharacters[LocalId].m_Active) return;

    const CNetObj_Character &MyChar = Snap.m_aCharacters[LocalId].m_Cur;
    m_CurrentSituation.m_Tick = Client()->GameTick(g_Config.m_ClDummy);
    m_CurrentSituation.m_MyX = MyChar.m_X;
    m_CurrentSituation.m_MyY = MyChar.m_Y;
    m_CurrentSituation.m_MyVelX = MyChar.m_VelX;
    m_CurrentSituation.m_MyVelY = MyChar.m_VelY;

    // Dummy — второй локальный игрок (если есть)
    int DummyId = Client()->m_DummyId;
    if(DummyId >= 0 && DummyId < 64 && Snap.m_aCharacters[DummyId].m_Active)
    {
        const CNetObj_Character &DuChar = Snap.m_aCharacters[DummyId].m_Cur;
        m_CurrentSituation.m_DummyX = DuChar.m_X;
        m_CurrentSituation.m_DummyY = DuChar.m_Y;
        m_CurrentSituation.m_DummyVelX = DuChar.m_VelX;
        m_CurrentSituation.m_DummyVelY = DuChar.m_VelY;
        m_CurrentSituation.m_HookState = MyChar.m_HookState;
        m_CurrentSituation.m_HookedPlayer = MyChar.m_HookedPlayer;

        // Расстояние
        int Dx = MyChar.m_X - DuChar.m_X;
        int Dy = MyChar.m_Y - DuChar.m_Y;
        m_CurrentSituation.m_Distance = (int)std::sqrt((double)Dx * Dx + (double)Dy * Dy);

        // Относительный угол
        double Dot = (double)(Dx * MyChar.m_VelX + Dy * MyChar.m_VelY);
        double MagD = std::sqrt((double)Dx * Dx + (double)Dy * Dy) + 1e-6;
        double MagV = std::sqrt((double)MyChar.m_VelX * MyChar.m_VelX +
                                (double)MyChar.m_VelY * MyChar.m_VelY) + 1e-6;
        double Cos = Dot / (MagD * MagV);
        if(Cos > 1.0) Cos = 1.0;
        if(Cos < -1.0) Cos = -1.0;
        m_CurrentSituation.m_RelativeAngle = (int)(std::acos(Cos) * 1000.0);

        m_CurrentSituation.m_bValid = true;
    }
}

void CPersonalFlightAssistant::RefreshRecommendation()
{
    if(!m_CurrentSituation.m_bValid)
    {
        m_Recommendation.m_bValid = false;
        return;
    }
    int ProfileId = g_Config.m_PfaProfileActive;
    m_Recommendation = m_Predictor.Recommend(m_CurrentSituation, ProfileId, PfaConst::MAX_CANDIDATES);
    m_LastRecommendationMs = (int64_t)time_get();
}

void CPersonalFlightAssistant::RenderSettingsPanel(CUIRect MainView)
{
    // Рендеринг UI выполняется в CMenus::RenderSettingsPersonalFlightAssistant
    // (см. menus_settings_pfa.cpp). Этот метод — точка входа для будущих
    // расширений; оставлен для обратной совместимости с интеграцией в
    // CMenus::RenderSettings.
    (void)MainView;
}

bool CPersonalFlightAssistant::ImportDemo(const char *pPath)
{
    if(!m_Db.IsOpen()) return false;
    SDemoMeta Existing;
    if(m_Db.FindDemoByPath(pPath, Existing))
    {
        log_info("pfa", "Демка уже импортирована: id=%lld", (long long)Existing.m_Id);
        return true;
    }

    // Прочитать заголовок
    SDemoMeta Meta{};
    char aError[256] = {0};
    if(!CDemoParser::ReadHeaderOnly(Storage(), pPath, IStorage::TYPE_ALL_OR_ABSOLUTE, Meta, aError, sizeof(aError)))
    {
        log_error("pfa", "ImportDemo: не удалось прочитать заголовок '%s': %s", pPath, aError);
        return false;
    }

    // SHA256 для дедупликации
    char aSha[65] = {0};
    if(CDemoParser::ComputeSha256(Storage(), pPath, IStorage::TYPE_ALL_OR_ABSOLUTE, aSha))
    {
        str_copy(Meta.m_aSha256, aSha);
    }

    // Размер файла
    IOHANDLE File = Storage()->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_ALL_OR_ABSOLUTE);
    if(File)
    {
        Meta.m_FileSize = (int64_t)io_length(File);
        io_close(File);
    }

    str_copy(Meta.m_aFilename, pPath);
    Meta.m_ImportedAt = (int64_t)time(nullptr);
    Meta.m_bParsed = false;

    int64_t Id = m_Db.AddDemo(Meta);
    if(Id == 0)
    {
        log_error("pfa", "ImportDemo: не удалось добавить в БД '%s'", pPath);
        return false;
    }
    log_info("pfa", "Импортирована демка: id=%lld path=%s", (long long)Id, pPath);
    return true;
}

void CPersonalFlightAssistant::StartAnalysisAll()
{
    if(!m_Db.IsOpen()) return;
    auto vDemos = m_Db.ListDemos();
    int NumToAnalyze = 0;
    for(const auto &D : vDemos)
    {
        if(D.m_bParsed) continue;
        // Создать фоновую задачу анализа
        Engine()->AddJob(std::make_shared<CAnalysisJob>(this, D.m_Id, D.m_aFilename, IStorage::TYPE_ALL_OR_ABSOLUTE));
        ++NumToAnalyze;
    }
    // Инициализировать прогресс UI
    if(NumToAnalyze > 0)
        m_SettingsUi.StartAnalysis(NumToAnalyze);
    else
        m_SettingsUi.FinishAnalysis("Нет непроанализированных демок");
}

void CPersonalFlightAssistant::AnalyzeOneDemoInBackground(int64_t DemoId,
    const char *pFilename, int StorageType)
{
    if(!m_pParser) return;
    SParseResult Result;
    if(!m_pParser->Parse(pFilename, StorageType, &Result))
    {
        log_error("pfa", "Background analyze failed: %s", Result.m_aError);
        m_SettingsUi.OnOneDemoAnalyzed();
        return;
    }
    // Сохранить треки
    for(const auto &Track : Result.m_vTracks)
    {
        STeeTrack T = Track;
        T.m_DemoId = DemoId;
        m_Db.AddTeeTrack(T);
    }
    // Сохранить попытки
    for(const auto &Attempt : Result.m_vAttempts)
    {
        SPseudoFlyAttempt A = Attempt;
        A.m_DemoId = DemoId;
        m_Db.AddAttempt(A);
    }
    m_Db.UpdateDemoParsed(DemoId, Result.m_NumAttemptsDetected, Result.m_NumSuccessDetected, true);
    // Пересчитать статистические модели
    m_Stats.RebuildAll();
    log_info("pfa", "Анализ демки %lld завершён: попыток=%d, успехов=%d",
        (long long)DemoId, Result.m_NumAttemptsDetected, Result.m_NumSuccessDetected);
    // Обновить прогресс UI
    m_SettingsUi.OnOneDemoAnalyzed();
    // Если это последняя демка — завершить анализ
    if(m_SettingsUi.m_bAnalyzing &&
       m_SettingsUi.m_AnalyzeDoneDemos >= m_SettingsUi.m_AnalyzeTotalDemos)
    {
        m_SettingsUi.FinishAnalysis("Все демки проанализированы");
        m_SettingsUi.RefreshDemoList();
    }
}

// ---- Консольные команды ----

void CPersonalFlightAssistant::ConPfaToggle(IConsole::IResult *pResult, void *pUserData)
{
    auto *pPfa = (CPersonalFlightAssistant *)pUserData;
    pPfa->ToggleHints();
}

void CPersonalFlightAssistant::ConPfaAnalyze(IConsole::IResult *pResult, void *pUserData)
{
    auto *pPfa = (CPersonalFlightAssistant *)pUserData;
    if(pResult->NumArguments() > 0)
    {
        const char *pFn = pResult->GetString(0);
        if(pPfa->ImportDemo(pFn))
        {
            // Запустить анализ конкретной демки
            SDemoMeta Meta;
            if(pPfa->m_Db.FindDemoByPath(pFn, Meta))
            {
                pPfa->Engine()->AddJob(std::make_shared<CAnalysisJob>(pPfa, Meta.m_Id, Meta.m_aFilename, IStorage::TYPE_ALL_OR_ABSOLUTE));
            }
        }
    }
    else
    {
        pPfa->StartAnalysisAll();
    }
}

void CPersonalFlightAssistant::ConPfaImportDemo(IConsole::IResult *pResult, void *pUserData)
{
    auto *pPfa = (CPersonalFlightAssistant *)pUserData;
    pPfa->ImportDemo(pResult->GetString(0));
}

void CPersonalFlightAssistant::ConPfaClearAll(IConsole::IResult *pResult, void *pUserData)
{
    auto *pPfa = (CPersonalFlightAssistant *)pUserData;
    auto vDemos = pPfa->m_Db.ListDemos();
    for(const auto &D : vDemos)
        pPfa->m_Db.RemoveDemo(D.m_Id);
    log_info("pfa", "База PFA очищена");
}

void CPersonalFlightAssistant::ConPfaRebuildModel(IConsole::IResult *pResult, void *pUserData)
{
    auto *pPfa = (CPersonalFlightAssistant *)pUserData;
    pPfa->m_Stats.RebuildAll();
}

void CPersonalFlightAssistant::ConPfaUpdateCheck(IConsole::IResult *pResult, void *pUserData)
{
    auto *pPfa = (CPersonalFlightAssistant *)pUserData;
    pPfa->m_Updater.CheckForUpdates(g_Config.m_PfaUpdateRepo);
}

void CPersonalFlightAssistant::ConPfaUpdateInstall(IConsole::IResult *pResult, void *pUserData)
{
    auto *pPfa = (CPersonalFlightAssistant *)pUserData;
    pPfa->m_Updater.InstallUpdate();
}

void CPersonalFlightAssistant::ConPfaUpdateRollback(IConsole::IResult *pResult, void *pUserData)
{
    auto *pPfa = (CPersonalFlightAssistant *)pUserData;
    pPfa->m_Updater.RollbackTo(pResult->GetString(0));
}

void CPersonalFlightAssistant::ConPfaStatus(IConsole::IResult *pResult, void *pUserData)
{
    auto *pPfa = (CPersonalFlightAssistant *)pUserData;
    auto St = pPfa->m_Db.GetStatistics(0);
    log_info("pfa", "Статус PFA: демки=%d, попыток=%d (успешных=%d, %.1f%%), профилей=%d",
        St.m_NumDemos, St.m_NumAttempts, St.m_NumSuccess,
        St.m_SuccessRate * 100.0f, St.m_NumProfiles);
}

} // namespace pfa
