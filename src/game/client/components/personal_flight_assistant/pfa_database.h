/**
 * @file pfa_database.h
 * @brief SQLite-хранилище для Personal Flight Assistant.
 *
 * Использует существующий движковый wrapper engine/sqlite.h
 * (CSqlite, CSqliteStmt, SqliteOpen, SqlitePrepare).
 * Файл БД: <storage_user>/personal_flight_assistant/pfa.sqlite
 *
 * Схема БД миграционная: каждое изменение схемы фиксируется в таблице
 * schema_version, и при запуске применяются недостающие миграции.
 * Это позволяет переживать обновления клиента без потери данных.
 *
 * Раздел ТЗ: 1. Статистика собранных данных, 6. Хранение данных.
 */

#ifndef GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_DATABASE_H
#define GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_DATABASE_H

#include "pfa_types.h"

#include <engine/sqlite.h>
#include <engine/storage.h>

#include <memory>
#include <string>
#include <vector>

class IStorage;
class CLock;

namespace pfa {

class CDatabase {
    IStorage *m_pStorage;
    CSqlite m_Db;
    char m_aDbPath[256];

    // Простой мьютекс для сериализации доступа из фонового потока анализа
    std::unique_ptr<CLock> m_pLock;

    bool Migrate();
    bool ExecuteSimple(const char *pSql);
    int64_t LastInsertRowId();

public:
    CDatabase();
    ~CDatabase();

    /**
     * Открыть БД. Создаёт каталог и файл, если их нет.
     * Применяет ожидающие миграции.
     * @return true при успехе.
     */
    bool Init(IStorage *pStorage);

    /**
     * Закрыть БД (вызывается в деструкторе, но можно явно).
     */
    void Shutdown();

    // ---- Управление демками ----

    /** Добавить запись о демке (без полного разбора). Возвращает ID. */
    int64_t AddDemo(const SDemoMeta &Demo);

    /** Обновить метаданные после разбора. */
    bool UpdateDemoParsed(int64_t DemoId, int32_t NumAttempts, int32_t NumSuccess, bool bParsed);

    /** Удалить демку и все связанные попытки/треки. */
    bool RemoveDemo(int64_t DemoId);

    /** Получить список всех демок (для UI). */
    std::vector<SDemoMeta> ListDemos();

    /** Найти демку по пути — для проверки дублей. */
    bool FindDemoByPath(const char *pPath, SDemoMeta &Out);

    // ---- Треки тиев ----

    /** Сохранить трек тия (blob). */
    bool AddTeeTrack(const STeeTrack &Track);

    /** Получить все треки для демки. */
    std::vector<STeeTrack> LoadTracksForDemo(int64_t DemoId);

    // ---- Попытки псевдофлая ----

    /** Добавить попытку псевдофлая. Возвращает ID. */
    int64_t AddAttempt(const SPseudoFlyAttempt &Attempt);

    /** Удалить все попытки для демки. */
    bool RemoveAttemptsForDemo(int64_t DemoId);

    /**
     * Найти k ближайших (по геометрии старта) успешных попыток.
     * Используется kNN-предиктором.
     */
    std::vector<SPseudoFlyAttempt> FindNearestSuccessful(
        int32_t Distance, int32_t RelAngle,
        int32_t VelX1, int32_t VelY1,
        int32_t VelX2, int32_t VelY2,
        int ProfileId, int k);

    /**
     * Загрузить ВСЕ попытки указанного профиля (и default).
     * @param ProfileId — ID профиля; если 0 — только default.
     * @param bIncludeDefault — включать default (profile_id=0) в результат.
     * Используется статистикой для ComputeGauss (только успехи)
     * и для подсчёта m_TotalAttempts (все).
     */
    std::vector<SPseudoFlyAttempt> ListAllAttempts(int ProfileId, bool bIncludeDefault = true);

    // ---- Профили ----

    std::vector<SProfile> ListProfiles();
    int64_t AddOrUpdateProfile(const SProfile &Profile);
    bool RemoveProfile(int64_t ProfileId);

    // ---- Статистика ----

    struct SStatistics {
        int32_t m_NumDemos;
        int32_t m_NumAttempts;
        int32_t m_NumSuccess;
        int32_t m_NumProfiles;
        int64_t m_LastAnalysisMs; // unix ms последнего анализа
        float   m_SuccessRate;    // 0..1
        int32_t m_AvgStartDistance;
        int32_t m_AvgDurationTicks;
    };

    /** Получить агрегированную статистику для UI. */
    SStatistics GetStatistics(int ProfileId = 0);

    /** Проверка, что БД открыта. */
    bool IsOpen() const { return m_Db != nullptr; }
};

} // namespace pfa

#endif // GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_DATABASE_H
