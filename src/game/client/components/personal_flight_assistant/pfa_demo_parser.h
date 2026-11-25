/**
 * @file pfa_demo_parser.h
 * @brief Парсер demo-файлов DDNet для извлечения телеметрии тиев.
 *
 * Использует движковый CDemoPlayer для чтения snapshot'ов и
 * проходим по всем тикам, извлекая CNetObj_CharacterCore для каждого
 * клиента. На основе этих данных формирует STeeTrack для каждого тия
 * и определяет попытки псевдофлая (Aled).
 *
 * Раздел ТЗ: 2. Анализ демок.
 */

#ifndef GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_DEMO_PARSER_H
#define GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_DEMO_PARSER_H

#include "pfa_types.h"

#include <engine/demo.h>
#include <engine/shared/snapshot.h>

#include <functional>
#include <string>
#include <vector>

class IStorage;
class CSnapshotDelta;
class IDemoPlayer;

namespace pfa {

/**
 * @brief Результат разбора демки.
 */
struct SParseResult {
    bool m_bOk;
    char m_aError[256];
    int32_t m_NumTicksProcessed;
    int32_t m_NumPlayersFound;
    int32_t m_NumAttemptsDetected;
    int32_t m_NumSuccessDetected;
    std::vector<STeeTrack> m_vTracks;
    std::vector<SPseudoFlyAttempt> m_vAttempts;
};

/**
 * @brief Главный класс парсера.
 *
 * Не зависит от игрового CGameClient — это изолированный модуль,
 * который можно тестировать отдельно. Принимает IStorage и путь к демке.
 */
class CDemoParser {
    IStorage *m_pStorage;
    CSnapshotDelta *m_pSnapshotDelta; // для распаковки delta-snapshots

    // Внутреннее состояние при обработке одной демки
    std::vector<STeeTrack> m_vCurrentTracks;
    int m_NumClients;

    /**
     * Обработать один распакованный snapshot.
     * Извлекает CNetObj_CharacterCore для каждого клиента и
     * добавляет STeeSample в соответствующий STeeTrack.
     */
    void ProcessSnapshot(const CSnapshot *pSnap, int Tick);

    /**
     * Найти в треках все попытки псевдофлая.
     * Алгоритм:
     *   1. Для каждой пары (i, j) тиев:
     *   2. Сканируем треки синхронно по тикам.
     *   3. Определяем момент входа в псевдофлай:
     *      - расстояние < PSEUDOFLY_MAX_DISTANCE
     *      - скорость хотя бы одного тия > PSEUDOFLY_MIN_SPEED
     *      - хук активен (m_HookState >= PSEUDOFLY_MIN_HOOK_STATE)
     *      - m_HookedPlayer указывает на другого из пары
     *      - состояние держится >= PSEUDOFLY_MIN_DURATION_TICKS
     *   4. Записываем стартовую геометрию в SPseudoFlyAttempt.
     *   5. Отслеживаем пик/конец: если расстояние превысило 2x стартового
     *      или хук был отпущен — попытка завершена. Если перед этим
     *      тии «продвинулись» (delta_x^2 + delta_y^2 > порог) — success.
     */
    void DetectAttempts(std::vector<SPseudoFlyAttempt> &vOut);

    /**
     * Реальная реализация поиска попыток в переданных треках.
     * Принимает треки явно, чтобы можно было тестировать на синтетике.
     */
    void DetectAttemptsInTracks(const std::vector<STeeTrack> &vTracks,
        std::vector<SPseudoFlyAttempt> &vOut);

public:
    CDemoParser(IStorage *pStorage, CSnapshotDelta *pSnapshotDelta);

    /**
     * Разобрать демку по пути.
     * @param pFilename — путь относительно storage.
     * @param StorageType — тип storage (IStorage::TYPE_USER и т.д.)
     * @param pOut — куда положить результат.
     * @return true при успехе.
     */
    bool Parse(const char *pFilename, int StorageType, SParseResult *pOut);

    /**
     * Лёгкая версия: только метаданные заголовка (CDemoHeader).
     * Не открывает полноценный плейер.
     */
    static bool ReadHeaderOnly(IStorage *pStorage, const char *pFilename,
        int StorageType, SDemoMeta &Out, char *pError = nullptr, size_t ErrorSize = 0);

    /**
     * Вычислить SHA256 файла демки (для дедупликации).
     */
    static bool ComputeSha256(IStorage *pStorage, const char *pFilename,
        int StorageType, char aShaOut[65]);
};

} // namespace pfa

#endif // GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_DEMO_PARSER_H
