#pragma once

#include "nexus/core/result.hpp"
#include <QWidget>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace nexus::ui {

class ResultWidget : public QWidget {
    Q_OBJECT

public:
    explicit ResultWidget(const core::SearchResult& result, QWidget* parent = nullptr);

    void set_selected(bool selected);
    [[nodiscard]] const core::SearchResult& result() const noexcept { return result_; }

private:
    void setup_ui();

    core::SearchResult result_;
    QWidget* indicator_bar_{nullptr};
    QLabel* icon_label_{nullptr};
    QLabel* title_label_{nullptr};
    QLabel* subtitle_label_{nullptr};
    QLabel* badge_label_{nullptr};
};

} // namespace nexus::ui

