/**
 * @file pfa_database.cpp
 * @brief Реализация SQLite-хранилища PFA.
 *
 * Схема БД:
 *
 *   schema_version (version INTEGER PRIMARY KEY, applied_at INTEGER)
 *   demos          (id, filename, map_name, netversion, timestamp,
 *                   length_ticks, num_attempts, num_success, parsed,
 *                   file_size, sha256, imported_at)
 *   tracks          (id, demo_id, player_slot, player_name,
 *                    first_tick, last_tick, samples_blob)
 *   attempts        (id, demo_id, profile_id, start_tick, end_tick,
 *                    start_distance, start_t1x, start_t1y, start_t2x, start_t2y,
 *                    start_t1vx, start_t1vy, start_t2vx, start_t2vy,
 *                    start_rel_angle, start_hook_state, start_hooked_player,
 *                    duration_ticks, peak_distance, end_distance,
 *                    success, failure_reason)
 *   profiles        (id, name, description, attempt_count, success_count,
 *                    last_updated_ms)
 *
 * Миграции применяются последовательно по schema_version.
 */

#include "pfa_database.h"

#include <base/fs.h>
#include <base/hash.h>
#include <base/lock.h>
#include <base/log.h>
#include <base/mem.h>
#include <base/str.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/sqlite.h>

#include <sqlite3.h>

#include <cstring>
#include <ctime>

namespace pfa {

namespace {
constexpr const char *PFA_DB_SUBDIR = "personal_flight_assistant";
constexpr const char *PFA_DB_FILENAME = "pfa.sqlite";

// SQL-инициализация схемы (миграция 1).
constexpr const char *SCHEMA_V1_SQL[] = {
    "CREATE TABLE IF NOT EXISTS schema_version ("
    "  version INTEGER PRIMARY KEY,"
    "  applied_at INTEGER NOT NULL"
    ");",

    "CREATE TABLE IF NOT EXISTS demos ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  filename TEXT NOT NULL UNIQUE,"
    "  map_name TEXT NOT NULL DEFAULT '',"
    "  netversion TEXT NOT NULL DEFAULT '',"
    "  timestamp TEXT NOT NULL DEFAULT '',"
    "  length_ticks INTEGER NOT NULL DEFAULT 0,"
    "  num_attempts INTEGER NOT NULL DEFAULT 0,"
    "  num_success INTEGER NOT NULL DEFAULT 0,"
    "  parsed INTEGER NOT NULL DEFAULT 0,"
    "  file_size INTEGER NOT NULL DEFAULT 0,"
    "  sha256 TEXT NOT NULL DEFAULT '',"
    "  imported_at INTEGER NOT NULL DEFAULT 0"
    ");",

    "CREATE TABLE IF NOT EXISTS tracks ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  demo_id INTEGER NOT NULL,"
    "  player_slot INTEGER NOT NULL,"
    "  player_name TEXT NOT NULL DEFAULT '',"
    "  first_tick INTEGER NOT NULL,"
    "  last_tick INTEGER NOT NULL,"
    "  samples_blob BLOB NOT NULL,"
    "  FOREIGN KEY(demo_id) REFERENCES demos(id) ON DELETE CASCADE"
    ");",

    "CREATE TABLE IF NOT EXISTS attempts ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  demo_id INTEGER NOT NULL,"
    "  profile_id INTEGER NOT NULL DEFAULT 0,"
    "  start_tick INTEGER NOT NULL,"
    "  end_tick INTEGER NOT NULL,"
    "  start_distance INTEGER NOT NULL,"
    "  start_t1x INTEGER NOT NULL,"
    "  start_t1y INTEGER NOT NULL,"
    "  start_t2x INTEGER NOT NULL,"
    "  start_t2y INTEGER NOT NULL,"
    "  start_t1vx INTEGER NOT NULL,"
    "  start_t1vy INTEGER NOT NULL,"
    "  start_t2vx INTEGER NOT NULL,"
    "  start_t2vy INTEGER NOT NULL,"
    "  start_rel_angle INTEGER NOT NULL,"
    "  start_hook_state INTEGER NOT NULL,"
    "  start_hooked_player INTEGER NOT NULL,"
    "  duration_ticks INTEGER NOT NULL,"
    "  peak_distance INTEGER NOT NULL,"
    "  end_distance INTEGER NOT NULL,"
    "  success INTEGER NOT NULL,"
    "  failure_reason TEXT NOT NULL DEFAULT ''"
    ");",

    "CREATE TABLE IF NOT EXISTS profiles ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name TEXT NOT NULL UNIQUE,"
    "  description TEXT NOT NULL DEFAULT '',"
    "  attempt_count INTEGER NOT NULL DEFAULT 0,"
    "  success_count INTEGER NOT NULL DEFAULT 0,"
    "  last_updated_ms INTEGER NOT NULL DEFAULT 0"
    ");",

    // Индексы для быстрых запросов kNN
    "CREATE INDEX IF NOT EXISTS idx_attempts_profile_success "
    "  ON attempts(profile_id, success);",
    "CREATE INDEX IF NOT EXISTS idx_attempts_start_distance "
    "  ON attempts(start_distance);",
    "CREATE INDEX IF NOT EXISTS idx_tracks_demo "
    "  ON tracks(demo_id);",

    // Профиль "default" создаётся автоматически
    "INSERT OR IGNORE INTO profiles (id, name, description) "
    "  VALUES (0, 'default', 'Все попытки без специальной классификации');",

    nullptr,
};
} // namespace

CDatabase::CDatabase() : m_pStorage(nullptr)
{
    m_aDbPath[0] = '\0';
    m_pLock = std::make_unique<CLock>();
}

CDatabase::~CDatabase()
{
    Shutdown();
}

bool CDatabase::Init(IStorage *pStorage)
{
    m_pStorage = pStorage;
    if(!m_pStorage)
    {
        log_error("pfa", "Init: нет storage");
        return false;
    }

    // Создать каталог personal_flight_assistant/ в user storage
    char aDir[256];
    str_format(aDir, sizeof(aDir), "%s", PFA_DB_SUBDIR);
    m_pStorage->CreateFolder(aDir, IStorage::TYPE_SAVE);

    // Полный путь к БД
    char aFullPath[512];
    str_format(aFullPath, sizeof(aFullPath), "%s/%s", PFA_DB_SUBDIR, PFA_DB_FILENAME);
    m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, aFullPath, m_aDbPath, sizeof(m_aDbPath));

    // Открыть БД
    m_Db = SqliteOpen(m_pStorage, aFullPath);
    if(!m_Db)
    {
        log_error("pfa", "Init: не удалось открыть БД '%s'", m_aDbPath);
        return false;
    }

    // Включить foreign keys (для каскадного удаления)
    ExecuteSimple("PRAGMA foreign_keys = ON;");
    ExecuteSimple("PRAGMA journal_mode = WAL;");
    ExecuteSimple("PRAGMA synchronous = NORMAL;");

    if(!Migrate())
    {
        log_error("pfa", "Init: миграция схемы провалена");
        return false;
    }

    log_info("pfa", "БД открыта: %s", m_aDbPath);
    return true;
}

void CDatabase::Shutdown()
{
    // CSqlite (unique_ptr) сам закроет дескриптор.
    m_Db.reset();
    m_pStorage = nullptr;
}

bool CDatabase::ExecuteSimple(const char *pSql)
{
    if(!m_Db) return false;
    char *pErr = nullptr;
    int rc = sqlite3_exec(m_Db.get(), pSql, nullptr, nullptr, &pErr);
    if(rc != SQLITE_OK)
    {
        log_error("pfa", "SQL error: %s — запрос: %s", pErr ? pErr : "?", pSql);
        sqlite3_free(pErr);
        return false;
    }
    return true;
}

bool CDatabase::Migrate()
{
    // Создать таблицу schema_version, если её нет
    if(!ExecuteSimple(SCHEMA_V1_SQL[0]))
        return false;

    // Проверить текущую версию
    CSqliteStmt Stmt = SqlitePrepare(m_Db.get(), "SELECT MAX(version) FROM schema_version;");
    if(!Stmt) return false;

    int CurrentVersion = 0;
    if(sqlite3_step(Stmt.get()) == SQLITE_ROW)
    {
        CurrentVersion = sqlite3_column_int(Stmt.get(), 0);
        if(sqlite3_column_type(Stmt.get(), 0) == SQLITE_NULL)
            CurrentVersion = 0;
    }

    // Применить миграцию 1 (единственная на данный момент)
    if(CurrentVersion < 1)
    {
        for(int i = 1; SCHEMA_V1_SQL[i] != nullptr; ++i)
        {
            if(!ExecuteSimple(SCHEMA_V1_SQL[i]))
            {
                log_error("pfa", "Миграция V1 провалилась на шаге %d", i);
                return false;
            }
        }
        // Зафиксировать применение
        char aBuf[128];
        str_format(aBuf, sizeof(aBuf),
            "INSERT INTO schema_version (version, applied_at) VALUES (1, %lld);",
            (long long)time(nullptr));
        ExecuteSimple(aBuf);
        log_info("pfa", "Применена миграция схемы БД v1");
    }

    return true;
}

int64_t CDatabase::LastInsertRowId()
{
    if(!m_Db) return 0;
    return (int64_t)sqlite3_last_insert_rowid(m_Db.get());
}

// ---- Демки ----

int64_t CDatabase::AddDemo(const SDemoMeta &Demo)
{
    if(!m_Db) return 0;
    CLockScope Lock(*m_pLock);

    CSqliteStmt Stmt = SqlitePrepare(m_Db.get(),
        "INSERT OR REPLACE INTO demos "
        "(filename, map_name, netversion, timestamp, length_ticks, "
        " num_attempts, num_success, parsed, file_size, sha256, imported_at) "
        "VALUES (?, ?, ?, ?, ?, 0, 0, 0, ?, ?, ?);");
    if(!Stmt) return 0;

    sqlite3_bind_text(Stmt.get(), 1, Demo.m_aFilename, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(Stmt.get(), 2, Demo.m_aMapName, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(Stmt.get(), 3, Demo.m_aNetversion, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(Stmt.get(), 4, Demo.m_aTimestamp, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(Stmt.get(), 5, Demo.m_LengthTicks);
    sqlite3_bind_int64(Stmt.get(), 6, Demo.m_FileSize);
    sqlite3_bind_text(Stmt.get(), 7, Demo.m_aSha256, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(Stmt.get(), 8, Demo.m_ImportedAt);

    int rc = sqlite3_step(Stmt.get());
    if(rc != SQLITE_DONE)
    {
        log_error("pfa", "AddDemo: step failed rc=%d", rc);
        return 0;
    }
    return LastInsertRowId();
}

bool CDatabase::UpdateDemoParsed(int64_t DemoId, int32_t NumAttempts, int32_t NumSuccess, bool bParsed)
{
    if(!m_Db) return false;
    CLockScope Lock(*m_pLock);
    CSqliteStmt Stmt = SqlitePrepare(m_Db.get(),
        "UPDATE demos SET num_attempts=?, num_success=?, parsed=? WHERE id=?;");
    if(!Stmt) return false;
    sqlite3_bind_int(Stmt.get(), 1, NumAttempts);
    sqlite3_bind_int(Stmt.get(), 2, NumSuccess);
    sqlite3_bind_int(Stmt.get(), 3, bParsed ? 1 : 0);
    sqlite3_bind_int64(Stmt.get(), 4, DemoId);
    return sqlite3_step(Stmt.get()) == SQLITE_DONE;
}

bool CDatabase::RemoveDemo(int64_t DemoId)
{
    if(!m_Db) return false;
    CLockScope Lock(*m_pLock);
    // Cascade должен удалить tracks и attempts, но SQLite требует
    // включённого PRAGMA foreign_keys (мы включили в Init).
    // На всякий случай удалим явно.
    {
        CSqliteStmt S = SqlitePrepare(m_Db.get(), "DELETE FROM tracks WHERE demo_id=?;");
        if(!S) return false;
        sqlite3_bind_int64(S.get(), 1, DemoId);
        sqlite3_step(S.get());
    }
    {
        CSqliteStmt S = SqlitePrepare(m_Db.get(), "DELETE FROM attempts WHERE demo_id=?;");
        if(!S) return false;
        sqlite3_bind_int64(S.get(), 1, DemoId);
        sqlite3_step(S.get());
    }
    {
        CSqliteStmt S = SqlitePrepare(m_Db.get(), "DELETE FROM demos WHERE id=?;");
        if(!S) return false;
        sqlite3_bind_int64(S.get(), 1, DemoId);
        return sqlite3_step(S.get()) == SQLITE_DONE;
    }
}

std::vector<SDemoMeta> CDatabase::ListDemos()
{
    std::vector<SDemoMeta> v;
    if(!m_Db) return v;
    CLockScope Lock(*m_pLock);

    CSqliteStmt Stmt = SqlitePrepare(m_Db.get(),
        "SELECT id, filename, map_name, netversion, timestamp, length_ticks, "
        "       num_attempts, num_success, parsed, file_size, sha256, imported_at "
        "FROM demos ORDER BY imported_at DESC;");
    if(!Stmt) return v;

    while(sqlite3_step(Stmt.get()) == SQLITE_ROW)
    {
        SDemoMeta D{};
        D.m_Id = sqlite3_column_int64(Stmt.get(), 0);
        const unsigned char *pFn = sqlite3_column_text(Stmt.get(), 1);
        str_copy(D.m_aFilename, pFn ? (const char *)pFn : "");
        const unsigned char *pMap = sqlite3_column_text(Stmt.get(), 2);
        str_copy(D.m_aMapName, pMap ? (const char *)pMap : "");
        const unsigned char *pNet = sqlite3_column_text(Stmt.get(), 3);
        str_copy(D.m_aNetversion, pNet ? (const char *)pNet : "");
        const unsigned char *pTs = sqlite3_column_text(Stmt.get(), 4);
        str_copy(D.m_aTimestamp, pTs ? (const char *)pTs : "");
        D.m_LengthTicks = sqlite3_column_int(Stmt.get(), 5);
        D.m_NumAttempts = sqlite3_column_int(Stmt.get(), 6);
        D.m_NumSuccess = sqlite3_column_int(Stmt.get(), 7);
        D.m_bParsed = sqlite3_column_int(Stmt.get(), 8) != 0;
        D.m_FileSize = sqlite3_column_int64(Stmt.get(), 9);
        const unsigned char *pSha = sqlite3_column_text(Stmt.get(), 10);
        str_copy(D.m_aSha256, pSha ? (const char *)pSha : "");
        D.m_ImportedAt = sqlite3_column_int64(Stmt.get(), 11);
        v.push_back(D);
    }
    return v;
}

bool CDatabase::FindDemoByPath(const char *pPath, SDemoMeta &Out)
{
    if(!m_Db) return false;
    CLockScope Lock(*m_pLock);
    CSqliteStmt Stmt = SqlitePrepare(m_Db.get(),
        "SELECT id, filename, parsed FROM demos WHERE filename=? LIMIT 1;");
    if(!Stmt) return false;
    sqlite3_bind_text(Stmt.get(), 1, pPath, -1, SQLITE_TRANSIENT);
    if(sqlite3_step(Stmt.get()) == SQLITE_ROW)
    {
        Out.m_Id = sqlite3_column_int64(Stmt.get(), 0);
        str_copy(Out.m_aFilename, pPath);
        Out.m_bParsed = sqlite3_column_int(Stmt.get(), 2) != 0;
        return true;
    }
    return false;
}

// ---- Треки ----

bool CDatabase::AddTeeTrack(const STeeTrack &Track)
{
    if(!m_Db) return false;
    CLockScope Lock(*m_pLock);

    CSqliteStmt Stmt = SqlitePrepare(m_Db.get(),
        "INSERT INTO tracks (demo_id, player_slot, player_name, first_tick, last_tick, samples_blob) "
        "VALUES (?, ?, ?, ?, ?, ?);");
    if(!Stmt) return false;

    sqlite3_bind_int64(Stmt.get(), 1, Track.m_DemoId);
    sqlite3_bind_int(Stmt.get(), 2, Track.m_PlayerSlot);
    sqlite3_bind_text(Stmt.get(), 3, Track.m_aPlayerName, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(Stmt.get(), 4, Track.m_FirstTick);
    sqlite3_bind_int(Stmt.get(), 5, Track.m_LastTick);

    const void *pBlob = Track.m_vSamples.data();
    int BlobSize = (int)(Track.m_vSamples.size() * sizeof(STeeSample));
    sqlite3_bind_blob(Stmt.get(), 6, pBlob, BlobSize, SQLITE_TRANSIENT);

    return sqlite3_step(Stmt.get()) == SQLITE_DONE;
}

std::vector<STeeTrack> CDatabase::LoadTracksForDemo(int64_t DemoId)
{
    std::vector<STeeTrack> v;
    if(!m_Db) return v;
    CLockScope Lock(*m_pLock);
    CSqliteStmt Stmt = SqlitePrepare(m_Db.get(),
        "SELECT player_slot, player_name, first_tick, last_tick, samples_blob "
        "FROM tracks WHERE demo_id=?;");
    if(!Stmt) return v;
    sqlite3_bind_int64(Stmt.get(), 1, DemoId);

    while(sqlite3_step(Stmt.get()) == SQLITE_ROW)
    {
        STeeTrack T{};
        T.m_DemoId = DemoId;
        T.m_PlayerSlot = (int8_t)sqlite3_column_int(Stmt.get(), 0);
        const unsigned char *pName = sqlite3_column_text(Stmt.get(), 1);
        str_copy(T.m_aPlayerName, pName ? (const char *)pName : "");
        T.m_FirstTick = sqlite3_column_int(Stmt.get(), 2);
        T.m_LastTick = sqlite3_column_int(Stmt.get(), 3);

        const void *pBlob = sqlite3_column_blob(Stmt.get(), 4);
        int BlobSize = sqlite3_column_bytes(Stmt.get(), 4);
        int nSamples = BlobSize / (int)sizeof(STeeSample);
        T.m_vSamples.resize(nSamples);
        if(nSamples > 0 && pBlob)
            mem_copy(T.m_vSamples.data(), pBlob, nSamples * sizeof(STeeSample));
        v.push_back(std::move(T));
    }
    return v;
}

// ---- Попытки ----

int64_t CDatabase::AddAttempt(const SPseudoFlyAttempt &A)
{
    if(!m_Db) return 0;
    CLockScope Lock(*m_pLock);

    CSqliteStmt Stmt = SqlitePrepare(m_Db.get(),
        "INSERT INTO attempts "
        "(demo_id, profile_id, start_tick, end_tick, start_distance, "
        " start_t1x, start_t1y, start_t2x, start_t2y, "
        " start_t1vx, start_t1vy, start_t2vx, start_t2vy, "
        " start_rel_angle, start_hook_state, start_hooked_player, "
        " duration_ticks, peak_distance, end_distance, success, failure_reason) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    if(!Stmt) return 0;

    sqlite3_bind_int64(Stmt.get(), 1, A.m_DemoId);
    sqlite3_bind_int(Stmt.get(), 2, A.m_ProfileId);
    sqlite3_bind_int(Stmt.get(), 3, A.m_StartTick);
    sqlite3_bind_int(Stmt.get(), 4, A.m_EndTick);
    sqlite3_bind_int(Stmt.get(), 5, A.m_StartDistance);
    sqlite3_bind_int(Stmt.get(), 6, A.m_StartTee1X);
    sqlite3_bind_int(Stmt.get(), 7, A.m_StartTee1Y);
    sqlite3_bind_int(Stmt.get(), 8, A.m_StartTee2X);
    sqlite3_bind_int(Stmt.get(), 9, A.m_StartTee2Y);
    sqlite3_bind_int(Stmt.get(), 10, A.m_StartTee1VelX);
    sqlite3_bind_int(Stmt.get(), 11, A.m_StartTee1VelY);
    sqlite3_bind_int(Stmt.get(), 12, A.m_StartTee2VelX);
    sqlite3_bind_int(Stmt.get(), 13, A.m_StartTee2VelY);
    sqlite3_bind_int(Stmt.get(), 14, A.m_StartRelativeAngle);
    sqlite3_bind_int(Stmt.get(), 15, A.m_StartHookState);
    sqlite3_bind_int(Stmt.get(), 16, A.m_StartHookedPlayer);
    sqlite3_bind_int(Stmt.get(), 17, A.m_DurationTicks);
    sqlite3_bind_int(Stmt.get(), 18, A.m_PeakDistance);
    sqlite3_bind_int(Stmt.get(), 19, A.m_EndDistance);
    sqlite3_bind_int(Stmt.get(), 20, A.m_bSuccess ? 1 : 0);
    sqlite3_bind_text(Stmt.get(), 21, A.m_aFailureReason, -1, SQLITE_TRANSIENT);

    if(sqlite3_step(Stmt.get()) != SQLITE_DONE)
        return 0;
    return LastInsertRowId();
}

bool CDatabase::RemoveAttemptsForDemo(int64_t DemoId)
{
    if(!m_Db) return false;
    CLockScope Lock(*m_pLock);
    CSqliteStmt S = SqlitePrepare(m_Db.get(), "DELETE FROM attempts WHERE demo_id=?;");
    if(!S) return false;
    sqlite3_bind_int64(S.get(), 1, DemoId);
    return sqlite3_step(S.get()) == SQLITE_DONE;
}

std::vector<SPseudoFlyAttempt> CDatabase::FindNearestSuccessful(
    int32_t Distance, int32_t RelAngle,
    int32_t VelX1, int32_t VelY1,
    int32_t VelX2, int32_t VelY2,
    int ProfileId, int k)
{
    std::vector<SPseudoFlyAttempt> v;
    if(!m_Db) return v;
    CLockScope Lock(*m_pLock);

    // SQLite не имеет встроенного kNN, но мы можем отсортировать по
    // приближённой метрике через SQL-выражение и взять TOP k.
    // Метрика: |start_distance - ?| + |start_rel_angle - ?| / 100
    // (грубая, но для первой версии достаточно; точный kNN делается в C++).
    const char *pSql =
        "SELECT id, demo_id, start_tick, end_tick, start_distance, "
        "       start_t1x, start_t1y, start_t2x, start_t2y, "
        "       start_t1vx, start_t1vy, start_t2vx, start_t2vy, "
        "       start_rel_angle, start_hook_state, start_hooked_player, "
        "       duration_ticks, peak_distance, end_distance, success, failure_reason "
        "FROM attempts "
        "WHERE success=1 AND (profile_id=? OR profile_id=0) "
        "ORDER BY (ABS(start_distance - ?) + ABS(start_rel_angle - ?) / 100) ASC "
        "LIMIT ?;";

    CSqliteStmt Stmt = SqlitePrepare(m_Db.get(), pSql);
    if(!Stmt) return v;
    sqlite3_bind_int(Stmt.get(), 1, ProfileId);
    sqlite3_bind_int(Stmt.get(), 2, Distance);
    sqlite3_bind_int(Stmt.get(), 3, RelAngle);
    sqlite3_bind_int(Stmt.get(), 4, k);

    while(sqlite3_step(Stmt.get()) == SQLITE_ROW)
    {
        SPseudoFlyAttempt A{};
        A.m_Id = sqlite3_column_int64(Stmt.get(), 0);
        A.m_DemoId = sqlite3_column_int64(Stmt.get(), 1);
        A.m_StartTick = sqlite3_column_int(Stmt.get(), 2);
        A.m_EndTick = sqlite3_column_int(Stmt.get(), 3);
        A.m_StartDistance = sqlite3_column_int(Stmt.get(), 4);
        A.m_StartTee1X = sqlite3_column_int(Stmt.get(), 5);
        A.m_StartTee1Y = sqlite3_column_int(Stmt.get(), 6);
        A.m_StartTee2X = sqlite3_column_int(Stmt.get(), 7);
        A.m_StartTee2Y = sqlite3_column_int(Stmt.get(), 8);
        A.m_StartTee1VelX = sqlite3_column_int(Stmt.get(), 9);
        A.m_StartTee1VelY = sqlite3_column_int(Stmt.get(), 10);
        A.m_StartTee2VelX = sqlite3_column_int(Stmt.get(), 11);
        A.m_StartTee2VelY = sqlite3_column_int(Stmt.get(), 12);
        A.m_StartRelativeAngle = sqlite3_column_int(Stmt.get(), 13);
        A.m_StartHookState = (int32_t)sqlite3_column_int(Stmt.get(), 14);
        A.m_StartHookedPlayer = (int8_t)sqlite3_column_int(Stmt.get(), 15);
        A.m_DurationTicks = sqlite3_column_int(Stmt.get(), 16);
        A.m_PeakDistance = sqlite3_column_int(Stmt.get(), 17);
        A.m_EndDistance = sqlite3_column_int(Stmt.get(), 18);
        A.m_bSuccess = sqlite3_column_int(Stmt.get(), 19) != 0;
        const unsigned char *pFr = sqlite3_column_text(Stmt.get(), 20);
        str_copy(A.m_aFailureReason, pFr ? (const char *)pFr : "");
        v.push_back(A);
    }
    return v;
}

std::vector<SPseudoFlyAttempt> CDatabase::ListAllAttempts(int ProfileId, bool bIncludeDefault)
{
    std::vector<SPseudoFlyAttempt> v;
    if(!m_Db) return v;
    CLockScope Lock(*m_pLock);

    const char *pSql;
    if(bIncludeDefault && ProfileId != 0)
    {
        pSql =
            "SELECT id, demo_id, start_tick, end_tick, start_distance, "
            "       start_t1x, start_t1y, start_t2x, start_t2y, "
            "       start_t1vx, start_t1vy, start_t2vx, start_t2vy, "
            "       start_rel_angle, start_hook_state, start_hooked_player, "
            "       duration_ticks, peak_distance, end_distance, success, failure_reason "
            "FROM attempts WHERE profile_id=? OR profile_id=0 "
            "ORDER BY start_tick ASC;";
    }
    else
    {
        pSql =
            "SELECT id, demo_id, start_tick, end_tick, start_distance, "
            "       start_t1x, start_t1y, start_t2x, start_t2y, "
            "       start_t1vx, start_t1vy, start_t2vx, start_t2vy, "
            "       start_rel_angle, start_hook_state, start_hooked_player, "
            "       duration_ticks, peak_distance, end_distance, success, failure_reason "
            "FROM attempts WHERE profile_id=? "
            "ORDER BY start_tick ASC;";
    }

    CSqliteStmt Stmt = SqlitePrepare(m_Db.get(), pSql);
    if(!Stmt) return v;
    sqlite3_bind_int(Stmt.get(), 1, ProfileId);

    while(sqlite3_step(Stmt.get()) == SQLITE_ROW)
    {
        SPseudoFlyAttempt A{};
        A.m_Id = sqlite3_column_int64(Stmt.get(), 0);
        A.m_DemoId = sqlite3_column_int64(Stmt.get(), 1);
        A.m_StartTick = sqlite3_column_int(Stmt.get(), 2);
        A.m_EndTick = sqlite3_column_int(Stmt.get(), 3);
        A.m_StartDistance = sqlite3_column_int(Stmt.get(), 4);
        A.m_StartTee1X = sqlite3_column_int(Stmt.get(), 5);
        A.m_StartTee1Y = sqlite3_column_int(Stmt.get(), 6);
        A.m_StartTee2X = sqlite3_column_int(Stmt.get(), 7);
        A.m_StartTee2Y = sqlite3_column_int(Stmt.get(), 8);
        A.m_StartTee1VelX = sqlite3_column_int(Stmt.get(), 9);
        A.m_StartTee1VelY = sqlite3_column_int(Stmt.get(), 10);
        A.m_StartTee2VelX = sqlite3_column_int(Stmt.get(), 11);
        A.m_StartTee2VelY = sqlite3_column_int(Stmt.get(), 12);
        A.m_StartRelativeAngle = sqlite3_column_int(Stmt.get(), 13);
        A.m_StartHookState = (int32_t)sqlite3_column_int(Stmt.get(), 14);
        A.m_StartHookedPlayer = (int8_t)sqlite3_column_int(Stmt.get(), 15);
        A.m_DurationTicks = sqlite3_column_int(Stmt.get(), 16);
        A.m_PeakDistance = sqlite3_column_int(Stmt.get(), 17);
        A.m_EndDistance = sqlite3_column_int(Stmt.get(), 18);
        A.m_bSuccess = sqlite3_column_int(Stmt.get(), 19) != 0;
        const unsigned char *pFr = sqlite3_column_text(Stmt.get(), 20);
        str_copy(A.m_aFailureReason, pFr ? (const char *)pFr : "");
        v.push_back(A);
    }
    return v;
}

// ---- Профили ----

std::vector<SProfile> CDatabase::ListProfiles()
{
    std::vector<SProfile> v;
    if(!m_Db) return v;
    CLockScope Lock(*m_pLock);
    CSqliteStmt Stmt = SqlitePrepare(m_Db.get(),
        "SELECT id, name, description, attempt_count, success_count, last_updated_ms "
        "FROM profiles ORDER BY id ASC;");
    if(!Stmt) return v;
    while(sqlite3_step(Stmt.get()) == SQLITE_ROW)
    {
        SProfile P{};
        P.m_Id = sqlite3_column_int64(Stmt.get(), 0);
        const unsigned char *pName = sqlite3_column_text(Stmt.get(), 1);
        str_copy(P.m_aName, pName ? (const char *)pName : "");
        const unsigned char *pDesc = sqlite3_column_text(Stmt.get(), 2);
        str_copy(P.m_aDescription, pDesc ? (const char *)pDesc : "");
        P.m_AttemptCount = sqlite3_column_int(Stmt.get(), 3);
        P.m_SuccessCount = sqlite3_column_int(Stmt.get(), 4);
        P.m_LastUpdatedTick = sqlite3_column_int64(Stmt.get(), 5);
        v.push_back(P);
    }
    return v;
}

int64_t CDatabase::AddOrUpdateProfile(const SProfile &Profile)
{
    if(!m_Db) return 0;
    CLockScope Lock(*m_pLock);
    CSqliteStmt Stmt = SqlitePrepare(m_Db.get(),
        "INSERT INTO profiles (name, description, attempt_count, success_count, last_updated_ms) "
        "VALUES (?, ?, 0, 0, 0) "
        "ON CONFLICT(name) DO UPDATE SET description=excluded.description;");
    if(!Stmt) return 0;
    sqlite3_bind_text(Stmt.get(), 1, Profile.m_aName, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(Stmt.get(), 2, Profile.m_aDescription, -1, SQLITE_TRANSIENT);
    if(sqlite3_step(Stmt.get()) != SQLITE_DONE)
        return 0;
    return LastInsertRowId();
}

bool CDatabase::RemoveProfile(int64_t ProfileId)
{
    if(!m_Db || ProfileId == 0) return false; // нельзя удалить default
    CLockScope Lock(*m_pLock);
    CSqliteStmt S = SqlitePrepare(m_Db.get(), "DELETE FROM profiles WHERE id=?;");
    if(!S) return false;
    sqlite3_bind_int64(S.get(), 1, ProfileId);
    return sqlite3_step(S.get()) == SQLITE_DONE;
}

// ---- Статистика ----

CDatabase::SStatistics CDatabase::GetStatistics(int ProfileId)
{
    SStatistics St{};
    if(!m_Db) return St;
    CLockScope Lock(*m_pLock);

    CSqliteStmt D = SqlitePrepare(m_Db.get(), "SELECT COUNT(*), COALESCE(SUM(parsed),0) FROM demos;");
    if(D && sqlite3_step(D.get()) == SQLITE_ROW)
    {
        St.m_NumDemos = sqlite3_column_int(D.get(), 0);
    }

    const char *pSql2 =
        "SELECT COUNT(*), COALESCE(SUM(CASE WHEN success=1 THEN 1 ELSE 0 END),0), "
        "       COALESCE(AVG(start_distance),0), COALESCE(AVG(duration_ticks),0) "
        "FROM attempts WHERE (profile_id=? OR profile_id=0);";

    CSqliteStmt A = SqlitePrepare(m_Db.get(), pSql2);
    if(A)
    {
        sqlite3_bind_int(A.get(), 1, ProfileId);
        if(sqlite3_step(A.get()) == SQLITE_ROW)
        {
            St.m_NumAttempts = sqlite3_column_int(A.get(), 0);
            St.m_NumSuccess = sqlite3_column_int(A.get(), 1);
            St.m_AvgStartDistance = (int32_t)sqlite3_column_double(A.get(), 2);
            St.m_AvgDurationTicks = (int32_t)sqlite3_column_double(A.get(), 3);
        }
    }
    St.m_NumProfiles = (int32_t)ListProfiles().size();
    St.m_SuccessRate = St.m_NumAttempts > 0 ?
        (float)St.m_NumSuccess / (float)St.m_NumAttempts : 0.0f;
    St.m_LastAnalysisMs = (int64_t)time(nullptr) * 1000;
    return St;
}

} // namespace pfa
