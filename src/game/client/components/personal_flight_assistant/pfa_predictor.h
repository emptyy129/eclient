/**
 * @file pfa_predictor.h
 * @brief Предиктор оптимальной точки начала псевдофлая (Aled).
 *
 * Сравнивает текущую игровую ситуацию с загруженными demo-кейсами
 * через kNN и вычисляет:
 *   - рекомендуемую точку старта
 *   - альтернативные точки
 *   - предсказанную траекторию движения
 *   - уверенность рекомендации
 *
 * Раздел ТЗ: 3. Расчёт точки начала псевдофлая.
 */

#ifndef GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_PREDICTOR_H
#define GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_PREDICTOR_H

#include "pfa_types.h"
#include "pfa_database.h"
#include "pfa_statistics.h"

#include <base/vmath.h>

namespace pfa {

class CPredictor {
    CDatabase *m_pDb;
    CStatistics *m_pStats;

    /**
     * Вычислить «расстояние» между текущей ситуацией и исторической попыткой.
     * Мера — взвешенная сумма:
     *   w_dist * |dist_cur - dist_hist| / dist_scale
     * + w_vel  * |vel_cur - vel_hist|  / vel_scale
     * + w_ang  * angle_diff(cur_rel, hist_rel) / ang_scale
     * Чем меньше — тем больше похожи ситуации.
     */
    float SituationDistance(const SCurrentSituation &Cur,
        const SPseudoFlyAttempt &Hist) const;

    /**
     * На основе текущей позиции и скорости предсказать траекторию движения
     * на m_BestTick тиков вперёд простым интегрированием скорости.
     * Это грубое приближение, но достаточно для отрисовки подсказки.
     */
    void PredictTrail(int32_t StartX, int32_t StartY,
        int32_t VelX, int32_t VelY, int32_t NumTicks,
        int32_t *pOutX, int32_t *pOutY, int &nOut) const;

public:
    CPredictor(CDatabase *pDb, CStatistics *pStats);

    /**
     * Главная функция: рассчитать рекомендацию для текущей ситуации.
     * @param Cur — снимок текущей геометрии тиев.
     * @param ProfileId — какой профиль использовать (0=default).
     * @param MaxCandidates — сколько альтернативных точек вернуть.
     * @return SRecommendation с заполненными полями.
     */
    SRecommendation Recommend(const SCurrentSituation &Cur,
        int64_t ProfileId, int MaxCandidates);

    /**
     * Кешированная последняя рекомендация (для визуализации).
     * Обновляется либо автоматически каждый кадр, либо по бинду.
     */
    const SRecommendation &LastRecommendation() const { return m_Last; }

private:
    SRecommendation m_Last;
};

} // namespace pfa

#endif // GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_PREDICTOR_H
