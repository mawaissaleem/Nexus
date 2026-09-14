#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QTableWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <memory>

namespace nexus::ui {

class AliasEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit AliasEditDialog(QWidget* parent = nullptr,
                            const QString& initial_name = "",
                            const QString& initial_target = "");

    QString name() const;
    QString target() const;

private slots:
    void on_browse_dir();
    void on_browse_file();
    void update_preview();

private:
    void setup_ui();

    QLineEdit* name_edit_{nullptr};
    QLineEdit* target_edit_{nullptr};
    QLabel* type_badge_{nullptr};
};

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    static void show_settings(QWidget* parent = nullptr);

signals:
    void settings_changed();

private slots:
    // Alias tab slots
    void on_add_alias();
    void on_edit_alias();
    void on_delete_alias();
    void on_filter_aliases(const QString& text);

    // Directory tab slots
    void on_add_directory();
    void on_remove_directory();
    void on_open_directory();

private:
    void setup_ui();
    void apply_theme();
    void reload_aliases();
    void reload_directories();

    // Tabs
    QTabWidget* tab_widget_{nullptr};

    // Aliases widgets
    QLineEdit* alias_search_{nullptr};
    QTableWidget* alias_table_{nullptr};
    QPushButton* add_alias_btn_{nullptr};
    QPushButton* edit_alias_btn_{nullptr};
    QPushButton* delete_alias_btn_{nullptr};

    // Directories widgets
    QListWidget* dir_list_{nullptr};
    QPushButton* add_dir_btn_{nullptr};
    QPushButton* remove_dir_btn_{nullptr};
    QPushButton* open_dir_btn_{nullptr};
};

} // namespace nexus::ui

