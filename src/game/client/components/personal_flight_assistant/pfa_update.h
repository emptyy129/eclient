/**
 * @file pfa_update.h
 * @brief Система обновлений через GitHub Releases.
 *
 * Запрашивает https://api.github.com/repos/<owner>/<repo>/releases/latest,
 * сравнивает версию с текущей, скачивает asset для Windows (DDNet-Windows-x86_64.zip),
 * проверяет SHA256, ставит с бэкапом, поддерживает откат.
 *
 * Раздел ТЗ: 8. Архитектура обновлений.
 */

#ifndef GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_UPDATE_H
#define GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_UPDATE_H

#include <base/lock.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class IStorage;
class IHttp;
class IHttpRequest;

namespace pfa {

/**
 * @brief Запись об одной версии (для changelog/rollback).
 */
struct SReleaseInfo {
    char    m_aTagName[32];         // e.g. "v1.2.3"
    char    m_aName[128];           // заголовок релиза
    char    m_aPublishedAt[32];     // ISO timestamp
    char    m_aHtmlUrl[256];        // ссылка на веб-страницу релиза
    char    m_aBodyChangelog[4096]; // markdown-текст changelog'а
    char    m_aAssetName[128];      // имя файла ассета (Windows zip)
    char    m_aAssetUrl[256];       // browser_download_url
    int64_t m_AssetSize;
    char    m_aAssetDigest[65];     // ожидаемый SHA256, если задан в release
};

/**
 * @brief Состояния процесса обновления.
 */
enum class EUpdateState {
    IDLE,
    CHECKING,
    NO_UPDATE,
    UPDATE_AVAILABLE,
    DOWNLOADING,
    VERIFYING,
    BACKING_UP,
    INSTALLING,
    DONE,
    FAILED,
};

/**
 * @brief Безопасный менеджер обновлений.
 *
 * В отличие от стандартного CUpdater DDNet, этот:
 *   - Опирается на GitHub Releases (не на update.ddnet.org)
 *   - Хранит последние N бэкапов в backups/ (откат)
 *   - Сохраняет конфиг/профили/демки PFA нетронутыми (они вне exe)
 *   - Проверяет SHA256 перед установкой
 *   - Поддерживает changelog в UI
 */
class CUpdater {
    IStorage *m_pStorage;
    IHttp *m_pHttp;

    mutable CLock m_Lock;
    EUpdateState m_State GUARDED_BY(m_Lock);
    int m_Percent GUARDED_BY(m_Lock);
    char m_aStatus[256] GUARDED_BY(m_Lock);
    char m_aLastError[256] GUARDED_BY(m_Lock);
    SReleaseInfo m_LatestRelease GUARDED_BY(m_Lock);
    std::vector<SReleaseInfo> m_vHistory GUARDED_BY(m_Lock);

    std::shared_ptr<IHttpRequest> m_pCurrentRequest;

    /** Парсинг одного JSON-релиза с GitHub API. */
    bool ParseReleaseJson(const char *pJson, SReleaseInfo &Out);

    /** Скачать и проверить SHA256 ассета. */
    bool DownloadAndVerify(const SReleaseInfo &Release, const char *pDestPath);

    /** Сделать бэкап текущего exe в backups/<timestamp>/. */
    bool BackupCurrent();

    /** Применить zip: распаковать поверх текущего exe. */
    bool ApplyArchive(const char *pArchivePath);

    /** Записать метаданные о применённой версии в backups/manifest.json. */
    bool WriteManifest(const SReleaseInfo &Release, const char *pBackupDir);

public:
    CUpdater();
    ~CUpdater();

    void Init(IStorage *pStorage, IHttp *pHttp);

    /** Проверить наличие новой версии (асинхронно, через GitHub API). */
    void CheckForUpdates(const char *pRepoOwnerSlashRepo);

    /** Если доступно обновление — начать скачивание. */
    void InstallUpdate();

    /** Откатиться к указанному бэкапу. */
    bool RollbackTo(const char *pBackupTag);

    /** Получить список доступных бэкапов для отката. */
    std::vector<std::string> ListBackups();

    /** Получить список последних N релизов для changelog UI. */
    const std::vector<SReleaseInfo> &ReleaseHistory() const { return m_vHistory; }

    /** Снимок состояния для UI. */
    EUpdateState GetState() const;
    int GetPercent() const;
    void GetStatus(char *pBuf, size_t BufSize) const;
    const SReleaseInfo *GetLatestRelease() const;
};

} // namespace pfa

#endif // GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_UPDATE_H
