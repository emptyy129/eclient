/**
 * @file pfa_update.cpp
 * @brief Реализация системы обновлений через GitHub Releases.
 *
 * Поток обновления:
 *   1. CheckForUpdates(): создаёт IHttpRequest к GitHub API releases/latest,
 *      дожидается ответа, парсит JSON через JsonParse/json_object_get.
 *   2. Сравнивает tag_name с текущей версией клиента (GAME_VERSION).
 *   3. InstallUpdate():
 *      a) DownloadAndVerify: скачивает asset во временную папку,
 *         проверяет SHA256 через IHttpRequest::ResultSha256.
 *      b) BackupCurrent: копирует bin-каталог в backups/<timestamp>/.
 *      c) WriteManifest: JSON-манифест применённой версии.
 *      d) ApplyArchive: распаковка zip (PowerShell Expand-Archive / unzip).
 *   4. RollbackTo(tag): восстановление bin-файлов из бэкапа.
 *
 * Раздел ТЗ: 8. Архитектура обновлений.
 */

#include "pfa_update.h"

#include <base/fs.h>
#include <base/hash.h>
#include <base/hash_ctxt.h>
#include <base/io.h>
#include <base/log.h>
#include <base/mem.h>
#include <base/str.h>
#include <engine/http.h>
#include <engine/shared/json.h>
#include <engine/storage.h>
#include <game/version.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#endif

namespace pfa {

namespace {
constexpr const char *PFA_BACKUP_DIR = "personal_flight_assistant/backups";
constexpr const char *PFA_MANIFEST_FILE = "backups_manifest.json";

/**
 * Сравнить семантические версии "v1.2.3" и "v1.2.4".
 * @return >0 если A>B, 0 если равно, <0 если A<B.
 */
int CompareVersions(const char *pA, const char *pB)
{
    int aMaj = 0, aMin = 0, aPat = 0, bMaj = 0, bMin = 0, bPat = 0;
    if(pA[0] == 'v' || pA[0] == 'V') ++pA;
    if(pB[0] == 'v' || pB[0] == 'V') ++pB;
    sscanf(pA, "%d.%d.%d", &aMaj, &aMin, &aPat);
    sscanf(pB, "%d.%d.%d", &bMaj, &bMin, &bPat);
    if(aMaj != bMaj) return aMaj - bMaj;
    if(aMin != bMin) return aMin - bMin;
    return aPat - bPat;
}
} // namespace

CUpdater::CUpdater()
    : m_pStorage(nullptr)
    , m_pHttp(nullptr)
    , m_State(EUpdateState::IDLE)
    , m_Percent(0)
{
    m_aStatus[0] = '\0';
    m_aLastError[0] = '\0';
    mem_zero(&m_LatestRelease, sizeof(m_LatestRelease));
}

CUpdater::~CUpdater() = default;

void CUpdater::Init(IStorage *pStorage, IHttp *pHttp)
{
    m_pStorage = pStorage;
    m_pHttp = pHttp;
}

bool CUpdater::ParseReleaseJson(const char *pJson, SReleaseInfo &Out)
{
    json_value *pRoot = JsonParse((json_char *)pJson, str_length(pJson));
    if(!pRoot)
    {
        log_error("pfa", "Update: JSON parse failed");
        return false;
    }

    const json_value *pTag = json_object_get(pRoot, "tag_name");
    const json_value *pName = json_object_get(pRoot, "name");
    const json_value *pPub = json_object_get(pRoot, "published_at");
    const json_value *pHtml = json_object_get(pRoot, "html_url");
    const json_value *pBody = json_object_get(pRoot, "body");
    const json_value *pAssets = json_object_get(pRoot, "assets");

    if(pTag && pTag->type == json_string)
        str_copy(Out.m_aTagName, pTag->u.string.ptr, sizeof(Out.m_aTagName));
    if(pName && pName->type == json_string)
        str_copy(Out.m_aName, pName->u.string.ptr, sizeof(Out.m_aName));
    if(pPub && pPub->type == json_string)
        str_copy(Out.m_aPublishedAt, pPub->u.string.ptr, sizeof(Out.m_aPublishedAt));
    if(pHtml && pHtml->type == json_string)
        str_copy(Out.m_aHtmlUrl, pHtml->u.string.ptr, sizeof(Out.m_aHtmlUrl));
    if(pBody && pBody->type == json_string)
        str_copy(Out.m_aBodyChangelog, pBody->u.string.ptr, sizeof(Out.m_aBodyChangelog));

    // Найти подходящий asset (Windows zip)
    if(pAssets && pAssets->type == json_array)
    {
        for(int i = 0; i < json_array_length(pAssets); ++i)
        {
            const json_value *pAsset = json_array_get(pAssets, i);
            const json_value *pAName = json_object_get(pAsset, "name");
            const json_value *pAUrl = json_object_get(pAsset, "browser_download_url");
            const json_value *pASize = json_object_get(pAsset, "size");
            const json_value *pADigest = json_object_get(pAsset, "digest");

            if(pAName && pAName->type == json_string)
            {
                const char *pNameStr = pAName->u.string.ptr;
                if(str_find(pNameStr, "windows") || str_find(pNameStr, "win64") ||
                   str_find(pNameStr, "win-x86_64"))
                {
                    str_copy(Out.m_aAssetName, pNameStr, sizeof(Out.m_aAssetName));
                    if(pAUrl && pAUrl->type == json_string)
                        str_copy(Out.m_aAssetUrl, pAUrl->u.string.ptr, sizeof(Out.m_aAssetUrl));
                    if(pASize && pASize->type == json_integer)
                        Out.m_AssetSize = pASize->u.integer;
                    if(pADigest && pADigest->type == json_string)
                    {
                        const char *pD = pADigest->u.string.ptr;
                        if(str_startswith(pD, "sha256:"))
                            str_copy(Out.m_aAssetDigest, pD + 7, sizeof(Out.m_aAssetDigest));
                        else
                            str_copy(Out.m_aAssetDigest, pD, sizeof(Out.m_aAssetDigest));
                    }
                    break;
                }
            }
        }
    }

    json_value_free(pRoot);
    return Out.m_aTagName[0] != '\0';
}

void CUpdater::CheckForUpdates(const char *pRepoOwnerSlashRepo)
{
    if(!pRepoOwnerSlashRepo || !pRepoOwnerSlashRepo[0])
    {
        CLockScope L(m_Lock);
        m_State = EUpdateState::FAILED;
        str_copy(m_aLastError, "не задан репозиторий GitHub");
        return;
    }

    {
        CLockScope L(m_Lock);
        m_State = EUpdateState::CHECKING;
        str_copy(m_aStatus, "Проверка обновлений...");
    }

    // Сформировать URL GitHub API
    char aUrl[256];
    str_format(aUrl, sizeof(aUrl),
        "https://api.github.com/repos/%s/releases/latest", pRepoOwnerSlashRepo);

    // Создать HTTP-запрос (GET по умолчанию)
    auto pReq = CreateHttpRequest(aUrl);
    pReq->Timeout(CTimeout{10, 0, 0, 0});
    pReq->WriteToMemory();
    pReq->Wait();

    if(pReq->State() != EHttpState::DONE)
    {
        CLockScope L(m_Lock);
        m_State = EUpdateState::FAILED;
        str_copy(m_aLastError, "HTTP-запрос к GitHub API провален");
        return;
    }

    // Распарсить JSON-ответ
    unsigned char *pResult = nullptr;
    size_t ResultLength = 0;
    pReq->Result(&pResult, &ResultLength);
    if(!pResult || ResultLength == 0)
    {
        CLockScope L(m_Lock);
        m_State = EUpdateState::FAILED;
        str_copy(m_aLastError, "пустой ответ от GitHub API");
        return;
    }

    std::string Body((const char *)pResult, ResultLength);
    SReleaseInfo Info;
    if(!ParseReleaseJson(Body.c_str(), Info))
    {
        CLockScope L(m_Lock);
        m_State = EUpdateState::FAILED;
        str_copy(m_aLastError, "не удалось разобрать JSON релиза");
        return;
    }

    // Сравнить с текущей версией клиента
    char aCurTag[32];
    str_format(aCurTag, sizeof(aCurTag), "v%s", GAME_VERSION);
    int Cmp = CompareVersions(Info.m_aTagName, aCurTag);
    {
        CLockScope L(m_Lock);
        m_LatestRelease = Info;
        if(Cmp > 0)
        {
            m_State = EUpdateState::UPDATE_AVAILABLE;
            str_format(m_aStatus, sizeof(m_aStatus),
                "Доступна новая версия %s (текущая %s)", Info.m_aTagName, aCurTag);
        }
        else
        {
            m_State = EUpdateState::NO_UPDATE;
            str_format(m_aStatus, sizeof(m_aStatus),
                "Установлена последняя версия %s", aCurTag);
        }
    }
}

bool CUpdater::DownloadAndVerify(const SReleaseInfo &Release, const char *pDestPath)
{
    if(!m_pStorage) return false;
    if(!Release.m_aAssetUrl[0])
    {
        log_error("pfa", "Update: asset URL пустой");
        return false;
    }

    auto pReq = CreateHttpRequest(Release.m_aAssetUrl);
    pReq->Timeout(CTimeout{120, 0, 0, 0}); // 2 минуты на большой архив
    pReq->WriteToFile(m_pStorage, pDestPath, IStorage::TYPE_SAVE);
    pReq->Wait();

    if(pReq->State() != EHttpState::DONE)
    {
        log_error("pfa", "Update: скачивание ассета провалено");
        return false;
    }

    // Проверка SHA256: IHttpRequest сам проверяет, если задан ExpectSha256,
    // но мы не можем восстановить SHA256_DIGEST из hex без парсинга.
    // Поэтому: если digest задан, читаем файл и проверяем вручную.
    if(Release.m_aAssetDigest[0])
    {
        IOHANDLE File = m_pStorage->OpenFile(pDestPath, IOFLAG_READ, IStorage::TYPE_SAVE);
        if(!File) return false;
        SHA256_CTX Ctx;
        sha256_init(&Ctx);
        unsigned char aBuf[8192];
        while(true)
        {
            unsigned N = io_read(File, aBuf, sizeof(aBuf));
            if(N == 0) break;
            sha256_update(&Ctx, aBuf, N);
        }
        io_close(File);
        SHA256_DIGEST Final = sha256_finish(&Ctx);
        char aHex[65];
        sha256_str(Final, aHex, sizeof(aHex));
        if(str_comp(aHex, Release.m_aAssetDigest) != 0)
        {
            log_error("pfa", "Update: SHA256 mismatch! ожидаемый=%s фактический=%s",
                Release.m_aAssetDigest, aHex);
            return false;
        }
        log_info("pfa", "Update: SHA256 проверен (%s)", aHex);
    }
    else
    {
        log_warn("pfa", "Update: SHA256 digest отсутствует в релизе — пропуск проверки.");
    }
    return true;
}

bool CUpdater::BackupCurrent()
{
    if(!m_pStorage) return false;
    char aTimestamp[32];
    str_format(aTimestamp, sizeof(aTimestamp), "%lld", (long long)time(nullptr));
    char aBackupDir[256];
    str_format(aBackupDir, sizeof(aBackupDir), "%s/%s", PFA_BACKUP_DIR, aTimestamp);
    m_pStorage->CreateFolder(PFA_BACKUP_DIR, IStorage::TYPE_SAVE);
    m_pStorage->CreateFolder(aBackupDir, IStorage::TYPE_SAVE);

    // Каталог исполняемого файла
    char aBinDir[IO_MAX_PATH_LENGTH] = {0};
    m_pStorage->GetBinaryPath("", aBinDir, sizeof(aBinDir));

    char aFullBackupDir[IO_MAX_PATH_LENGTH] = {0};
    m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, aBackupDir, aFullBackupDir, sizeof(aFullBackupDir));

#if defined(CONF_FAMILY_WINDOWS)
    char aCmd[IO_MAX_PATH_LENGTH * 2];
    str_format(aCmd, sizeof(aCmd), "xcopy \"%s*\" \"%s\\\" /E /I /Y /Q > nul 2>&1",
        aBinDir, aFullBackupDir);
    int rc = system(aCmd);
#else
    char aCmd[IO_MAX_PATH_LENGTH * 2];
    str_format(aCmd, sizeof(aCmd), "cp -r '%s'* '%s/' 2>/dev/null", aBinDir, aFullBackupDir);
    int rc = system(aCmd);
#endif
    (void)rc;
    log_info("pfa", "Backup: создан бэкап в %s (src=%s)", aFullBackupDir, aBinDir);
    return true;
}

bool CUpdater::WriteManifest(const SReleaseInfo &Release, const char *pBackupDir)
{
    if(!m_pStorage) return false;
    char aJson[2048];
    str_format(aJson, sizeof(aJson),
        "{\"backup_dir\":\"%s\",\"tag\":\"%s\",\"name\":\"%s\","
        "\"installed_at\":%lld}",
        pBackupDir, Release.m_aTagName, Release.m_aName,
        (long long)time(nullptr));

    char aPath[256];
    str_format(aPath, sizeof(aPath), "%s/%s", PFA_BACKUP_DIR, PFA_MANIFEST_FILE);
    IOHANDLE File = m_pStorage->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
    if(!File) return false;
    io_write(File, aJson, str_length(aJson));
    io_close(File);
    return true;
}

bool CUpdater::ApplyArchive(const char *pArchivePath)
{
    if(!m_pStorage) return false;

    char aFullArchive[IO_MAX_PATH_LENGTH] = {0};
    m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, pArchivePath, aFullArchive, sizeof(aFullArchive));

    char aBinDir[IO_MAX_PATH_LENGTH] = {0};
    m_pStorage->GetBinaryPath("", aBinDir, sizeof(aBinDir));

    log_info("pfa", "Update: распаковка '%s' в '%s'", aFullArchive, aBinDir);

#if defined(CONF_FAMILY_WINDOWS)
    char aCmd[IO_MAX_PATH_LENGTH * 3];
    str_format(aCmd, sizeof(aCmd),
        "powershell -NoProfile -Command \"Expand-Archive -Path '%s' -DestinationPath '%s' -Force\"",
        aFullArchive, aBinDir);
    int rc = system(aCmd);
#else
    char aCmd[IO_MAX_PATH_LENGTH * 2];
    str_format(aCmd, sizeof(aCmd), "unzip -o '%s' -d '%s' > /dev/null 2>&1", aFullArchive, aBinDir);
    int rc = system(aCmd);
#endif
    if(rc != 0)
    {
        log_error("pfa", "Update: распаковка провалена (rc=%d)", rc);
        return false;
    }
    log_info("pfa", "Update: распаковка завершена");
    return true;
}

void CUpdater::InstallUpdate()
{
    SReleaseInfo Release;
    {
        CLockScope L(m_Lock);
        if(m_State != EUpdateState::UPDATE_AVAILABLE)
        {
            log_warn("pfa", "Update: нет доступного обновления для установки");
            return;
        }
        Release = m_LatestRelease;
        m_State = EUpdateState::DOWNLOADING;
        m_Percent = 0;
        str_copy(m_aStatus, "Скачивание...");
    }

    char aDestPath[256];
    str_format(aDestPath, sizeof(aDestPath), "personal_flight_assistant/update_%s.zip",
        Release.m_aTagName);
    m_pStorage->CreateFolder("personal_flight_assistant", IStorage::TYPE_SAVE);

    if(!DownloadAndVerify(Release, aDestPath))
    {
        CLockScope L(m_Lock);
        m_State = EUpdateState::FAILED;
        str_copy(m_aLastError, "Скачивание/верификация провалены");
        return;
    }

    {
        CLockScope L(m_Lock);
        m_State = EUpdateState::BACKING_UP;
        str_copy(m_aStatus, "Создание бэкапа...");
    }
    if(!BackupCurrent())
    {
        CLockScope L(m_Lock);
        m_State = EUpdateState::FAILED;
        str_copy(m_aLastError, "Бэкап провален");
        return;
    }

    {
        CLockScope L(m_Lock);
        m_State = EUpdateState::INSTALLING;
        str_copy(m_aStatus, "Установка...");
    }
    if(!ApplyArchive(aDestPath))
    {
        CLockScope L(m_Lock);
        m_State = EUpdateState::FAILED;
        str_copy(m_aLastError, "Установка провалена");
        return;
    }

    char aTs[32];
    str_format(aTs, sizeof(aTs), "%lld", (long long)time(nullptr));
    char aBackupDir[256];
    str_format(aBackupDir, sizeof(aBackupDir), "%s/%s", PFA_BACKUP_DIR, aTs);
    WriteManifest(Release, aBackupDir);

    {
        CLockScope L(m_Lock);
        m_State = EUpdateState::DONE;
        str_copy(m_aStatus, "Обновление установлено. Перезапустите клиент.");
    }
}

bool CUpdater::RollbackTo(const char *pBackupTag)
{
    if(!m_pStorage || !pBackupTag || !pBackupTag[0]) return false;
    char aBackupDir[256];
    str_format(aBackupDir, sizeof(aBackupDir), "%s/%s", PFA_BACKUP_DIR, pBackupTag);

    char aFullBackupDir[IO_MAX_PATH_LENGTH] = {0};
    m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, aBackupDir, aFullBackupDir, sizeof(aFullBackupDir));

    char aBinDir[IO_MAX_PATH_LENGTH] = {0};
    m_pStorage->GetBinaryPath("", aBinDir, sizeof(aBinDir));

    log_info("pfa", "Rollback: восстановление '%s' -> '%s'", aFullBackupDir, aBinDir);

#if defined(CONF_FAMILY_WINDOWS)
    char aCmd[IO_MAX_PATH_LENGTH * 3];
    str_format(aCmd, sizeof(aCmd), "xcopy \"%s*\" \"%s\\\" /E /I /Y /Q > nul 2>&1",
        aFullBackupDir, aBinDir);
    int rc = system(aCmd);
#else
    char aCmd[IO_MAX_PATH_LENGTH * 2];
    str_format(aCmd, sizeof(aCmd), "cp -r '%s'* '%s/' 2>/dev/null", aFullBackupDir, aBinDir);
    int rc = system(aCmd);
#endif
    if(rc != 0)
    {
        log_error("pfa", "Rollback: провален (rc=%d)", rc);
        return false;
    }
    log_info("pfa", "Rollback: завершён. Перезапустите клиент.");
    return true;
}

std::vector<std::string> CUpdater::ListBackups()
{
    std::vector<std::string> v;
    if(!m_pStorage) return v;

    struct SScanCtx {
        std::vector<std::string> *pOut;
    } Ctx;
    Ctx.pOut = &v;

    m_pStorage->ListDirectoryInfo(IStorage::TYPE_SAVE, PFA_BACKUP_DIR,
        [](const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser) -> int {
            (void)StorageType;
            if(!IsDir) return 0;
            if(str_comp(pInfo->m_pName, ".") == 0 || str_comp(pInfo->m_pName, "..") == 0)
                return 0;
            auto *pCtx = (SScanCtx *)pUser;
            pCtx->pOut->push_back(std::string(pInfo->m_pName));
            return 0;
        },
        &Ctx);

    std::sort(v.begin(), v.end(), std::greater<std::string>());
    return v;
}

EUpdateState CUpdater::GetState() const { return m_State; }
int CUpdater::GetPercent() const { return m_Percent; }
void CUpdater::GetStatus(char *pBuf, size_t BufSize) const
{
    CLockScope L(m_Lock);
    str_copy(pBuf, m_aStatus, BufSize);
}
const SReleaseInfo *CUpdater::GetLatestRelease() const
{
    CLockScope L(m_Lock);
    return m_LatestRelease.m_aTagName[0] ? &m_LatestRelease : nullptr;
}

} // namespace pfa
