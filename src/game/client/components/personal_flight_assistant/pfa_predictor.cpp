/**
 * @file pfa_predictor.cpp
 * @brief Реализация предиктора оптимальной точки начала псевдофлая.
 *
 * Алгоритм kNN:
 *   1. Получаем текущую геометрию: dist, vel1, vel2, rel_angle.
 *   2. Запрашиваем из БД TOP-k успешных попыток по грубой дистанции.
 *   3. Переранжируем их точной метрикой SituationDistance.
 *   4. Берём топ-K (по умолчанию PfaConst::MAX_CANDIDATES).
 *   5. Для каждого кандидата вычисляем «точку старта» — точку в мире,
 *      где игроку нужно оказаться, чтобы попытка удалась.
 *      Точка старта = историческая стартовая позиция Tee1 минус
 *      текущую позицию Tee1 + текущую позицию Tee1 = историческая
 *      стартовая позиция Tee1 в мировых координатах демки
 *      (но мы храним относительную геометрию, поэтому:
 *       BestPoint = MyPos + (HistStart_Tee2 - HistStart_Tee1)
 *       т.е. «куда поставить второй тий относительно моего»).
 *   6. Лучшая точка = кандидат с максимальным успехом и минимальной
 *      SituationDistance.
 *   7. Уверенность = EstimateSuccessProbability для лучшего кандидата
 *      × (k / k_total).
 *
 * Раздел ТЗ: 3. Расчёт точки начала псевдофлая.
 */

#include "pfa_predictor.h"

#include <base/log.h>
#include <base/math.h>
#include <base/mem.h>
#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>

namespace pfa {

CPredictor::CPredictor(CDatabase *pDb, CStatistics *pStats)
    : m_pDb(pDb)
    , m_pStats(pStats)
{
    mem_zero(&m_Last, sizeof(m_Last));
}

float CPredictor::SituationDistance(const SCurrentSituation &Cur,
    const SPseudoFlyAttempt &Hist) const
{
    // Взвешенная сумма нормированных разностей признаков.
    int Vel1MagCur = (int)std::sqrt((double)Cur.m_MyVelX * Cur.m_MyVelX +
                                     (double)Cur.m_MyVelY * Cur.m_MyVelY);
    int Vel1MagHist = (int)std::sqrt((double)Hist.m_StartTee1VelX * Hist.m_StartTee1VelX +
                                      (double)Hist.m_StartTee1VelY * Hist.m_StartTee1VelY);
    int Vel2MagCur = (int)std::sqrt((double)Cur.m_DummyVelX * Cur.m_DummyVelX +
                                     (double)Cur.m_DummyVelY * Cur.m_DummyVelY);
    int Vel2MagHist = (int)std::sqrt((double)Hist.m_StartTee2VelX * Hist.m_StartTee2VelX +
                                      (double)Hist.m_StartTee2VelY * Hist.m_StartTee2VelY);

    float DDist = std::abs((float)Cur.m_Distance - (float)Hist.m_StartDistance) / 100.0f;
    float DAng = std::abs((float)Cur.m_RelativeAngle - (float)Hist.m_StartRelativeAngle) / 1000.0f;
    float DVel1 = std::abs((float)Vel1MagCur - (float)Vel1MagHist) / 500.0f;
    float DVel2 = std::abs((float)Vel2MagCur - (float)Vel2MagHist) / 500.0f;

    return PfaConst::KNN_DISTANCE_WEIGHT * DDist
         + PfaConst::KNN_ANGLE_WEIGHT * DAng
         + PfaConst::KNN_VELOCITY_WEIGHT * DVel1
         + PfaConst::KNN_VELOCITY_WEIGHT * DVel2;
}

void CPredictor::PredictTrail(int32_t StartX, int32_t StartY,
    int32_t VelX, int32_t VelY, int32_t NumTicks,
    int32_t *pOutX, int32_t *pOutY, int &nOut) const
{
    // Простое интегрирование скорости: x(t) = x0 + v*t
    // В DDNet нет трения в воздухе (free fall), поэтому это разумное
    // приближение для краткосрочной траектории.
    nOut = 0;
    if(!pOutX || !pOutY) return;
    int MaxPoints = 64;
    int Step = NumTicks / MaxPoints;
    if(Step < 1) Step = 1;
    for(int t = 0; t <= NumTicks && nOut < MaxPoints; t += Step)
    {
        // Позиция: позиция + скорость * t / 50 (т.к. vel в единицах/тик, а не в единицах/сек)
        pOutX[nOut] = StartX + VelX * t / 50;
        pOutY[nOut] = StartY + VelY * t / 50;
        ++nOut;
    }
}

SRecommendation CPredictor::Recommend(const SCurrentSituation &Cur,
    int64_t ProfileId, int MaxCandidates)
{
    SRecommendation R{};
    if(!Cur.m_bValid || !m_pDb || !m_pStats)
    {
        R.m_bValid = false;
        return R;
    }

    if(MaxCandidates > PfaConst::MAX_CANDIDATES)
        MaxCandidates = PfaConst::MAX_CANDIDATES;

    // Получить kNN-кандидатов из БД (грубо)
    int k = g_Config.m_PfaKnnK * 4; // берем с запасом, переранжируем точно
    if(k < 5) k = 5;
    auto vCandidates = m_pDb->FindNearestSuccessful(
        Cur.m_Distance, Cur.m_RelativeAngle,
        Cur.m_MyVelX, Cur.m_MyVelY,
        Cur.m_DummyVelX, Cur.m_DummyVelY,
        (int)ProfileId, k);

    if(vCandidates.empty())
    {
        R.m_bValid = false;
        log_info("pfa", "Recommend: нет исторических кейсов для рекомендации");
        return R;
    }

    // Переранжировать точной метрикой
    std::sort(vCandidates.begin(), vCandidates.end(),
        [&](const SPseudoFlyAttempt &A, const SPseudoFlyAttempt &B) {
            return SituationDistance(Cur, A) < SituationDistance(Cur, B);
        });

    // Получить модель для расчёта вероятности успеха
    const SProfileModel *pModel = m_pStats->GetProfile(ProfileId);

    // Заполнить кандидатов
    R.m_NumCandidates = std::min((int)vCandidates.size(), MaxCandidates);
    for(int i = 0; i < R.m_NumCandidates; ++i)
    {
        const SPseudoFlyAttempt &Cand = vCandidates[i];
        // Точка старта — куда поставить дамми относительно меня
        R.m_aCandidateX[i] = Cur.m_MyX + (Cand.m_StartTee2X - Cand.m_StartTee1X);
        R.m_aCandidateY[i] = Cur.m_MyY + (Cand.m_StartTee2Y - Cand.m_StartTee1Y);
        float Confidence = 1.0f / (1.0f + SituationDistance(Cur, Cand));
        if(pModel)
        {
            int Vel1Mag = (int)std::sqrt((double)Cur.m_MyVelX * Cur.m_MyVelX +
                                          (double)Cur.m_MyVelY * Cur.m_MyVelY);
            int Vel2Mag = (int)std::sqrt((double)Cur.m_DummyVelX * Cur.m_DummyVelX +
                                          (double)Cur.m_DummyVelY * Cur.m_DummyVelY);
            Confidence *= m_pStats->EstimateSuccessProbability(*pModel,
                Cur.m_Distance, Cur.m_RelativeAngle, Vel1Mag, Vel2Mag);
        }
        R.m_aCandidateConfidence[i] = Confidence;
    }

    // Лучший кандидат — первый после сортировки
    if(R.m_NumCandidates > 0)
    {
        const SPseudoFlyAttempt &Best = vCandidates[0];
        R.m_bValid = true;
        R.m_BestX = R.m_aCandidateX[0];
        R.m_BestY = R.m_aCandidateY[0];
        R.m_BestTick = Best.m_StartTick;
        R.m_BestDistance = Best.m_StartDistance;
        R.m_BestRelativeAngle = Best.m_StartRelativeAngle;
        R.m_Confidence = R.m_aCandidateConfidence[0];
        R.m_MatchedAttemptId = Best.m_Id;
        // Предсказать траекторию от меня до лучшей точки
        PredictTrail(Cur.m_MyX, Cur.m_MyY,
            Cur.m_MyVelX, Cur.m_MyVelY, 50, // 1 секунда вперёд
            R.m_aTrailX, R.m_aTrailY, R.m_NumTrailPoints);
    }
    else
    {
        R.m_bValid = false;
    }

    m_Last = R;
    return R;
}

} // namespace pfa
