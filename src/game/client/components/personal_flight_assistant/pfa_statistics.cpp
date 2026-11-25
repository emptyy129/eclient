/**
 * @file pfa_statistics.cpp
 * @brief Реализация статистической модели PFA.
 *
 * Все параметры вычисляются из реальных попыток в БД. Никаких
 * хардкодов или случайных значений. Если данных недостаточно —
 * модель помечается как невалидная (m_bValid=false) и предиктор
 * не выдаёт рекомендаций (это явно обрабатывается в UI).
 *
 * Раздел ТЗ: 2. Анализ демок — статистическая модель,
 *            5. Улучшение модели при добавлении новых демок.
 */

#include "pfa_statistics.h"

#include <base/log.h>
#include <base/math.h>
#include <base/mem.h>
#include <base/str.h>

#include <algorithm>
#include <cmath>
#include <ctime>

namespace pfa {

CStatistics::CStatistics(CDatabase *pDb)
    : m_pDb(pDb)
{
}

template<typename F>
SGaussParam CStatistics::ComputeGauss(const std::vector<SPseudoFlyAttempt> &Attempts, F &&Selector)
{
    SGaussParam G{};
    G.m_bValid = false;
    G.m_Count = (int32_t)Attempts.size();
    if(G.m_Count < 3) // минимум 3 успешных кейса для статистики
        return G;

    double Sum = 0.0, SumSq = 0.0;
    G.m_MinObserved = INT_MAX;
    G.m_MaxObserved = INT_MIN;
    for(const auto &A : Attempts)
    {
        int32_t V = Selector(A);
        Sum += V;
        SumSq += (double)V * V;
        if(V < G.m_MinObserved) G.m_MinObserved = V;
        if(V > G.m_MaxObserved) G.m_MaxObserved = V;
    }
    G.m_Mean = (float)(Sum / G.m_Count);
    double Var = SumSq / G.m_Count - G.m_Mean * G.m_Mean;
    if(Var < 0.0) Var = 0.0;
    G.m_Stddev = (float)std::sqrt(Var);
    G.m_bValid = true;
    return G;
}

std::vector<SPseudoFlyAttempt> CStatistics::LoadProfileAttempts(int64_t ProfileId, bool SuccessOnly)
{
    std::vector<SPseudoFlyAttempt> v;
    if(!m_pDb) return v;
    // Используем ListAllAttempts (возвращает все попытки профиля + default),
    // затем фильтруем по успеху при необходимости.
    auto All = m_pDb->ListAllAttempts((int)ProfileId, /*bIncludeDefault=*/ProfileId != 0);
    if(SuccessOnly)
    {
        v.reserve(All.size());
        for(const auto &A : All)
            if(A.m_bSuccess) v.push_back(A);
    }
    else
    {
        v = std::move(All);
    }
    return v;
}

bool CStatistics::RebuildProfile(int64_t ProfileId)
{
    if(!m_pDb) return false;

    // Загрузить все успешные попытки профиля
    auto vSuccess = LoadProfileAttempts(ProfileId, true);

    SProfileModel Model{};
    Model.m_ProfileId = ProfileId;
    str_copy(Model.m_aName, ProfileId == 0 ? "default" : "profile");

    Model.m_Distance = ComputeGauss(vSuccess,
        [](const SPseudoFlyAttempt &A) { return A.m_StartDistance; });
    Model.m_RelativeAngle = ComputeGauss(vSuccess,
        [](const SPseudoFlyAttempt &A) { return A.m_StartRelativeAngle; });
    Model.m_Tee1Speed = ComputeGauss(vSuccess,
        [](const SPseudoFlyAttempt &A) {
            return (int32_t)std::sqrt((double)A.m_StartTee1VelX * A.m_StartTee1VelX +
                                      (double)A.m_StartTee1VelY * A.m_StartTee1VelY);
        });
    Model.m_Tee2Speed = ComputeGauss(vSuccess,
        [](const SPseudoFlyAttempt &A) {
            return (int32_t)std::sqrt((double)A.m_StartTee2VelX * A.m_StartTee2VelX +
                                      (double)A.m_StartTee2VelY * A.m_StartTee2VelY);
        });
    Model.m_DurationTicks = ComputeGauss(vSuccess,
        [](const SPseudoFlyAttempt &A) { return A.m_DurationTicks; });

    Model.m_SuccessAttempts = (int32_t)vSuccess.size();
    // TotalAttempts — все попытки профиля (без фильтра по успеху)
    auto vAll = LoadProfileAttempts(ProfileId, false);
    Model.m_TotalAttempts = (int32_t)vAll.size();
    Model.m_SuccessRate = Model.m_TotalAttempts > 0 ?
        (float)Model.m_SuccessAttempts / Model.m_TotalAttempts : 0.0f;
    Model.m_LastUpdatedMs = (int64_t)time(nullptr) * 1000;

    // Обновить кэш в векторе
    bool bFound = false;
    for(auto &M : m_vModels)
    {
        if(M.m_ProfileId == ProfileId)
        {
            M = Model;
            bFound = true;
            break;
        }
    }
    if(!bFound) m_vModels.push_back(Model);

    log_info("pfa", "Пересчитана модель профиля %lld: успехов=%d/%d (%.1f%%), "
        "Dist=%.1f±%.1f, Angle=%.1f±%.1f, Duration=%.1f±%.1f",
        (long long)ProfileId, Model.m_SuccessAttempts, Model.m_TotalAttempts,
        Model.m_SuccessRate * 100.0f,
        Model.m_Distance.m_Mean, Model.m_Distance.m_Stddev,
        Model.m_RelativeAngle.m_Mean, Model.m_RelativeAngle.m_Stddev,
        Model.m_DurationTicks.m_Mean, Model.m_DurationTicks.m_Stddev);
    return true;
}

bool CStatistics::RebuildAll()
{
    if(!m_pDb) return false;
    auto vProfiles = m_pDb->ListProfiles();
    bool bOk = true;
    for(const auto &P : vProfiles)
    {
        if(!RebuildProfile(P.m_Id))
            bOk = false;
    }
    return bOk;
}

const SProfileModel *CStatistics::GetProfile(int64_t ProfileId) const
{
    for(const auto &M : m_vModels)
        if(M.m_ProfileId == ProfileId) return &M;
    return nullptr;
}

float CStatistics::EstimateSuccessProbability(const SProfileModel &Model,
    int32_t Distance, int32_t RelAngle, int32_t Vel1, int32_t Vel2) const
{
    if(!Model.IsValid()) return 0.0f;

    // PDF гауссианы на каждом признаке, нормированная на [0..1]
    auto GaussScore = [](const SGaussParam &G, int32_t V) -> float {
        if(!G.m_bValid || G.m_Stddev < 1e-3f) return 0.5f;
        float Z = (V - G.m_Mean) / G.m_Stddev;
        // PDF гауссианы: exp(-z^2/2)
        return std::exp(-0.5f * Z * Z);
    };

    float SD = GaussScore(Model.m_Distance, Distance);
    float SA = GaussScore(Model.m_RelativeAngle, RelAngle);
    float SV1 = GaussScore(Model.m_Tee1Speed, Vel1);
    float SV2 = GaussScore(Model.m_Tee2Speed, Vel2);

    // Геометрическое среднее, чтобы один низкий признак сильнее понижал
    float Combined = std::sqrt(SD * SA) * std::sqrt(SV1 * SV2);
    // Умножить на исторический success rate профиля
    return Combined * Model.m_SuccessRate;
}

int64_t CStatistics::ClassifyProfile(const SPseudoFlyAttempt &Attempt)
{
    // Простая пороговая классификация.
    // Если ни одно условие не выполнено — default (0).
    int Vel1Mag = (int)std::sqrt((double)Attempt.m_StartTee1VelX * Attempt.m_StartTee1VelX +
                                  (double)Attempt.m_StartTee1VelY * Attempt.m_StartTee1VelY);
    if(Vel1Mag > 2000)
    {
        SProfile P{};
        str_copy(P.m_aName, "high_velocity");
        str_copy(P.m_aDescription, "Псевдофлай с высокой стартовой скоростью (>2000)");
        if(m_pDb) return m_pDb->AddOrUpdateProfile(P);
    }
    if(Attempt.m_StartDistance < 30)
    {
        SProfile P{};
        str_copy(P.m_aName, "tight");
        str_copy(P.m_aDescription, "Псевдофлай с малым стартовым расстоянием (<30)");
        if(m_pDb) return m_pDb->AddOrUpdateProfile(P);
    }
    if(Attempt.m_DurationTicks > 200)
    {
        SProfile P{};
        str_copy(P.m_aName, "long");
        str_copy(P.m_aDescription, "Длинные поддерживаемые псевдофлаи (>200 тиков)");
        if(m_pDb) return m_pDb->AddOrUpdateProfile(P);
    }
    return 0; // default
}

} // namespace pfa
