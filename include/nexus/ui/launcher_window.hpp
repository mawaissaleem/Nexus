#pragma once

#include "nexus/core/config_manager.hpp"
#include "nexus/core/search_engine.hpp"
#include "nexus/ui/query_manager.hpp"
#include "nexus/ui/theme.hpp"
#include <QWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QGraphicsDropShadowEffect>
#include <QTimer>
#include <memory>

namespace nexus::ui {

class LauncherWindow : public QWidget {
    Q_OBJECT

public:
    explicit LauncherWindow(
        std::shared_ptr<core::SearchEngine> search_engine,
        ThemeController& theme,
        core::ConfigManager& config,
        QWidget* parent = nullptr
    );

    void toggle_launcher();
    void show_launcher();
    void hide_launcher();
    void open_settings();

signals:
    void settings_requested();  // the settings button / "settings" result; the application owns the dialog

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    void on_text_changed(const QString& text);
    void perform_search();
    void on_search_results(const std::vector<core::SearchResult>& results, const QString& query_text);
    void on_item_clicked(QListWidgetItem* item);
    void execute_selected();

private:
    void setup_ui();
    void apply_palette();
    void center_on_screen();
    void update_results(const std::vector<core::SearchResult>& results);
    void update_footer_hint();
    void run_result(const core::SearchResult& res);
    void select_next_result();
    void select_prev_result();

    std::shared_ptr<core::SearchEngine> search_engine_;
    ThemeController& theme_;
    core::ConfigManager& config_;
    QLineEdit* search_input_{nullptr};
    QListWidget* result_list_{nullptr};
    QWidget* container_{nullptr};
    QWidget* footer_{nullptr};
    QLabel* hint_label_{nullptr};
    QLabel* brand_label_{nullptr};
    QFrame* separator_{nullptr};
    QPushButton* settings_btn_{nullptr};
    QGraphicsDropShadowEffect* shadow_{nullptr};
    QTimer* debounce_timer_{nullptr};
    std::vector<core::SearchResult> current_results_;
    std::unique_ptr<QueryManager> query_manager_{nullptr};
};

} // namespace nexus::ui

