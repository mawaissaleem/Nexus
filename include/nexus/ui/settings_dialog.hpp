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

// What the dialog asks of the application. The dialog stays UI-only: the caller persists, re-grabs and re-indexes.
struct SettingsHooks {
    // Called with the shortcut text when the user applies one; returns false if it is invalid.
    std::function<bool(const std::string&)> apply_shortcut;
    // Called after the user adds or removes a search directory or exclusion; the caller re-indexes.
    std::function<void()> directories_changed;
    // (id, display name) of every registered provider, for the on/off list.
    std::vector<std::pair<std::string, std::string>> providers;
};

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    // `config`, `aliases` and `theme` must outlive the dialog. The dialog follows and changes `theme`.
    SettingsDialog(core::ConfigManager& config, core::AliasManager& aliases, ThemeController& theme,
                   SettingsHooks hooks, QWidget* parent = nullptr);

    // Re-reads the values that can change while the dialog is hidden (set from the launcher, config file).
    void refresh_from_config();

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
    ThemeController& theme_;
    SettingsHooks hooks_;
    Palette palette_;
    core::ConfigManager& config_;
    core::AliasManager& aliases_;
};

} // namespace nexus::ui

