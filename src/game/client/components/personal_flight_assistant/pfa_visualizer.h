/**
 * @file pfa_visualizer.h
 * @brief Отрисовка подсказок PFA в игровом мире.
 *
 * Использует IGraphics и ITextRender из движка — никакого
 * прямого OpenGL, чтобы остаться совместимым с любым бэкендом
 * (OpenGL, Vulkan, GLES, null).
 *
 * Раздел ТЗ: 4. Визуализация.
 *             + Пользовательские настройки (прозрачность, шрифт, отключение).
 */

#ifndef GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_VISUALIZER_H
#define GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_VISUALIZER_H

#include "pfa_types.h"

#include <base/color.h>
#include <base/vmath.h>

class IGraphics;
class ITextRender;
class CUi;
struct CUIRect;

namespace pfa {

class CVisualizer {
    IGraphics *m_pGraphics;
    ITextRender *m_pTextRender;

    // Кэш текстовых контейнеров для числовых подписей.
    // В DDNet текст рендерится через CreateTextContainer + RenderTextContainer.
    // Мы создаём заново каждый кадр, т.к. текст динамический.
    bool m_bContainersDirty;

    /**
     * Нарисовать отрезок между двумя точками в мировых координатах.
     * Реализация: Graphics()->QuadsText() с поворотом; либо
     * Lines() — зависит от того, что поддерживает backend.
     * В DDNet принято использовать QuadsText для линий с толщиной.
     */
    void DrawLine(vec2 From, vec2 To, float Thickness, ColorRGBA Color) const;

    /**
     * Нарисовать точку (кружок) в мировых координатах.
     * Реализация: Quad с поворотом 0 и размером R*2 x R*2.
     */
    void DrawPoint(vec2 Pos, float Radius, ColorRGBA Color) const;

    /**
     * Нарисовать текстовую подпись в мировых координатах.
     * @param FontSize — высота шрифта в мировых единицах (умножается на
     * масштаб камеры).
     * @param Alpha — 0..1.
     */
    void DrawLabel(vec2 Pos, const char *pText, float FontSize, float Alpha) const;

public:
    CVisualizer();
    void Init(IGraphics *pGraphics, ITextRender *pTextRender);

    /**
     * Главная функция рендера подсказки.
     * Должна вызываться из OnRender() после отрисовки мира, но до HUD.
     *
     * @param Rec — что рисовать.
     * @param CameraCenter — центр камеры в мировых координатах (для перевода
     * экранных координат в мировые).
     * @param bEnabled — глобальный включатель (g_Config.m_PfaEnabled).
     */
    void Render(const SRecommendation &Rec, vec2 CameraCenter, bool bEnabled);
};

} // namespace pfa

#endif // GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_VISUALIZER_H
