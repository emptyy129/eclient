/**
 * @file pfa_statistics.h
 * @brief Статистическая модель игрока на основе реальных данных.
 *
 * Не использует случайных или заранее заданных значений.
 * Все параметры модели вычисляются из попыток псевдофлая в БД.
 *
 * Раздел ТЗ: 2 (персональная статистическая модель),
 *            5 (улучшение модели при добавлении новых демок).
 */

#ifndef GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_STATISTICS_H
#define GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_STATISTICS_H

#include "pfa_types.h"
#include "pfa_database.h"

#include <base/vmath.h>

#include <cstdint>
#include <vector>

namespace pfa {

/**
 * @brief Параметры нормального распределения одного признака.
 * Используется для описания «идеальных» стартовых условий:
 * если большинство успешных попыток начинаются на расстоянии ~120 ± 15,
 * мы храним Mean=120, Stddev=15.
 */
struct SGaussParam {
    bool    m_bValid;     // false если недостаточно данных
    int32_t m_Count;      // число успешных семплов
    float   m_Mean;
    float   m_Stddev;     // выборочное стандартное отклонение
    int32_t m_MinObserved;
    int32_t m_MaxObserved;
};

/**
 * @brief Статистическая модель одного профиля.
 * Объединяет гауссианы для ключевых признаков стартовой геометрии.
 */
struct SProfileModel {
    int64_t m_ProfileId;
    char    m_aName[32];

    SGaussParam m_Distance;          // стартовое расстояние между тиями
    SGaussParam m_RelativeAngle;     // угол между Tee1->Tee2 и скоростью
    SGaussParam m_Tee1Speed;         // |Vel| первого тия
    SGaussParam m_Tee2Speed;         // |Vel| второго тия
    SGaussParam m_DurationTicks;     // длительность успешных попыток

    int32_t m_TotalAttempts;
    int32_t m_SuccessAttempts;
    float   m_SuccessRate;           // m_SuccessAttempts / m_TotalAttempts
    int64_t m_LastUpdatedMs;         // когда модель была пересчитана

    bool IsValid() const { return m_Distance.m_bValid; }
};

/**
 * @brief Класс-владелец всех моделей и операций над ними.
 */
class CStatistics {
    CDatabase *m_pDb;
    std::vector<SProfileModel> m_vModels;

    /**
     * Вычислить гауссиану для одного столбца попыток.
     * @param Attempts — только успешные попытки профиля.
     * @param Selector — лямбда извлечения значения из SPseudoFlyAttempt.
     */
    template<typename F>
    SGaussParam ComputeGauss(const std::vector<SPseudoFlyAttempt> &Attempts, F &&Selector);

    /**
     * Загрузить все попытки профиля.
     */
    std::vector<SPseudoFlyAttempt> LoadProfileAttempts(int64_t ProfileId, bool SuccessOnly);

public:
    explicit CStatistics(CDatabase *pDb);

    /**
     * Пересчитать модель одного профиля на основе всех попыток в БД.
     * Если AttemptCount изменился с прошлого раза — модель пересчитается.
     * @return true если модель обновлена или актуальна.
     */
    bool RebuildProfile(int64_t ProfileId);

    /**
     * Пересчитать ВСЕ профили (после добавления новой демки).
     */
    bool RebuildAll();

    /**
     * Получить модель профиля (0 = default).
     */
    const SProfileModel *GetProfile(int64_t ProfileId) const;

    /**
     * Получить все профили.
     */
    const std::vector<SProfileModel> &AllProfiles() const { return m_vModels; }

    /**
     * Оценить «похожесть» текущей ситуации на успешный кейс:
     * вычисляет вероятность того, что запуск псевдофлая из текущей
     * геометрии завершится успехом, по гауссианам признаков.
     * @return 0..1.
     */
    float EstimateSuccessProbability(const SProfileModel &Model,
        int32_t Distance, int32_t RelAngle,
        int32_t Vel1, int32_t Vel2) const;

    /**
     * Автоматически классифицировать попытку и подобрать профиль.
     * Простейшие правила:
     *   - speed > 2000 → "high_velocity"
     *   - distance < 30 → "tight"
     *   - duration > 200 ticks → "long"
     *   - иначе → "default"
     * Профили создаются автоматически при первом попадании.
     */
    int64_t ClassifyProfile(const SPseudoFlyAttempt &Attempt);
};

} // namespace pfa

#endif // GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_STATISTICS_H
