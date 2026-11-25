/**
 * @file pfa_visualizer.cpp
 * @brief Реализация отрисовки подсказок PFA.
 *
 * Использует только публичный IGraphics / ITextRender — никакого
 * прямого OpenGL-кода. Все цвета и размеры берутся из g_Config,
 * что позволяет пользователю настраивать:
 *   - шрифт подписей (m_PfaLabelFontSize)
 *   - прозрачность (m_PfaVisualizeAlpha, 0..255)
 *   - полное отключение линий / точек / подписей отдельно
 *
 * Раздел ТЗ: 4. Визуализация (с настройками пользователя).
 */

#include "pfa_visualizer.h"

#include <base/color.h>
#include <base/math.h>
#include <base/vmath.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>
#include <game/client/ui.h>

#include <cmath>
#include <cstdio>

namespace pfa {

CVisualizer::CVisualizer()
    : m_pGraphics(nullptr)
    , m_pTextRender(nullptr)
    , m_bContainersDirty(true)
{
}

void CVisualizer::Init(IGraphics *pGraphics, ITextRender *pTextRender)
{
    m_pGraphics = pGraphics;
    m_pTextRender = pTextRender;
}

void CVisualizer::DrawLine(vec2 From, vec2 To, float Thickness, ColorRGBA Color) const
{
    if(!m_pGraphics) return;
    // DDNet не имеет прямого Lines(), поэтому используем Quad с поворотом.
    vec2 Dir = To - From;
    float Len = length(Dir);
    if(Len < 1e-3f) return;
    vec2 Mid = (From + To) * 0.5f;
    float Angle = std::atan2(Dir.y, Dir.x);

    IGraphics::CQuadItem Quad(Mid.x, Mid.y, Len, Thickness);
    m_pGraphics->TextureClear();
    m_pGraphics->QuadsBegin();
    m_pGraphics->SetColor(Color.r, Color.g, Color.b, Color.a);
    m_pGraphics->QuadsSetRotation(Angle);
    m_pGraphics->QuadsDrawTL(&Quad, 1);
    m_pGraphics->QuadsEnd();
}

void CVisualizer::DrawPoint(vec2 Pos, float Radius, ColorRGBA Color) const
{
    if(!m_pGraphics) return;
    IGraphics::CQuadItem Quad(Pos.x - Radius, Pos.y - Radius, Radius * 2.0f, Radius * 2.0f);
    m_pGraphics->TextureClear();
    m_pGraphics->QuadsBegin();
    m_pGraphics->SetColor(Color.r, Color.g, Color.b, Color.a);
    m_pGraphics->QuadsDrawTL(&Quad, 1);
    m_pGraphics->QuadsEnd();
}

void CVisualizer::DrawLabel(vec2 Pos, const char *pText, float FontSize, float Alpha) const
{
    if(!m_pTextRender || !pText || !pText[0]) return;
    // Создать временный текстовый контейнер и сразу его отрисовать.
    // В DDNet это делается через CTextCursor:
    CTextCursor Cursor;
    Cursor.m_FontSize = FontSize;
    Cursor.m_Align = TEXTALIGN_CENTER;
    Cursor.m_X = Pos.x;
    Cursor.m_Y = Pos.y;
    m_pTextRender->TextColor(1.0f, 1.0f, 1.0f, Alpha);
    m_pTextRender->TextEx(&Cursor, pText, -1);
}

void CVisualizer::Render(const SRecommendation &Rec, vec2 CameraCenter, bool bEnabled)
{
    if(!bEnabled || !Rec.m_bValid) return;
    if(!m_pGraphics || !m_pTextRender) return;

    // Глобальные настройки пользователя
    if(!g_Config.m_PfaVisualizeLines && !g_Config.m_PfaVisualizePoints && !g_Config.m_PfaVisualizeLabels)
        return; // всё отключено

    float Alpha = (float)g_Config.m_PfaVisualizeAlpha / 255.0f;
    if(Alpha <= 0.0f) return;

    // Цвета из конфига
    ColorRGBA LineColor(
        (float)g_Config.m_PfaLineColorR / 255.0f,
        (float)g_Config.m_PfaLineColorG / 255.0f,
        (float)g_Config.m_PfaLineColorB / 255.0f,
        Alpha);
    ColorRGBA PointColor(
        (float)g_Config.m_PfaPointColorR / 255.0f,
        (float)g_Config.m_PfaPointColorG / 255.0f,
        (float)g_Config.m_PfaPointColorB / 255.0f,
        Alpha);
    ColorRGBA BestColor(
        (float)g_Config.m_PfaBestColorR / 255.0f,
        (float)g_Config.m_PfaBestColorG / 255.0f,
        (float)g_Config.m_PfaBestColorB / 255.0f,
        Alpha);

    // Подготовка экранных координат: в DDNet мировые координаты
    // переводятся в экранные через m_pGraphics->GetScreenScreen.
    // Для отрисовки в мировых координатах просто используем
    // World-Quad (как делают все компоненты items.cpp, players.cpp).
    // CameraCenter используется для проверки, что точка видна.
    (void)CameraCenter;

    // 1. Линия предполагаемого движения (траектория)
    if(g_Config.m_PfaVisualizeLines && Rec.m_NumTrailPoints > 1)
    {
        for(int i = 1; i < Rec.m_NumTrailPoints; ++i)
        {
            vec2 A((float)Rec.m_aTrailX[i - 1], (float)Rec.m_aTrailY[i - 1]);
            vec2 B((float)Rec.m_aTrailX[i], (float)Rec.m_aTrailY[i]);
            DrawLine(A, B, PfaConst::LINE_WIDTH, LineColor);
        }
    }

    // 2. Альтернативные точки начала
    if(g_Config.m_PfaVisualizePoints)
    {
        for(int i = 0; i < Rec.m_NumCandidates; ++i)
        {
            vec2 P((float)Rec.m_aCandidateX[i], (float)Rec.m_aCandidateY[i]);
            DrawPoint(P, PfaConst::POINT_RADIUS, PointColor);
        }
    }

    // 3. Выделенная рекомендуемая точка
    if(g_Config.m_PfaVisualizePoints)
    {
        vec2 Best((float)Rec.m_BestX, (float)Rec.m_BestY);
        DrawPoint(Best, PfaConst::BEST_POINT_RADIUS, BestColor);
        // Внешнее кольцо — контраст
        DrawPoint(Best, PfaConst::BEST_POINT_RADIUS + 4.0f,
            ColorRGBA(BestColor.r, BestColor.g, BestColor.b, Alpha * 0.4f));
    }

    // 4. Числовые подписи
    if(g_Config.m_PfaVisualizeLabels)
    {
        float FontSize = (float)g_Config.m_PfaLabelFontSize;

        // Расстояние до рекомендуемой точки от центра камеры
        char aDistBuf[32];
        int Dist = (int)std::sqrt(
            (double)((Rec.m_BestX - (int)CameraCenter.x) * (Rec.m_BestX - (int)CameraCenter.x) +
                     (Rec.m_BestY - (int)CameraCenter.y) * (Rec.m_BestY - (int)CameraCenter.y)));
        str_format(aDistBuf, sizeof(aDistBuf), "%d", Dist);
        DrawLabel(vec2((float)Rec.m_BestX, (float)Rec.m_BestY - PfaConst::BEST_POINT_RADIUS - 8.0f),
            aDistBuf, FontSize, Alpha);

        // Уверенность рекомендации
        char aConfBuf[32];
        str_format(aConfBuf, sizeof(aConfBuf), "%.0f%%", Rec.m_Confidence * 100.0f);
        DrawLabel(vec2((float)Rec.m_BestX, (float)Rec.m_BestY + PfaConst::BEST_POINT_RADIUS + 8.0f),
            aConfBuf, FontSize * 0.9f, Alpha);

        // Подписи к альтернативным точкам
        for(int i = 0; i < Rec.m_NumCandidates; ++i)
        {
            if(Rec.m_aCandidateConfidence[i] < 0.05f) continue; // слишком слабые
            char aBuf[32];
            str_format(aBuf, sizeof(aBuf), "%.0f%%", Rec.m_aCandidateConfidence[i] * 100.0f);
            DrawLabel(vec2((float)Rec.m_aCandidateX[i], (float)Rec.m_aCandidateY[i] - PfaConst::POINT_RADIUS - 6.0f),
                aBuf, FontSize * 0.75f, Alpha * 0.7f);
        }
    }

    // Сбросить цвет текста после себя
    m_pTextRender->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

} // namespace pfa
