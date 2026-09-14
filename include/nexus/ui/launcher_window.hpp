#pragma once

#include "nexus/core/search_engine.hpp"
#include <QWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QTimer>
#include <memory>

namespace nexus::ui {

class LauncherWindow : public QWidget {
    Q_OBJECT

public:
    explicit LauncherWindow(
        std::shared_ptr<core::SearchEngine> search_engine,
        QWidget* parent = nullptr
    );

    void toggle_launcher();
    void show_launcher();
    void hide_launcher();
    void open_settings();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    void on_text_changed(const QString& text);
    void perform_search();
    void on_item_clicked(QListWidgetItem* item);
    void execute_selected();

private:
    void setup_ui();
    void center_on_screen();
    void update_results(const std::vector<core::SearchResult>& results);

    std::shared_ptr<core::SearchEngine> search_engine_;
    QLineEdit* search_input_{nullptr};
    QListWidget* result_list_{nullptr};
    QWidget* container_{nullptr};
    QWidget* footer_{nullptr};
    QTimer* debounce_timer_{nullptr};
    std::vector<core::SearchResult> current_results_;
};

} // namespace nexus::ui

