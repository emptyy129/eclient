/**
 * @file pfa_settings_ui.cpp
 * @brief Реализация держателя состояния UI Personal Flight Assistant.
 *
 * Весь рендеринг UI выполняется в CMenus::RenderSettingsPersonalFlightAssistant
 * (см. menus_settings_pfa.cpp). Этот файл — только логика состояния.
 */

#include "pfa_settings_ui.h"

#include <base/str.h>
#include <base/time.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace pfa {

CSettingsUi::CSettingsUi()
    : m_SelectedDemoIndex(-1)
    , m_ScrollDemoList(0)
    , m_ImportSelectedIndex(-1)
    , m_bAnalyzing(false)
    , m_AnalyzeProgress(0)
    , m_AnalyzeStartedMs(0)
    , m_AnalyzeFinishedMs(0)
    , m_AnalyzeTotalDemos(0)
    , m_AnalyzeDoneDemos(0)
    , m_SelectedProfileId(0)
    , m_ActiveTab(TAB_DEMOS)
    , m_pDb(nullptr)
{
    m_aSearchFilter[0] = '\0';
    m_aAnalyzeStatus[0] = '\0';
}

void CSettingsUi::RefreshDemoList()
{
    if(!m_pDb) return;
    m_vCachedDemos = m_pDb->ListDemos();
    // Подсветить первую демку, если ничего не выбрано
    if(m_SelectedDemoIndex < 0 && !m_vCachedDemos.empty())
        m_SelectedDemoIndex = 0;
    if(m_SelectedDemoIndex >= (int)m_vCachedDemos.size())
        m_SelectedDemoIndex = (int)m_vCachedDemos.size() - 1;
}

void CSettingsUi::RefreshImportCandidates(IStorage *pStorage)
{
    m_vImportCandidates.clear();
    if(!pStorage) return;

    // Сканируем папку demos/ в user storage — туда DDNet сохраняет демки
    // по умолчанию. Используем ListDirectoryInfo с колбэком.
    struct SScanCtx {
        std::vector<std::string> *pOut;
    } Ctx;
    Ctx.pOut = &m_vImportCandidates;

    pStorage->ListDirectoryInfo(IStorage::TYPE_SAVE, "demos",
        [](const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser) -> int {
            auto *pCtx = (SScanCtx *)pUser;
            if(IsDir) return 0;
            // Только .demo файлы (проверка по расширению)
            if(str_endswith(pInfo->m_pName, ".demo"))
            {
                char aPath[IO_MAX_PATH_LENGTH];
                str_format(aPath, sizeof(aPath), "demos/%s", pInfo->m_pName);
                pCtx->pOut->push_back(std::string(aPath));
            }
            return 0;
        },
        &Ctx);

    std::sort(m_vImportCandidates.begin(), m_vImportCandidates.end());
}

void CSettingsUi::UpdateAnalysisProgress()
{
    if(!m_bAnalyzing) return;
    if(m_AnalyzeTotalDemos > 0)
    {
        m_AnalyzeProgress = std::min(100, (m_AnalyzeDoneDemos * 100) / m_AnalyzeTotalDemos);
        char aBuf[128];
        str_format(aBuf, sizeof(aBuf), "Анализ: %d/%d демок...", m_AnalyzeDoneDemos, m_AnalyzeTotalDemos);
        str_copy(m_aAnalyzeStatus, aBuf);
    }
}

void CSettingsUi::ResetAnalysisState()
{
    m_bAnalyzing = false;
    m_AnalyzeProgress = 0;
    m_aAnalyzeStatus[0] = '\0';
    m_AnalyzeStartedMs = 0;
    m_AnalyzeFinishedMs = 0;
    m_AnalyzeTotalDemos = 0;
    m_AnalyzeDoneDemos = 0;
}

void CSettingsUi::StartAnalysis(int TotalDemos)
{
    m_bAnalyzing = true;
    m_AnalyzeProgress = 0;
    m_AnalyzeStartedMs = (int64_t)time(nullptr) * 1000;
    m_AnalyzeFinishedMs = 0;
    m_AnalyzeTotalDemos = TotalDemos;
    m_AnalyzeDoneDemos = 0;
    str_copy(m_aAnalyzeStatus, "Запуск анализа...");
}

void CSettingsUi::OnOneDemoAnalyzed()
{
    if(m_bAnalyzing) ++m_AnalyzeDoneDemos;
}

void CSettingsUi::FinishAnalysis(const char *pStatus)
{
    m_bAnalyzing = false;
    m_AnalyzeProgress = 100;
    m_AnalyzeFinishedMs = (int64_t)time(nullptr) * 1000;
    str_copy(m_aAnalyzeStatus, pStatus ? pStatus : "Анализ завершён");
}

void CSettingsUi::GetStatusText(char *pBuf, size_t BufSize) const
{
    if(m_bAnalyzing)
        str_format(pBuf, BufSize, "Анализ в процессе: %d%% (%s)", m_AnalyzeProgress, m_aAnalyzeStatus);
    else if(m_AnalyzeFinishedMs > 0)
    {
        int64_t Now = (int64_t)time(nullptr) * 1000;
        int Elapsed = (int)((Now - m_AnalyzeFinishedMs) / 1000);
        str_format(pBuf, BufSize, "Анализ завершён %dс назад: %s", Elapsed, m_aAnalyzeStatus);
    }
    else
        str_copy(pBuf, "Анализ не запускался", BufSize);
}

} // namespace pfa
