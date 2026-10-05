#include "nexus/ui/theme.hpp"
#include <QEvent>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

namespace nexus::ui {

Palette Palette::dark() {
    Palette p;
    p.overlay_rgb = "255, 255, 255";
    p.accent = "#007AFF";
    p.on_accent = "#FFFFFF";
    p.text_strong = "#FFFFFF";
    p.text = "#F0F0F5";
    p.title = "#D8D8DC";
    p.text_soft = "#A0A0B0";
    p.text_dim = "#9A9AA4";
    p.subtitle = "#9A9AA0";
    p.subtitle_dim = "#8C8C94";
    p.subtitle_selected = "#CBE2FF";
    p.hint = "#72727A";
    p.alias_hint = "#72727E";
    p.config_info = "#666675";
    p.brand = "#55555E";
    p.container_bg = "rgba(28, 28, 34, 0.96)";
    p.row_selected_bg = "rgba(0, 122, 255, 0.32)";
    p.row_selected_border = "rgba(0, 140, 255, 0.85)";
    p.selection_bg = "rgba(0, 122, 255, 0.35)";
    p.selection_text = "#FFFFFF";
    p.window = "#18181E";
    p.panel = "#202028";
    p.field = "#141418";
    p.header = "#1C1C24";
    p.button = "#2D2D38";
    p.button_hover = "#383846";
    p.danger = "#c92a2a";
    p.danger_border = "#ff6b6b";
    p.badge_empty = {"#383844", "#888894"};
    p.badge_url = {"#0d47a1", "#90caf9"};
    p.badge_dir = {"#1b5e20", "#a5d6a7"};
    p.badge_file = {"#4a148c", "#ce93d8"};
    p.badge_cmd = {"#e65100", "#ffcc80"};
    p.shadow = QColor(0, 0, 0, 160);
    p.check_bg = QColor(22, 22, 28);
    p.check_border = QColor(255, 255, 255, 75);
    p.check_text_hover = QColor(255, 255, 255);
    p.target_text = QColor(180, 200, 230);
    return p;
}

Palette Palette::light() {
    Palette p = dark();  // accent, danger and on_accent are shared
    p.overlay_rgb = "0, 0, 0";
    p.text_strong = "#101014";
    p.text = "#1C1C22";
    p.title = "#2A2A30";
    p.text_soft = "#5A5A68";
    p.text_dim = "#5C5C66";
    p.subtitle = "#6A6A72";
    p.subtitle_dim = "#6E6E76";
    p.subtitle_selected = "#1F4E8C";
    p.hint = "#6A6A72";
    p.alias_hint = "#6A6A74";
    p.config_info = "#7A7A88";
    p.brand = "#8A8A94";
    p.container_bg = "rgba(250, 250, 252, 0.96)";
    p.row_selected_bg = "rgba(0, 122, 255, 0.16)";
    p.row_selected_border = "rgba(0, 122, 255, 0.6)";
    p.selection_bg = "rgba(0, 122, 255, 0.25)";
    p.selection_text = "#101014";
    p.window = "#F4F4F7";
    p.panel = "#FFFFFF";
    p.field = "#FFFFFF";
    p.header = "#ECECF1";
    p.button = "#E4E4EA";
    p.button_hover = "#D8D8E0";
    p.badge_empty = {"#E4E4EA", "#6A6A72"};
    p.badge_url = {"#D6E6FA", "#0d47a1"};
    p.badge_dir = {"#D9F0DB", "#1b5e20"};
    p.badge_file = {"#EBDDF3", "#4a148c"};
    p.badge_cmd = {"#FDE6CC", "#e65100"};
    p.shadow = QColor(0, 0, 0, 70);
    p.check_bg = QColor(255, 255, 255);
    p.check_border = QColor(0, 0, 0, 75);
    p.check_text_hover = QColor(0, 0, 0);
    p.target_text = QColor(40, 80, 140);
    return p;
}

ThemeController::ThemeController(utils::ThemeMode mode, QObject* parent)
    : QObject(parent), mode_(mode), dark_(utils::resolve_dark(mode, system_is_dark())),
      palette_(dark_ ? Palette::dark() : Palette::light()) {
    // Follow desktop theme changes while running. Qt 6.5+ has a dedicated signal; otherwise the
    // application sends ApplicationPaletteChange to itself when the platform theme updates.
    qApp->installEventFilter(this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &ThemeController::refresh);
#endif
}

bool ThemeController::system_is_dark() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    switch (QGuiApplication::styleHints()->colorScheme()) {
        case Qt::ColorScheme::Dark: return true;
        case Qt::ColorScheme::Light: return false;
        case Qt::ColorScheme::Unknown: break;
    }
#endif
    const QPalette p = QGuiApplication::palette();
    return p.color(QPalette::WindowText).lightness() > p.color(QPalette::Window).lightness();
}

void ThemeController::set_mode(utils::ThemeMode mode) {
    mode_ = mode;
    refresh();
}

void ThemeController::refresh() {
    const bool dark = utils::resolve_dark(mode_, system_is_dark());
    if (dark == dark_) return;
    dark_ = dark;
    palette_ = dark_ ? Palette::dark() : Palette::light();
    emit palette_changed();
}

bool ThemeController::eventFilter(QObject* watched, QEvent* event) {
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange && mode_ == utils::ThemeMode::System) {
        refresh();
    }
    return QObject::eventFilter(watched, event);
}

} // namespace nexus::ui
