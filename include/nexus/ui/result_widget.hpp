#pragma once

#include "nexus/core/result.hpp"
#include "nexus/ui/theme.hpp"
#include <QWidget>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace nexus::ui {

class ResultWidget : public QWidget {
    Q_OBJECT

public:
    ResultWidget(const core::SearchResult& result, const Palette& palette, QWidget* parent = nullptr);

    void set_selected(bool selected);
    void set_palette(const Palette& palette);
    [[nodiscard]] const core::SearchResult& result() const noexcept { return result_; }

private:
    void setup_ui();

    core::SearchResult result_;
    Palette palette_;
    bool selected_{false};
    QWidget* indicator_bar_{nullptr};
    QLabel* icon_label_{nullptr};
    QLabel* title_label_{nullptr};
    QLabel* subtitle_label_{nullptr};
    QLabel* badge_label_{nullptr};
};

} // namespace nexus::ui

