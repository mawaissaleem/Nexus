// Settings dialog: applying a shortcut shows the reason the application gave, and a success shows nothing.
// Offscreen Qt, temp config files, and a fake apply hook, so nothing is saved for real and no key is grabbed.
#include "nexus/core/alias_manager.hpp"
#include "nexus/core/config_manager.hpp"
#include "nexus/ui/settings_dialog.hpp"
#include "nexus/ui/theme.hpp"
#include "check.hpp"
#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <cassert>
#include <filesystem>
#include <unistd.h>

using namespace nexus;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const fs::path dir = fs::temp_directory_path() / ("nexus_test_settings_" + std::to_string(getpid()));
    fs::create_directories(dir);
    core::ConfigManager config((dir / "config.json").string());
    core::AliasManager aliases((dir / "aliases.json").string());
    ui::ThemeController theme(utils::ThemeMode::Dark);

    utils::Status outcome = utils::Status::error("can't write /x/config.json: Read-only file system");
    std::string applied;
    ui::SettingsHooks hooks;
    hooks.apply_shortcut = [&](const std::string& text) {
        applied = text;
        return outcome;
    };
    ui::SettingsDialog dialog(config, aliases, theme, std::move(hooks));
    int changed = 0;
    QObject::connect(&dialog, &ui::SettingsDialog::settings_changed, [&] { ++changed; });

    QPushButton* apply = nullptr;
    for (auto* b : dialog.findChildren<QPushButton*>())
        if (b->text() == "Apply") apply = b;
    assert(apply);

    // The warning is modal: read it and close it from the event loop it runs.
    QString shown;
    auto read_warning = [&] {
        QTimer::singleShot(0, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (box == nullptr) return;
            shown = box->text();
            box->done(QMessageBox::Ok);
        });
    };

    read_warning();
    apply->click();
    CHECK_EQ(applied, config.get_shortcut());
    CHECK_EQ(shown.toStdString(), outcome.message());
    CHECK_EQ(changed, 0);

    outcome = {};
    shown.clear();
    read_warning();
    apply->click();
    assert(shown.isEmpty());
    CHECK_EQ(changed, 1);

    fs::remove_all(dir);
    return 0;
}
