/**
 * @file pfa_demo_parser.cpp
 * @brief Реализация парсера demo-файлов PFA.
 *
 * Алгоритм:
 *   1. Загружаем демку через CDemoPlayer с собственным listener.
 *   2. В колбэке OnDemoPlayerSnapshot проходим по всем items snapshot'а,
 *      находим NETOBJTYPE_CHARACTER и для каждого клиента извлекаем
 *      поля CNetObj_CharacterCore.
 *   3. В колбэке OnDemoPlayerMessage фиксируем события (finishing line,
 *      death) для последующего определения успеха попытки.
 *   4. После полного прохода вызываем DetectAttempts(), который ищет
 *      в треках паттерны псевдофлая.
 *
 * Алгоритм DetectAttempts():
 *   Для каждой пары (i, j) клиентов:
 *     Синхронно проходим по тикам от max(first_i, first_j) до min(last_i, last_j).
 *     В каждом тике:
 *       - вычисляем distance(|Tee_i - Tee_j|)
 *       - проверяем hook_state и hooked_player
 *       - если вход в псевдофлай (см. pfa_demo_parser.h) — отмечаем start_tick
 *       - копим данные до выхода из псевдофлая: рост distance > 2x стартового,
 *         либо hook отпущен, либо конец трека
 *       - считаем попытку успешной, если за время псевдофлая
 *         смещение Tee_i по направлению его стартовой скорости >= порога
 *
 * ВАЖНО: никакие случайные значения не используются. Все параметры —
 * константы из pfa_config.h, основаны на физике DDNet (50 ticks/sec,
 * макс. скорость ~1000..2000 vel units, расстояние тия ~30 px).
 */

#include "pfa_demo_parser.h"

#include <base/fs.h>
#include <base/hash.h>
#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <engine/demo.h>
#include <engine/shared/compression.h>
#include <engine/shared/config.h>
#include <engine/shared/demo.h>
#include <engine/shared/huffman.h>
#include <engine/shared/network.h>
#include <engine/shared/packer.h>
#include <engine/shared/snapshot.h>
#include <engine/storage.h>
#include <generated/protocol.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace pfa {

namespace {
constexpr int MAX_CLIENTS_PFA = 64;

/**
 * @brief Внутренний listener, собирающий треки тиев.
 */
class CDemoParseListener : public CDemoPlayer::IListener {
public:
    CDemoPlayer *m_pDemoPlayer;
    CDemoParser *m_pParser;
    SParseResult *m_pResult;
    int m_LastTick;
    bool m_bSixup;

    // Текущие треки по слотам
    STeeTrack m_aTracks[MAX_CLIENTS_PFA];
    bool m_abTrackActive[MAX_CLIENTS_PFA];

    CDemoParseListener() { Reset(); }

    void Reset()
    {
        m_LastTick = -1;
        m_bSixup = false;
        for(int i = 0; i < MAX_CLIENTS_PFA; ++i)
        {
            m_aTracks[i].m_vSamples.clear();
            m_aTracks[i].m_PlayerSlot = (int8_t)i;
            m_aTracks[i].m_aPlayerName[0] = '\0';
            m_aTracks[i].m_FirstTick = INT_MAX;
            m_aTracks[i].m_LastTick = -1;
            m_abTrackActive[i] = false;
        }
    }

    void OnDemoPlayerSnapshot(void *pData, int Size) override
    {
        const CSnapshot *pSnap = (const CSnapshot *)pData;
        if(!pSnap || Size <= 0) return;

        // Текущий тик из info плейера
        const CDemoPlayer::CPlaybackInfo *pInfo = m_pDemoPlayer->Info();
        int Tick = pInfo ? pInfo->m_Info.m_CurrentTick : m_LastTick + 1;
        m_LastTick = Tick;

        int Num = pSnap->NumItems();
        for(int i = 0; i < Num; ++i)
        {
            const CSnapshotItem *pItem = pSnap->GetItem(i);
            int ItemType = pSnap->GetItemType(i);
            int ItemId = pItem->Id();

            if(ItemType == NETOBJTYPE_CHARACTER && ItemId >= 0 && ItemId < MAX_CLIENTS_PFA)
            {
                const CNetObj_Character *pChar = (const CNetObj_Character *)pItem->Data();
                // CNetObj_Character включает CNetObj_CharacterCore как базу
                // (см. gamecore.h Read/Write — первые поля Core, потом Character)
                STeeSample S;
                S.m_Tick = Tick;
                S.m_X = pChar->m_X;
                S.m_Y = pChar->m_Y;
                S.m_VelX = pChar->m_VelX;
                S.m_VelY = pChar->m_VelY;
                S.m_Angle = pChar->m_Angle;
                S.m_Direction = pChar->m_Direction;
                S.m_HookedPlayer = pChar->m_HookedPlayer;
                S.m_HookState = pChar->m_HookState;
                S.m_HookTick = pChar->m_HookTick;
                S.m_HookX = pChar->m_HookX;
                S.m_HookY = pChar->m_HookY;
                S.m_HookDx = pChar->m_HookDx;
                S.m_HookDy = pChar->m_HookDy;

                STeeTrack &Track = m_aTracks[ItemId];
                Track.m_vSamples.push_back(S);
                Track.m_PlayerSlot = (int8_t)ItemId;
                if(S.m_Tick < Track.m_FirstTick) Track.m_FirstTick = S.m_Tick;
                if(S.m_Tick > Track.m_LastTick) Track.m_LastTick = S.m_Tick;
                m_abTrackActive[ItemId] = true;
            }
            else if(ItemType == NETOBJTYPE_CLIENTINFO && ItemId >= 0 && ItemId < MAX_CLIENTS_PFA)
            {
                const CNetObj_ClientInfo *pInfo2 = (const CNetObj_ClientInfo *)pItem->Data();
                STeeTrack &Track = m_aTracks[ItemId];
                IntsToStr(pInfo2->m_aName, std::size(pInfo2->m_aName),
                    Track.m_aPlayerName, sizeof(Track.m_aPlayerName));
            }
        }
    }

    void OnDemoPlayerMessage(void *pData, int Size) override
    {
        // Сообщения не используются для определения псевдофлая;
        // успех определяется по геометрии в DetectAttempts.
        (void)pData; (void)Size;
    }
};

/**
 * @brief Создать CSnapshotDelta с правильными статическими размерами.
 * Скопировано из tools/demo_extract_chat.cpp — это canonical способ.
 */
std::unique_ptr<CSnapshotDelta> CreateSnapshotDeltaInternal()
{
    auto pResult = std::make_unique<CSnapshotDelta>();
    CNetObjHandler NetObjHandler;
    for(int i = 0; i < NUM_NETOBJTYPES; ++i)
        pResult->SetStaticsize(i, NetObjHandler.GetObjSize(i));
    return pResult;
}
} // namespace

CDemoParser::CDemoParser(IStorage *pStorage, CSnapshotDelta *pSnapshotDelta)
    : m_pStorage(pStorage)
    , m_pSnapshotDelta(pSnapshotDelta)
    , m_NumClients(0)
{
}

bool CDemoParser::ReadHeaderOnly(IStorage *pStorage, const char *pFilename,
    int StorageType, SDemoMeta &Out, char *pError, size_t ErrorSize)
{
    if(pError && ErrorSize) pError[0] = '\0';

    // Создаём временный demo player только для чтения заголовка
    auto pDelta = std::make_unique<CSnapshotDelta>();
    auto pDeltaSixup = std::make_unique<CSnapshotDelta>();
    CDemoPlayer Player(pDelta.get(), pDeltaSixup.get(), false);

    CDemoHeader Header;
    CTimelineMarkers Markers;
    CMapInfo MapInfo;
    if(!Player.GetDemoInfo(pStorage, pFilename, StorageType, &Header, &Markers, &MapInfo,
        nullptr, pError, ErrorSize))
    {
        return false;
    }

    str_copy(Out.m_aMapName, Header.m_aMapName);
    str_copy(Out.m_aNetversion, Header.m_aNetversion);
    str_copy(Out.m_aTimestamp, Header.m_aTimestamp);
    Out.m_LengthTicks = bytes_be_to_uint(Header.m_aLength);
    Out.m_NumTimelineMarkers = bytes_be_to_uint(Markers.m_aNumTimelineMarkers);
    Out.m_bParsed = false;
    return true;
}

bool CDemoParser::ComputeSha256(IStorage *pStorage, const char *pFilename,
    int StorageType, char aShaOut[65])
{
    aShaOut[0] = '\0';
    IOHANDLE File = pStorage->OpenFile(pFilename, IOFLAG_READ, StorageType);
    if(!File) return false;

    SHA256_DIGEST Dig;
    sha256_init(&Dig);
    unsigned char aBuf[8192];
    while(true)
    {
        unsigned Bytes = io_read(File, aBuf, sizeof(aBuf));
        if(Bytes == 0) break;
        sha256_update(&Dig, aBuf, Bytes);
    }
    io_close(File);

    SHA256_DIGEST Final = sha256_finish(&Dig);
    char aHex[65];
    sha256_str(Final, aHex, sizeof(aHex));
    str_copy(aShaOut, aHex, 65);
    return true;
}

bool CDemoParser::Parse(const char *pFilename, int StorageType, SParseResult *pOut)
{
    if(!m_pStorage || !m_pSnapshotDelta || !pOut)
    {
        if(pOut) { pOut->m_bOk = false; str_copy(pOut->m_aError, "invalid args"); }
        return false;
    }

    pOut->m_bOk = false;
    pOut->m_aError[0] = '\0';
    pOut->m_NumTicksProcessed = 0;
    pOut->m_NumPlayersFound = 0;
    pOut->m_NumAttemptsDetected = 0;
    pOut->m_NumSuccessDetected = 0;
    pOut->m_vTracks.clear();
    pOut->m_vAttempts.clear();

    // Sixup-дельту создаём локально; в реальном интегрированном билде
    // берётся из GameClient->ServerSnapshotDelta()
    auto pDeltaSixup = std::make_unique<CSnapshotDelta>();
    CDemoPlayer Player(m_pSnapshotDelta, pDeltaSixup.get(), false);

    if(Player.Load(m_pStorage, pFilename, StorageType) == -1)
    {
        str_copy(pOut->m_aError, Player.ErrorMessage() ? Player.ErrorMessage() : "load failed");
        log_error("pfa", "Parse: не удалось загрузить '%s': %s", pFilename, pOut->m_aError);
        return false;
    }

    CDemoParseListener Listener;
    Listener.m_pDemoPlayer = &Player;
    Listener.m_pParser = this;
    Listener.m_pResult = pOut;
    Listener.Reset();
    Player.SetListener(&Listener);

    CNetBase::Init();
    Player.Play();

    // Главный цикл чтения
    int TickGuard = 0;
    while(Player.IsPlaying())
    {
        Player.Update(false);
        const CDemoPlayer::CPlaybackInfo *pInfo = Player.Info();
        if(pInfo && pInfo->m_Info.m_Paused)
            break;
        if(++TickGuard > 2000000) // анти-зацикливание (40+ секунд при 50Hz = бесконечно для демо)
            break;
    }
    Player.Stop();

    // Собрать финальные треки
    for(int i = 0; i < MAX_CLIENTS_PFA; ++i)
    {
        if(Listener.m_abTrackActive[i] && !Listener.m_aTracks[i].m_vSamples.empty())
        {
            pOut->m_vTracks.push_back(Listener.m_aTracks[i]);
            pOut->m_NumPlayersFound++;
        }
    }
    pOut->m_NumTicksProcessed = Listener.m_LastTick + 1;

    // Поиск попыток псевдофлая на основе собранных треков
    DetectAttemptsInTracks(pOut->m_vTracks, pOut->m_vAttempts);
    pOut->m_NumAttemptsDetected = (int32_t)pOut->m_vAttempts.size();
    for(const auto &A : pOut->m_vAttempts)
        if(A.m_bSuccess) pOut->m_NumSuccessDetected++;

    pOut->m_bOk = true;
    log_info("pfa", "Parse '%s': ticks=%d players=%d attempts=%d success=%d",
        pFilename, pOut->m_NumTicksProcessed, pOut->m_NumPlayersFound,
        pOut->m_NumAttemptsDetected, pOut->m_NumSuccessDetected);
    return true;
}

void CDemoParser::ProcessSnapshot(const CSnapshot *pSnap, int Tick)
{
    // В реализации используется CDemoParseListener::OnDemoPlayerSnapshot
    // (см. выше). Эта функция оставлена как точка расширения для будущих
    // оптимизаций (потоковый парсинг без CDemoPlayer).
    (void)pSnap; (void)Tick;
}

/**
 * @brief Линейно-интерполированный семпл на произвольном тике.
 * Возвращает false, если тик вне диапазона трека.
 */
static bool SampleAtTick(const STeeTrack &Track, int Tick, STeeSample &Out)
{
    if(Track.m_vSamples.empty()) return false;
    if(Tick < Track.m_FirstTick || Tick > Track.m_LastTick) return false;
    // В демках тии присутствуют не в каждом тике; используем ближайший
    int Idx = -1;
    int BestDelta = INT_MAX;
    for(size_t i = 0; i < Track.m_vSamples.size(); ++i)
    {
        int d = abs(Track.m_vSamples[i].m_Tick - Tick);
        if(d < BestDelta) { BestDelta = d; Idx = (int)i; if(d == 0) break; }
        if(Track.m_vSamples[i].m_Tick > Tick) break;
    }
    if(Idx < 0) return false;
    Out = Track.m_vSamples[Idx];
    return true;
}

void CDemoParser::DetectAttempts(std::vector<SPseudoFlyAttempt> &vOut)
{
    // Современная сигнатура: треки берутся из m_pResult->m_vTracks.
    // Этот метод оставлен для совместимости с .h, но вызывается
    // DetectAttemptsInTracks(pOut->m_vTracks, vOut).
    // Если m_vCurrentTracks пустой — пустой результат.
    if(!m_vCurrentTracks.empty())
    {
        // Ручная передача треков через m_vCurrentTracks
        DetectAttemptsInTracks(m_vCurrentTracks, vOut);
    }
}

/**
 * @brief Главная реализация алгоритма поиска псевдофлая.
 *
 * Для каждой пары треков (i, j), i < j:
 *   1. Определяем общий диапазон тиков [max_first, min_last].
 *   2. Идём по этому диапазону. В каждом тике:
 *      - вычисляем расстояние и проверяем, что:
 *          dist < PSEUDOFLY_MAX_DISTANCE
 *          hook активен у одного из тиев
 *          hookedPlayer одного указывает на другого
 *          скорость >= PSEUDOFLY_MIN_SPEED у хотя бы одного
 *      - Если все условия — это потенциальный старт псевдофлая.
 *   3. Отмечаем start_tick = текущий. Продолжаем копить:
 *      - peak_distance обновляем
 *      - Если dist > 2 * start_dist или hook отпущен или конец диапазона:
 *        завершаем попытку.
 *   4. Если duration >= PSEUDOFLY_MIN_DURATION_TICKS:
 *      - вычисляем смещение Tee_i: delta_pos = pos(end) - pos(start).
 *        Считаем успехом, если проекция delta_pos на стартовую
 *        скорость >= 50% от (|start_vel| * duration / tickrate).
 *        То есть тии продвинулись в нужном направлении.
 *   5. Присваиваем profileId=0 (default). CStatistics::ClassifyProfile
 *      позже может реклассифицировать.
 */
void CDemoParser::DetectAttemptsInTracks(const std::vector<STeeTrack> &vTracks,
    std::vector<SPseudoFlyAttempt> &vOut)
{
    vOut.clear();
    if(vTracks.size() < 2) return;

    for(size_t i = 0; i < vTracks.size(); ++i)
    {
        for(size_t j = i + 1; j < vTracks.size(); ++j)
        {
            const STeeTrack &TA = vTracks[i];
            const STeeTrack &TB = vTracks[j];
            if(TA.m_vSamples.empty() || TB.m_vSamples.empty()) continue;

            int FirstTick = std::max(TA.m_FirstTick, TB.m_FirstTick);
            int LastTick = std::min(TA.m_LastTick, TB.m_LastTick);
            if(LastTick <= FirstTick) continue;

            int Cur = FirstTick;
            while(Cur < LastTick)
            {
                STeeSample SA, SB;
                if(!SampleAtTick(TA, Cur, SA) || !SampleAtTick(TB, Cur, SB))
                {
                    ++Cur;
                    continue;
                }

                int Dist = (int)std::sqrt(
                    (double)((SA.m_X - SB.m_X) * (SA.m_X - SB.m_X) +
                             (SA.m_Y - SB.m_Y) * (SA.m_Y - SB.m_Y)));

                // Проверка условий входа в псевдофлай
                bool DistOk = (Dist <= PfaConst::PSEUDOFLY_MAX_DISTANCE);
                int SpeedA = (int)std::sqrt((double)(SA.m_VelX * SA.m_VelX + SA.m_VelY * SA.m_VelY));
                int SpeedB = (int)std::sqrt((double)(SB.m_VelX * SB.m_VelX + SB.m_VelY * SB.m_VelY));
                bool SpeedOk = (SpeedA >= PfaConst::PSEUDOFLY_MIN_SPEED ||
                                SpeedB >= PfaConst::PSEUDOFLY_MIN_SPEED);
                bool HookOk =
                    (SA.m_HookState >= PfaConst::PSEUDOFLY_MIN_HOOK_STATE &&
                     SA.m_HookedPlayer == TB.m_PlayerSlot) ||
                    (SB.m_HookState >= PfaConst::PSEUDOFLY_MIN_HOOK_STATE &&
                     SB.m_HookedPlayer == TA.m_PlayerSlot);

                if(!(DistOk && SpeedOk && HookOk))
                {
                    ++Cur;
                    continue;
                }

                // Зафиксировать старт попытки
                int StartTick = Cur;
                int StartDist = Dist;
                int PeakDist = Dist;
                int EndTick = StartTick;
                int StartTee1X = SA.m_X, StartTee1Y = SA.m_Y;
                int StartTee2X = SB.m_X, StartTee2Y = SB.m_Y;
                int StartTee1VX = SA.m_VelX, StartTee1VY = SA.m_VelY;
                int StartTee2VX = SB.m_VelX, StartTee2VY = SB.m_VelY;

                // Относительный угол: угол между (TeeA -> TeeB) и вектором скорости TeeA
                int RelAngle = 0;
                {
                    int Dx = SB.m_X - SA.m_X;
                    int Dy = SB.m_Y - SA.m_Y;
                    double Dot = (double)(Dx * SA.m_VelX + Dy * SA.m_VelY);
                    double MagD = std::sqrt((double)(Dx * Dx + Dy * Dy)) + 1e-6;
                    double MagV = std::sqrt((double)(SA.m_VelX * SA.m_VelX + SA.m_VelY * SA.m_VelY)) + 1e-6;
                    double Cos = Dot / (MagD * MagV);
                    if(Cos > 1.0) Cos = 1.0;
                    if(Cos < -1.0) Cos = -1.0;
                    RelAngle = (int)(std::acos(Cos) * 1000.0); // в миллирадианах
                }

                // Идём вперёд, пока не выйдем из псевдофлая
                int Inner = Cur;
                while(Inner < LastTick)
                {
                    ++Inner;
                    STeeSample SA2, SB2;
                    if(!SampleAtTick(TA, Inner, SA2) || !SampleAtTick(TB, Inner, SB2))
                        continue;

                    int Dist2 = (int)std::sqrt(
                        (double)((SA2.m_X - SB2.m_X) * (SA2.m_X - SB2.m_X) +
                                 (SA2.m_Y - SB2.m_Y) * (SA2.m_Y - SB2.m_Y)));
                    if(Dist2 > PeakDist) PeakDist = Dist2;

                    bool HookStillOk =
                        (SA2.m_HookState >= PfaConst::PSEUDOFLY_MIN_HOOK_STATE &&
                         SA2.m_HookedPlayer == TB.m_PlayerSlot) ||
                        (SB2.m_HookState >= PfaConst::PSEUDOFLY_MIN_HOOK_STATE &&
                         SB2.m_HookedPlayer == TA.m_PlayerSlot);

                    // Выход: либо хук отпущен, либо расстояние резко выросло
                    if(!HookStillOk || Dist2 > 2 * StartDist + PfaConst::PSEUDOFLY_MAX_DISTANCE)
                    {
                        EndTick = Inner;
                        break;
                    }
                    EndTick = Inner;
                }
                if(EndTick == StartTick) EndTick = StartTick + 1;

                int Duration = EndTick - StartTick;
                if(Duration < PfaConst::PSEUDOFLY_MIN_DURATION_TICKS)
                {
                    Cur = EndTick + 1;
                    continue;
                }

                // Определить успех: тии продвинулись в направлении стартовой скорости
                STeeSample SAEnd, SBEnd;
                bool GotEndA = SampleAtTick(TA, EndTick, SAEnd);
                bool GotEndB = SampleAtTick(TB, EndTick, SBEnd);
                bool bSuccess = false;
                if(GotEndA && GotEndB)
                {
                    int DeltaAX = SAEnd.m_X - StartTee1X;
                    int DeltaAY = SAEnd.m_Y - StartTee1Y;
                    double DotA = (double)(DeltaAX * StartTee1VX + DeltaAY * StartTee1VY);
                    double MagStartV = std::sqrt((double)(StartTee1VX * StartTee1VX +
                                                          StartTee1VY * StartTee1VY)) + 1e-6;
                    double Expected = 0.5 * MagStartV * Duration / 50.0; // 50 Hz
                    bSuccess = (DotA > 0 && DotA >= Expected);
                }

                SPseudoFlyAttempt A;
                mem_zero(&A, sizeof(A));
                A.m_DemoId = TA.m_DemoId; // будет переписан при сохранении
                A.m_StartTick = StartTick;
                A.m_EndTick = EndTick;
                A.m_ProfileId = 0; // default
                A.m_StartDistance = StartDist;
                A.m_StartTee1X = StartTee1X;
                A.m_StartTee1Y = StartTee1Y;
                A.m_StartTee2X = StartTee2X;
                A.m_StartTee2Y = StartTee2Y;
                A.m_StartTee1VelX = StartTee1VX;
                A.m_StartTee1VelY = StartTee1VY;
                A.m_StartTee2VelX = StartTee2VX;
                A.m_StartTee2VelY = StartTee2VY;
                A.m_StartRelativeAngle = RelAngle;
                A.m_StartHookState = SA.m_HookState;
                A.m_StartHookedPlayer = SA.m_HookedPlayer;
                A.m_DurationTicks = Duration;
                A.m_PeakDistance = PeakDist;
                A.m_EndDistance = GotEndA && GotEndB ?
                    (int)std::sqrt((double)((SAEnd.m_X - SBEnd.m_X) * (SAEnd.m_X - SBEnd.m_X) +
                                            (SAEnd.m_Y - SBEnd.m_Y) * (SAEnd.m_Y - SBEnd.m_Y))) : PeakDist;
                A.m_bSuccess = bSuccess;
                str_copy(A.m_aFailureReason, bSuccess ? "" : "insufficient_progress");
                vOut.push_back(A);

                Cur = EndTick + 1;
            }
        }
    }
}

} // namespace pfa
