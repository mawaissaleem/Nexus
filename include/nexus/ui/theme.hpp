#pragma once

#include "nexus/utils/theme.hpp"
#include <QColor>
#include <QObject>
#include <QString>

namespace nexus::ui {

// The only place colours live. CSS-facing values are strings so the dark palette reproduces the
// original stylesheets byte for byte; painter-facing values are QColor.
struct Palette {
    struct Badge { QString bg, fg; };

    QString overlay_rgb;  // "r, g, b" of the translucent tint used for borders/hover (white on dark, black on light)
    [[nodiscard]] QString overlay(const char* alpha) const { return "rgba(" + overlay_rgb + ", " + alpha + ")"; }

    QString accent, on_accent;
    QString text_strong, text, title, text_soft, text_dim, subtitle, subtitle_dim, subtitle_selected;
    QString hint, alias_hint, config_info, brand;
    QString container_bg, row_selected_bg, row_selected_border, selection_bg, selection_text;
    QString window, panel, field, header, button, button_hover, danger, danger_border;
    Badge badge_empty, badge_url, badge_dir, badge_file, badge_cmd;
    QColor shadow, check_bg, check_border, check_text_hover, target_text;

    [[nodiscard]] static Palette dark();
    [[nodiscard]] static Palette light();
};

// Owns the active palette and tells windows when it changes. Not persisted here: the caller saves the mode.
class ThemeController : public QObject {
    Q_OBJECT

public:
    explicit ThemeController(utils::ThemeMode mode, QObject* parent = nullptr);

    void set_mode(utils::ThemeMode mode);
    [[nodiscard]] utils::ThemeMode mode() const noexcept { return mode_; }
    [[nodiscard]] const Palette& palette() const noexcept { return palette_; }

    // Qt 6.5+: the desktop colour scheme. Older Qt: the application palette is darker than its text.
    [[nodiscard]] static bool system_is_dark();

signals:
    void palette_changed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refresh();

    utils::ThemeMode mode_;
    bool dark_;
    Palette palette_;
};

} // namespace nexus::ui
