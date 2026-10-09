#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QTableWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include "nexus/core/alias_manager.hpp"
#include "nexus/core/config_manager.hpp"
#include "nexus/ui/theme.hpp"
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace nexus::ui {

class AliasEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit AliasEditDialog(const Palette& palette,
                            QWidget* parent = nullptr,
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
    Palette palette_;
};

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(core::ConfigManager& config, core::AliasManager& aliases, QWidget* parent = nullptr);

    static void show_settings(core::ConfigManager& config, core::AliasManager& aliases, QWidget* parent = nullptr);

    // Called with the shortcut text when the user applies one; returns false if it is invalid.
    // The caller owns persisting and re-grabbing it, so the dialog stays UI-only.
    static void set_shortcut_applier(std::function<bool(const std::string&)> applier);

    // The theme the dialog follows and changes (set once at startup; not owned).
    static void set_theme_controller(ThemeController* theme);

    // Called after the user adds or removes a search directory; the caller re-indexes.
    static void set_directories_changed_handler(std::function<void()> handler);

    // (id, display name) of every registered provider, for the on/off list. Set once at startup.
    static void set_providers(std::vector<std::pair<std::string, std::string>> providers);

signals:
    void settings_changed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

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

    // Exclusion slots (Search Directories tab)
    void on_add_exclusion();
    void on_remove_exclusion();

    // Navigation tab slots
    void on_ctrl_np_toggled(bool checked);
    void on_history_toggled(bool checked);
    void on_theme_changed(int index);
    void on_apply_shortcut();

private:
    void setup_ui();
    void apply_theme();
    void reload_aliases();
    void reload_directories();
    void reload_exclusions();

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

    QListWidget* exclusion_list_{nullptr};
    QLineEdit* exclusion_edit_{nullptr};

    // Navigation widgets
    QCheckBox* ctrl_np_checkbox_{nullptr};
    QCheckBox* history_checkbox_{nullptr};
    QComboBox* theme_combo_{nullptr};
    QLineEdit* shortcut_edit_{nullptr};
    Palette palette_;
    core::ConfigManager& config_;
    core::AliasManager& aliases_;
};

} // namespace nexus::ui

