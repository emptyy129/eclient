/**
 * @file pfa_types.h
 * @brief Общие типы данных Personal Flight Assistant.
 *
 * Все структуры спроектированы так, чтобы быть POD-совместимыми
 * и сериализуемыми в SQLite без сложных адаптеров.
 *
 * Раздел ТЗ: 2. Анализ демок — описывает форму извлекаемых данных.
 */

#ifndef GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_TYPES_H
#define GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_TYPES_H

#include <base/types.h>
#include <base/vmath.h>

#include "pfa_config.h"

#include <cstdint>
#include <vector>

namespace pfa {

#pragma pack(push, 1)

/**
 * @brief Телеметрия одного тия в конкретном тике.
 * Извлекается из CNetObj_CharacterCore в snapshot'ах демки.
 * Координаты в world units (как в m_X/m_Y протокола), скорость — в vel-единицах.
 */
struct STeeSample {
    int32_t m_Tick;          // игровой тик
    int32_t m_X, m_Y;        // позиция (world units)
    int32_t m_VelX, m_VelY;  // скорость
    int32_t m_Angle;         // направление прицела (0..2^32 = 2π)
    int8_t  m_Direction;     // -1, 0, 1
    int8_t  m_HookedPlayer;  // -1 или индекс игрока
    int8_t  m_HookState;     // -1..5 (см. protocol.h)
    int32_t m_HookTick;
    int32_t m_HookX, m_HookY, m_HookDx, m_HookDy;
};

/**
 * @brief Полный трек одного тия за всю демку (список семплов).
 * Хранится в SQLite в виде блоба (serialize через pfa::SerializeTrack).
 */
struct STeeTrack {
    int64_t m_DemoId;            // FK -> demos.id
    int8_t  m_PlayerSlot;        // 0..MAX_CLIENTS-1
    char    m_aPlayerName[16];   // для отладки/профилей
    int32_t m_FirstTick;
    int32_t m_LastTick;
    std::vector<STeeSample> m_vSamples;
};

/**
 * @brief Описание попытки псевдофлая (Aled).
 * Каждая успешная или неудачная попытка сохраняется как отдельная строка.
 *
 * Поля «старт» — значения в момент начала псевдофлая.
 * Поля «результат» — итог попытки.
 */
struct SPseudoFlyAttempt {
    int64_t m_Id;                 // PK, автоинкремент в SQLite
    int64_t m_DemoId;             // FK -> demos.id
    int32_t m_StartTick;          // тик начала псевдофлая
    int32_t m_EndTick;            // тик завершения/срыва
    int32_t m_ProfileId;          // 0=default, см. SProfile

    // Стартовая геометрия
    int32_t m_StartDistance;      // расстояние между тиями в стартовый тик
    int32_t m_StartTee1X, m_StartTee1Y;
    int32_t m_StartTee2X, m_StartTee2Y;
    int32_t m_StartTee1VelX, m_StartTee1VelY;
    int32_t m_StartTee2VelX, m_StartTee2VelY;
    int32_t m_StartRelativeAngle; // угол между вектором Tee1->Tee2 и скоростью Tee1
    int32_t m_StartHookState;     // состояние хука на старте
    int8_t  m_StartHookedPlayer;  // кто кого держит хуком

    // Результат
    int32_t m_DurationTicks;      // m_EndTick - m_StartTick
    int32_t m_PeakDistance;       // макс. расстояние во время псевдофлая
    int32_t m_EndDistance;        // расстояние в момент завершения
    bool    m_bSuccess;           // true = успешная попытка, false = срыв
    char    m_aFailureReason[32]; // описание причины срыва ("hook_release", "collision", ...)
};

/**
 * @brief Игровой профиль — отдельная статистическая модель под ситуацию.
 * Примеры: "default", "high_velocity", "tight_turn", "long_distance".
 * Раздел ТЗ: 5. Улучшение модели — отдельные профили.
 */
struct SProfile {
    int64_t m_Id;
    char    m_aName[32];
    char    m_aDescription[128];
    int32_t m_AttemptCount;       // кэшированное число попыток в профиле
    int32_t m_SuccessCount;       // кэшированное число успехов
    int64_t m_LastUpdatedTick;    // время последнего пересчёта статистики
};

/**
 * @brief Метаданные загруженной демки.
 */
struct SDemoMeta {
    int64_t m_Id;                  // PK в SQLite
    char    m_aFilename[256];      // путь относительно storage
    char    m_aMapName[64];        // карта из CDemoHeader.m_aMapName
    char    m_aNetversion[64];
    char    m_aTimestamp[20];
    int32_t m_LengthTicks;         // длительность в тиках
    int32_t m_NumTimelineMarkers;
    int32_t m_NumAttempts;         // сколько попыток псевдофлая извлечено
    int32_t m_NumSuccess;          // сколько успешных
    int64_t m_FileSize;
    char    m_aSha256[65];         // hex SHA256 файла демки
    int64_t m_ImportedAt;          // unix timestamp импорта в PFA
    bool    m_bParsed;             // полностью проанализирована?
};

/**
 * @brief Текущая игровая ситуация — снимок для сравнения с историей.
 */
struct SCurrentSituation {
    int32_t m_Tick;
    int32_t m_MyX, m_MyY;
    int32_t m_MyVelX, m_MyVelY;
    int32_t m_DummyX, m_DummyY;
    int32_t m_DummyVelX, m_DummyVelY;
    int32_t m_Distance;
    int32_t m_RelativeAngle;
    int8_t  m_HookState;
    int8_t  m_HookedPlayer;
    bool    m_bValid;              // false если нет второго тия или мы не в игре
};

/**
 * @brief Рекомендация точки начала псевдофлая.
 */
struct SRecommendation {
    bool    m_bValid;
    float   m_Confidence;          // 0..1
    int32_t m_BestX, m_BestY;      // рекомендуемая точка
    int32_t m_BestTick;            // через сколько тиков начинать
    int32_t m_BestDistance;        // желаемое стартовое расстояние
    int32_t m_BestRelativeAngle;   // желаемый угол
    // Альтернативные точки (до PfaConst::MAX_CANDIDATES)
    int32_t m_NumCandidates;
    int32_t m_aCandidateX[PfaConst::MAX_CANDIDATES];
    int32_t m_aCandidateY[PfaConst::MAX_CANDIDATES];
    float   m_aCandidateConfidence[PfaConst::MAX_CANDIDATES];
    // Предсказанная траектория до точки старта
    int32_t m_NumTrailPoints;
    int32_t m_aTrailX[64], m_aTrailY[64];
    int64_t m_MatchedAttemptId;    // ID кейса из БД, на котором основана рекомендация
};

#pragma pack(pop)

} // namespace pfa

#endif // GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_TYPES_H
