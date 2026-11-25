/**
 * @file pfa_config.h
 * @brief Конфигурационные переменные Personal Flight Assistant (PFA).
 *
 * Все переменные хранятся в общем конфигурационном пространстве DDNet
 * (CConfig) и регистрируются через MACRO_CONFIG-механизм движка.
 * Это позволяет им переживать обновления и быть сохранёнными в
 * settings_ddnet.cfg автоматически.
 *
 * Раздел ТЗ: 1. Настройки.
 */

#ifndef GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_CONFIG_H
#define GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_CONFIG_H

/**
 * ВНИМАНИЕ: реальные MACRO_CONFIG-строки должны быть добавлены в
 * src/engine/shared/config_variables.h. Здесь приведена документация
 * всех переменных и их значений по умолчанию, чтобы держать реализацию
 * согласованной между модулями.
 *
 * Список переменных PFA:
 *
 *   m_PfaEnabled            (bool, 0)     — глобальный включатель PFA.
 *   m_PfaBindShowHints      (int, 0)      — код клавиши для бинда «показать подсказку».
 *   m_PfaAutoRefresh        (bool, 1)     — автообновление визуализации каждый кадр.
 *   m_PfaMinSamples         (int, 5)      — минимум успешных кейсов для выдачи рекомендации.
 *   m_PfaKnnK               (int, 5)      — k для kNN-поиска похожих ситуаций.
 *   m_PfaConfidenceThreshold(int, 40)    — порог уверенности (0..100), ниже которого подсказка не показывается.
 *   m_PfaMaxCandidates      (int, 5)      — сколько альтернативных точек рисовать.
 *   m_PfaVisualizeLines     (bool, 1)     — рисовать линии траектории.
 *   m_PfaVisualizePoints    (bool, 1)     — рисовать точки начала.
 *   m_PfaVisualizeLabels    (bool, 1)     — рисовать числовые подписи.
 *   m_PfaVisualizeAlpha     (int, 220)    — прозрачность визуализации (0..255).
 *   m_PfaLabelFontSize      (int, 11)     — размер шрифта подписей (8..32).
 *   m_PfaLineColorR/G/B     (int, 0..255) — цвет линий (по умолчанию 100/220/255 — голубой).
 *   m_PfaPointColorR/G/B    (int, 0..255) — цвет точек-кандидатов (по умолчанию 255/180/60).
 *   m_PfaBestColorR/G/B     (int, 0..255) — цвет рекомендуемой точки (по умолчанию 80/255/120).
 *   m_PfaUpdateChannel      (int, 0)      — 0=stable, 1=beta, 2=dev.
 *   m_PfaAutoUpdateCheck    (bool, 1)     — автопроверка обновлений при старте.
 *   m_PfaUpdateRepo         (string)      — GitHub-репозиторий для обновлений (owner/repo).
 *   m_PfaBackupKeep         (int, 3)      — сколько бэкапов клиента хранить.
 *   m_PfaProfileActive      (int, 0)      — активный профиль (0=default).
 *
 * Значения по умолчанию выбраны консервативно: PFA выключен до тех пор,
 * пока пользователь сам его не включит — это сохраняет совместимость
 * со стандартным DDNet-клиентом для тех, кто не использует функцию.
 */

// Числовые константы, не привязанные к конфигу
namespace PfaConst {
    // Размеры базы данных и буферов
    constexpr int MAX_DEMOS_IN_DB = 256;
    constexpr int MAX_ATTEMPTS_PER_DEMO = 512;
    constexpr int MAX_TICKS_TRAIL = 600; // 12 секунд при 50 Hz
    constexpr int MAX_CANDIDATES = 16;

    // Параметры определения псевдофлая (Aled)
    constexpr int PSEUDOFLY_MIN_DURATION_TICKS = 25; // 0.5 секунды
    constexpr int PSEUDOFLY_MAX_DISTANCE = 80;       // близость тиев в пикселях мира
    constexpr int PSEUDOFLY_MIN_SPEED = 800;         // минимальная суммарная скорость
    constexpr int PSEUDOFLY_MIN_HOOK_STATE = 1;      // хук должен быть активен

    // Параметры kNN-поиска
    constexpr float KNN_DISTANCE_WEIGHT = 1.0f;
    constexpr float KNN_VELOCITY_WEIGHT = 0.4f;
    constexpr float KNN_ANGLE_WEIGHT = 0.6f;

    // Визуализация
    constexpr float POINT_RADIUS = 8.0f;
    constexpr float BEST_POINT_RADIUS = 14.0f;
    constexpr float LINE_WIDTH = 2.5f;
} // namespace PfaConst

#endif // GAME_CLIENT_COMPONENTS_PERSONAL_FLIGHT_ASSISTANT_PFA_CONFIG_H
