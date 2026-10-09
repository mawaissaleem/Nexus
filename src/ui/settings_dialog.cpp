#include "nexus/ui/settings_dialog.hpp"
#include "nexus/core/alias_manager.hpp"
#include "nexus/core/config_manager.hpp"
#include "nexus/utils/logger.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QIcon>
#include <QDialogButtonBox>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QEvent>
#include <QPalette>
#include <QComboBox>
#include <algorithm>

namespace nexus::ui {

// ============================================================================
// AliasEditDialog
// ============================================================================

AliasEditDialog::AliasEditDialog(const Palette& palette,
                                 QWidget* parent,
                                 const QString& initial_name,
                                 const QString& initial_target)
    : QDialog(parent), palette_(palette) {
    if (parent) {
        setPalette(parent->palette());
    }
    setWindowTitle(initial_name.isEmpty() ? "Add New Alias" : "Edit Alias");
    setMinimumWidth(480);
    setup_ui();

    name_edit_->setText(initial_name);
    target_edit_->setText(initial_target);
    update_preview();
}

void AliasEditDialog::setup_ui() {
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setSpacing(14);
    main_layout->setContentsMargins(18, 18, 18, 18);

    auto* form_layout = new QFormLayout();
    form_layout->setSpacing(10);

    name_edit_ = new QLineEdit(this);
    name_edit_->setPlaceholderText("e.g. gh, docs, figma, work");
    form_layout->addRow("Nickname:", name_edit_);

    auto* target_container = new QWidget(this);
    auto* target_layout = new QVBoxLayout(target_container);
    target_layout->setContentsMargins(0, 0, 0, 0);
    target_layout->setSpacing(6);

    target_edit_ = new QLineEdit(target_container);
    target_edit_->setPlaceholderText("e.g. https://github.com or ~/Projects or code");
    target_layout->addWidget(target_edit_);

    auto* browse_layout = new QHBoxLayout();
    browse_layout->setContentsMargins(0, 0, 0, 0);
    browse_layout->setSpacing(8);

    auto* browse_dir_btn = new QPushButton("Browse Folder...", target_container);
    browse_dir_btn->setCursor(Qt::PointingHandCursor);
    connect(browse_dir_btn, &QPushButton::clicked, this, &AliasEditDialog::on_browse_dir);
    browse_layout->addWidget(browse_dir_btn);

    auto* browse_file_btn = new QPushButton("Browse File...", target_container);
    browse_file_btn->setCursor(Qt::PointingHandCursor);
    connect(browse_file_btn, &QPushButton::clicked, this, &AliasEditDialog::on_browse_file);
    browse_layout->addWidget(browse_file_btn);

    browse_layout->addStretch();
    target_layout->addLayout(browse_layout);

    form_layout->addRow("Target:", target_container);

    type_badge_ = new QLabel(this);
    type_badge_->setStyleSheet("padding: 3px 8px; border-radius: 4px; font-size: 11px; font-weight: 600;");
    form_layout->addRow("Detected Type:", type_badge_);

    main_layout->addLayout(form_layout);

    connect(target_edit_, &QLineEdit::textChanged, this, &AliasEditDialog::update_preview);

    auto* btn_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btn_box->button(QDialogButtonBox::Ok)->setText("Save Alias");
    btn_box->button(QDialogButtonBox::Ok)->setCursor(Qt::PointingHandCursor);
    btn_box->button(QDialogButtonBox::Cancel)->setCursor(Qt::PointingHandCursor);
    connect(btn_box, &QDialogButtonBox::accepted, this, [this]() {
        if (name().isEmpty()) {
            QMessageBox::warning(this, "Validation Error", "Nickname cannot be empty.");
            name_edit_->setFocus();
            return;
        }
        if (target().isEmpty()) {
            QMessageBox::warning(this, "Validation Error", "Target cannot be empty.");
            target_edit_->setFocus();
            return;
        }
        accept();
    });
    connect(btn_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    main_layout->addWidget(btn_box);
}

void AliasEditDialog::on_browse_dir() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Directory Target", QDir::homePath());
    if (!dir.isEmpty()) {
        target_edit_->setText(dir);
    }
}

void AliasEditDialog::on_browse_file() {
    QString file = QFileDialog::getOpenFileName(this, "Select File Target", QDir::homePath());
    if (!file.isEmpty()) {
        target_edit_->setText(file);
    }
}

void AliasEditDialog::update_preview() {
    const auto badge = [this](const QString& text, const Palette::Badge& colours, const char* extra) {
        type_badge_->setText(text);
        type_badge_->setStyleSheet("background-color: " + colours.bg + "; color: " + colours.fg +
                                   "; border-radius: 4px; padding: 2px 8px;" + extra);
    };

    QString t = target_edit_->text().trimmed();
    if (t.isEmpty()) {
        badge("Empty", palette_.badge_empty, "");
        return;
    }

    auto type = core::AliasManager::detect_type(t.toStdString());
    switch (type) {
        case core::AliasType::Url: badge("🌐 Web Link", palette_.badge_url, " font-weight: bold;"); break;
        case core::AliasType::Directory: badge("📁 Directory", palette_.badge_dir, " font-weight: bold;"); break;
        case core::AliasType::File: badge("📄 File", palette_.badge_file, " font-weight: bold;"); break;
        case core::AliasType::Command: badge("⚡ Command / App", palette_.badge_cmd, " font-weight: bold;"); break;
        case core::AliasType::Shell: badge("⚡ Shell command", palette_.badge_cmd, " font-weight: bold;"); break;
    }
}

QString AliasEditDialog::name() const {
    return name_edit_->text().trimmed();
}

QString AliasEditDialog::target() const {
    return target_edit_->text().trimmed();
}

// ============================================================================
// SettingsDialog
// ============================================================================

namespace {
    SettingsDialog* g_active_settings = nullptr;
    std::function<bool(const std::string&)> g_shortcut_applier;
    std::function<void()> g_directories_changed;
    std::vector<std::pair<std::string, std::string>> g_providers;
    ThemeController* g_theme = nullptr;

    class ModernCheckBox : public QCheckBox {
    public:
        ModernCheckBox(const QString& text, const Palette& palette, QWidget* parent = nullptr)
            : QCheckBox(text, parent), palette_(palette) {
            setCursor(Qt::PointingHandCursor);
            setAttribute(Qt::WA_Hover, true);
        }

        QSize sizeHint() const override {
            QFontMetrics fm(font());
            int w = 32 + fm.horizontalAdvance(text()) + 8;
            int h = std::max(24, fm.height() + 6);
            return QSize(w, h);
        }

    protected:
        void paintEvent(QPaintEvent* /*event*/) override {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);

            int h = height();
            int box_size = 20;
            int box_y = (h - box_size) / 2;
            QRect box_rect(0, box_y, box_size, box_size);

            if (isChecked()) {
                painter.setBrush(QColor(palette_.accent));
                painter.setPen(Qt::NoPen);
                painter.drawRoundedRect(box_rect, 5, 5);

                // Draw crisp white checkmark
                QPen pen(QColor(palette_.on_accent), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                painter.setPen(pen);
                QPainterPath path;
                path.moveTo(box_rect.x() + 5, box_rect.y() + 10);
                path.lineTo(box_rect.x() + 8.5, box_rect.y() + 14);
                path.lineTo(box_rect.x() + 15, box_rect.y() + 6.5);
                painter.drawPath(path);
            } else {
                painter.setBrush(palette_.check_bg);
                QColor border_color = underMouse() ? QColor(palette_.accent) : palette_.check_border;
                painter.setPen(QPen(border_color, 1.5));
                painter.drawRoundedRect(box_rect, 5, 5);
            }

            // Draw label text
            painter.setPen(underMouse() ? palette_.check_text_hover : QColor(palette_.text));
            QFont f = font();
            f.setPointSize(10);
            f.setWeight(QFont::DemiBold);
            painter.setFont(f);
            QRect text_rect(30, 0, width() - 30, h);
            painter.drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter, text());
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void enterEvent(QEnterEvent* event) override {
#else
void enterEvent(QEvent* event) override {
#endif
    QCheckBox::enterEvent(event);
    update();
}

        void leaveEvent(QEvent* event) override {
            QCheckBox::leaveEvent(event);
            update();
        }

    private:
        const Palette& palette_;  // the owning dialog's, updated in place on theme change
    };
}

void SettingsDialog::set_shortcut_applier(std::function<bool(const std::string&)> applier) {
    g_shortcut_applier = std::move(applier);
}

void SettingsDialog::set_theme_controller(ThemeController* theme) {
    g_theme = theme;
}

void SettingsDialog::set_directories_changed_handler(std::function<void()> handler) {
    g_directories_changed = std::move(handler);
}

void SettingsDialog::set_providers(std::vector<std::pair<std::string, std::string>> providers) {
    g_providers = std::move(providers);
}

void SettingsDialog::show_settings(core::ConfigManager& config, core::AliasManager& aliases, QWidget* parent) {
    if (!g_active_settings) {
        g_active_settings = new SettingsDialog(config, aliases, parent);
    }
    if (g_active_settings->ctrl_np_checkbox_) {
        bool blocked = g_active_settings->ctrl_np_checkbox_->blockSignals(true);
        g_active_settings->ctrl_np_checkbox_->setChecked(
            config.is_ctrl_np_navigation_enabled()
        );
        g_active_settings->ctrl_np_checkbox_->blockSignals(blocked);
        g_active_settings->ctrl_np_checkbox_->update();
    }
    if (g_active_settings->shortcut_edit_) {
        g_active_settings->shortcut_edit_->setText(QString::fromStdString(config.get_shortcut()));
    }
    g_active_settings->show();
    g_active_settings->raise();
    g_active_settings->activateWindow();
}

SettingsDialog::SettingsDialog(core::ConfigManager& config, core::AliasManager& aliases, QWidget* parent)
    : QDialog(parent), palette_(g_theme ? g_theme->palette() : Palette::dark()), config_(config), aliases_(aliases) {
    setWindowTitle("Nexus Settings & Configuration");
    resize(640, 480);
    setMinimumSize(540, 400);

    apply_theme();
    setup_ui();
    reload_aliases();
    reload_directories();
    reload_exclusions();
    if (g_theme) {
        connect(g_theme, &ThemeController::palette_changed, this, [this]() {
            palette_ = g_theme->palette();
            apply_theme();
            for (int r = 0; r < alias_table_->rowCount(); ++r) {
                if (auto* item = alias_table_->item(r, 2)) item->setForeground(palette_.target_text);
            }
            ctrl_np_checkbox_->update();
            history_checkbox_->update();
        });
    }
}

void SettingsDialog::apply_theme() {
    const Palette& p = palette_;
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(p.window));
    pal.setColor(QPalette::WindowText, QColor(p.text));
    pal.setColor(QPalette::Base, QColor(p.field));
    pal.setColor(QPalette::Text, QColor(p.text));
    pal.setColor(QPalette::Button, QColor(p.button));
    pal.setColor(QPalette::ButtonText, QColor(p.text_strong));
    pal.setColor(QPalette::Highlight, QColor(p.accent));
    pal.setColor(QPalette::HighlightedText, QColor(p.on_accent));
    setPalette(pal);

    setStyleSheet(
        "QDialog, QMessageBox {"
        "  background-color: " + p.window + ";"
        "  color: " + p.text + ";"
        "  font-family: system-ui, -apple-system, sans-serif;"
        "}"
        "QLabel {"
        "  color: " + p.text + ";"
        "}"
        "QMessageBox QLabel {"
        "  color: " + p.text + ";"
        "  font-size: 13px;"
        "}"
        "QTabWidget::pane {"
        "  border: 1px solid " + p.overlay("0.1") + ";"
        "  background-color: " + p.panel + ";"
        "  border-radius: 8px;"
        "  padding: 10px;"
        "}"
        "QTabBar::tab {"
        "  background: " + p.window + ";"
        "  color: " + p.text_dim + ";"
        "  padding: 8px 20px;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "  border-top-left-radius: 6px;"
        "  border-top-right-radius: 6px;"
        "  margin-right: 2px;"
        "}"
        "QTabBar::tab:selected {"
        "  background: " + p.panel + ";"
        "  color: " + p.text_strong + ";"
        "  border-bottom: 2px solid " + p.accent + ";"
        "}"
        "QTableWidget, QListWidget {"
        "  background-color: " + p.field + ";"
        "  color: " + p.text + ";"
        "  border: 1px solid " + p.overlay("0.08") + ";"
        "  border-radius: 6px;"
        "  gridline-color: " + p.overlay("0.05") + ";"
        "  selection-background-color: " + p.selection_bg + ";"
        "  selection-color: " + p.selection_text + ";"
        "}"
        "QHeaderView::section {"
        "  background-color: " + p.header + ";"
        "  color: " + p.text_soft + ";"
        "  padding: 6px 10px;"
        "  border: none;"
        "  font-weight: 600;"
        "  font-size: 12px;"
        "}"
        "QPushButton {"
        "  background-color: " + p.button + ";"
        "  color: " + p.text_strong + ";"
        "  border: 1px solid " + p.overlay("0.12") + ";"
        "  border-radius: 6px;"
        "  padding: 6px 14px;"
        "  font-weight: 600;"
        "  font-size: 12px;"
        "}"
        "QPushButton:hover {"
        "  background-color: " + p.button_hover + ";"
        "  border-color: " + p.accent + ";"
        "}"
        "QPushButton:pressed {"
        "  background-color: " + p.accent + ";"
        "}"
        "QPushButton#primaryBtn {"
        "  background-color: " + p.accent + ";"
        "  color: " + p.on_accent + ";"
        "  border: none;"
        "  padding: 6px 16px;"
        "}"
        "QPushButton#dangerBtn:hover {"
        "  background-color: " + p.danger + ";"
        "  border-color: " + p.danger_border + ";"
        "}"
        "QLabel#softLabel {"
        "  color: " + p.text_soft + ";"
        "  font-size: 12px;"
        "}"
        "QLabel#hintLabel {"
        "  color: " + p.alias_hint + ";"
        "  font-size: 11px;"
        "}"
        "QLabel#infoLabel {"
        "  color: " + p.config_info + ";"
        "  font-size: 11px;"
        "}"
        "QLabel#optionDesc {"
        "  color: " + p.text_dim + ";"
        "  font-size: 12px;"
        "  line-height: 1.4;"
        "  border: none;"
        "  background: transparent;"
        "  padding-left: 30px;"
        "}"
        "#navCard {"
        "  background-color: " + p.field + ";"
        "  border: 1px solid " + p.overlay("0.08") + ";"
        "  border-radius: 8px;"
        "}"
        "QLineEdit, QSpinBox {"
        "  background-color: " + p.field + ";"
        "  color: " + p.text_strong + ";"
        "  border: 1px solid " + p.overlay("0.15") + ";"
        "  border-radius: 6px;"
        "  padding: 6px 10px;"
        "}"
        "QLineEdit:focus {"
        "  border: 1px solid " + p.accent + ";"
        "}"
    );
}

void SettingsDialog::setup_ui() {
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setSpacing(12);
    main_layout->setContentsMargins(14, 14, 14, 14);

    tab_widget_ = new QTabWidget(this);

    // ========================================================================
    // Tab 1: Aliases & Web Links
    // ========================================================================
    auto* alias_tab = new QWidget();
    auto* alias_layout = new QVBoxLayout(alias_tab);
    alias_layout->setSpacing(10);
    alias_layout->setContentsMargins(8, 8, 8, 8);

    // Filter + Add header row
    auto* alias_header_layout = new QHBoxLayout();
    alias_search_ = new QLineEdit(alias_tab);
    alias_search_->setPlaceholderText("🔍 Filter aliases...");
    connect(alias_search_, &QLineEdit::textChanged, this, &SettingsDialog::on_filter_aliases);
    alias_header_layout->addWidget(alias_search_, 1);

    add_alias_btn_ = new QPushButton("+ Add New Alias", alias_tab);
    add_alias_btn_->setObjectName("primaryBtn");
    add_alias_btn_->setCursor(Qt::PointingHandCursor);
    connect(add_alias_btn_, &QPushButton::clicked, this, &SettingsDialog::on_add_alias);
    alias_header_layout->addWidget(add_alias_btn_);
    alias_layout->addLayout(alias_header_layout);

    // Table
    alias_table_ = new QTableWidget(alias_tab);
    alias_table_->setColumnCount(3);
    alias_table_->setHorizontalHeaderLabels({"Nickname", "Type", "Target (Link / Path / App)"});
    alias_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    alias_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    alias_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    alias_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    alias_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    alias_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    alias_table_->verticalHeader()->setVisible(false);
    connect(alias_table_, &QTableWidget::cellDoubleClicked, this, &SettingsDialog::on_edit_alias);
    alias_layout->addWidget(alias_table_, 1);

    // Alias action buttons
    auto* alias_actions = new QHBoxLayout();
    edit_alias_btn_ = new QPushButton("Edit Selected", alias_tab);
    edit_alias_btn_->setCursor(Qt::PointingHandCursor);
    connect(edit_alias_btn_, &QPushButton::clicked, this, &SettingsDialog::on_edit_alias);
    alias_actions->addWidget(edit_alias_btn_);

    delete_alias_btn_ = new QPushButton("Delete", alias_tab);
    delete_alias_btn_->setCursor(Qt::PointingHandCursor);
    delete_alias_btn_->setObjectName("dangerBtn");
    connect(delete_alias_btn_, &QPushButton::clicked, this, &SettingsDialog::on_delete_alias);
    alias_actions->addWidget(delete_alias_btn_);

    alias_actions->addStretch();
    auto* alias_hint = new QLabel("Tip: Double-click an item to edit it", alias_tab);
    alias_hint->setObjectName("hintLabel");
    alias_actions->addWidget(alias_hint);
    alias_layout->addLayout(alias_actions);

    tab_widget_->addTab(alias_tab, "⭐ Aliases & Web Links");

    // ========================================================================
    // Tab 2: Search Directories
    // ========================================================================
    auto* dir_tab = new QWidget();
    auto* dir_layout = new QVBoxLayout(dir_tab);
    dir_layout->setSpacing(10);
    dir_layout->setContentsMargins(8, 8, 8, 8);

    auto* dir_desc = new QLabel("Nexus searches these directories for files and folders when you type in the search bar:", dir_tab);
    dir_desc->setObjectName("softLabel");
    dir_layout->addWidget(dir_desc);

    dir_list_ = new QListWidget(dir_tab);
    dir_layout->addWidget(dir_list_, 1);

    auto* dir_actions = new QHBoxLayout();
    add_dir_btn_ = new QPushButton("+ Add Directory...", dir_tab);
    add_dir_btn_->setObjectName("primaryBtn");
    add_dir_btn_->setCursor(Qt::PointingHandCursor);
    connect(add_dir_btn_, &QPushButton::clicked, this, &SettingsDialog::on_add_directory);
    dir_actions->addWidget(add_dir_btn_);

    remove_dir_btn_ = new QPushButton("Remove Selected", dir_tab);
    remove_dir_btn_->setCursor(Qt::PointingHandCursor);
    remove_dir_btn_->setObjectName("dangerBtn");
    connect(remove_dir_btn_, &QPushButton::clicked, this, &SettingsDialog::on_remove_directory);
    dir_actions->addWidget(remove_dir_btn_);

    open_dir_btn_ = new QPushButton("Open Folder", dir_tab);
    open_dir_btn_->setCursor(Qt::PointingHandCursor);
    connect(open_dir_btn_, &QPushButton::clicked, this, &SettingsDialog::on_open_directory);
    dir_actions->addWidget(open_dir_btn_);

    dir_actions->addStretch();
    dir_layout->addLayout(dir_actions);

    auto* excl_desc = new QLabel("Folders and files with these names are skipped when indexing (names only, no paths):", dir_tab);
    excl_desc->setObjectName("softLabel");
    dir_layout->addWidget(excl_desc);

    exclusion_list_ = new QListWidget(dir_tab);
    exclusion_list_->setMaximumHeight(110);
    dir_layout->addWidget(exclusion_list_);

    auto* excl_actions = new QHBoxLayout();
    exclusion_edit_ = new QLineEdit(dir_tab);
    exclusion_edit_->setPlaceholderText("e.g. node_modules");
    connect(exclusion_edit_, &QLineEdit::returnPressed, this, &SettingsDialog::on_add_exclusion);
    excl_actions->addWidget(exclusion_edit_, 1);
    auto* add_excl_btn = new QPushButton("Add", dir_tab);
    connect(add_excl_btn, &QPushButton::clicked, this, &SettingsDialog::on_add_exclusion);
    excl_actions->addWidget(add_excl_btn);
    auto* remove_excl_btn = new QPushButton("Remove Selected", dir_tab);
    remove_excl_btn->setObjectName("dangerBtn");
    connect(remove_excl_btn, &QPushButton::clicked, this, &SettingsDialog::on_remove_exclusion);
    excl_actions->addWidget(remove_excl_btn);
    dir_layout->addLayout(excl_actions);

    tab_widget_->addTab(dir_tab, "📁 Search Directories");

    // ========================================================================
    // Tab 3: Navigation & Keyboard
    // ========================================================================
    auto* nav_tab = new QWidget();
    auto* nav_layout = new QVBoxLayout(nav_tab);
    nav_layout->setSpacing(14);
    nav_layout->setContentsMargins(12, 12, 12, 12);

    auto* card = new QWidget(nav_tab);
    card->setObjectName("navCard");
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setSpacing(8);
    card_layout->setContentsMargins(18, 16, 18, 16);

    ctrl_np_checkbox_ = new ModernCheckBox("Enable Ctrl+N / Ctrl+P navigation for search results", palette_, card);
    ctrl_np_checkbox_->setChecked(config_.is_ctrl_np_navigation_enabled());
    connect(ctrl_np_checkbox_, &QCheckBox::toggled, this, &SettingsDialog::on_ctrl_np_toggled);
    card_layout->addWidget(ctrl_np_checkbox_);

    auto* desc_label = new QLabel(
        "When enabled, you can navigate up and down through search suggestions using Ctrl+N (next) "
        "and Ctrl+P (previous) in addition to the arrow keys. This is especially convenient for Vim users "
        "and terminal workflows without moving your hands away from the home row.",
        card
    );
    desc_label->setWordWrap(true);
    desc_label->setCursor(Qt::PointingHandCursor);
    desc_label->setObjectName("optionDesc");
    desc_label->installEventFilter(this);
    card_layout->addWidget(desc_label);

    history_checkbox_ = new ModernCheckBox("Record usage and query history (used to rank results)", palette_, card);
    history_checkbox_->setChecked(config_.is_history_enabled());
    connect(history_checkbox_, &QCheckBox::toggled, this, &SettingsDialog::on_history_toggled);
    card_layout->addWidget(history_checkbox_);

    auto* theme_row = new QHBoxLayout();
    auto* theme_label = new QLabel("Theme:", card);
    theme_label->setStyleSheet("border: none; background: transparent;");
    theme_combo_ = new QComboBox(card);
    theme_combo_->addItem("Dark", static_cast<int>(utils::ThemeMode::Dark));
    theme_combo_->addItem("Light", static_cast<int>(utils::ThemeMode::Light));
    theme_combo_->addItem("System", static_cast<int>(utils::ThemeMode::System));
    theme_combo_->setCurrentIndex(theme_combo_->findData(static_cast<int>(config_.get_theme())));
    connect(theme_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsDialog::on_theme_changed);
    theme_row->addWidget(theme_label);
    theme_row->addWidget(theme_combo_, 1);
    card_layout->addLayout(theme_row);

    auto* shortcut_row = new QHBoxLayout();
    auto* shortcut_label = new QLabel("Global shortcut:", card);
    shortcut_label->setStyleSheet("border: none; background: transparent;");
    shortcut_edit_ = new QLineEdit(QString::fromStdString(config_.get_shortcut()), card);
    shortcut_edit_->setPlaceholderText("e.g. Alt+Space, Ctrl+Shift+K");
    auto* shortcut_apply = new QPushButton("Apply", card);
    connect(shortcut_apply, &QPushButton::clicked, this, &SettingsDialog::on_apply_shortcut);
    connect(shortcut_edit_, &QLineEdit::returnPressed, this, &SettingsDialog::on_apply_shortcut);
    shortcut_row->addWidget(shortcut_label);
    shortcut_row->addWidget(shortcut_edit_, 1);
    shortcut_row->addWidget(shortcut_apply);
    card_layout->addLayout(shortcut_row);

    nav_layout->addWidget(card);
    nav_layout->addStretch();

    tab_widget_->addTab(nav_tab, "⌨ Navigation");

    // ========================================================================
    // Tab 4: Search (result count, providers on/off)
    // ========================================================================
    auto* search_tab = new QWidget();
    auto* search_layout = new QVBoxLayout(search_tab);
    search_layout->setSpacing(14);
    search_layout->setContentsMargins(12, 12, 12, 12);

    auto* search_card = new QWidget(search_tab);
    search_card->setObjectName("navCard");
    auto* search_card_layout = new QVBoxLayout(search_card);
    search_card_layout->setSpacing(8);
    search_card_layout->setContentsMargins(18, 16, 18, 16);

    auto* count_row = new QHBoxLayout();
    auto* count_label = new QLabel("Results shown:", search_card);
    count_label->setStyleSheet("border: none; background: transparent;");
    auto* count_spin = new QSpinBox(search_card);
    count_spin->setRange(core::ConfigManager::kMinResultCount, core::ConfigManager::kMaxResultCount);
    count_spin->setValue(config_.get_result_count());
    connect(count_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int n) {
        config_.set_result_count(n);
        emit settings_changed();
    });
    count_row->addWidget(count_label);
    count_row->addWidget(count_spin);
    count_row->addStretch();
    search_card_layout->addLayout(count_row);

    auto* providers_label = new QLabel("Search sources (untick to turn one off):", search_card);
    providers_label->setStyleSheet("border: none; background: transparent;");
    search_card_layout->addWidget(providers_label);
    for (const auto& [id, name] : g_providers) {
        auto* box = new ModernCheckBox(QString::fromStdString(name), palette_, search_card);
        box->setChecked(config_.is_provider_enabled(id));
        connect(box, &QCheckBox::toggled, this, [this, id = id](bool on) {
            config_.set_provider_enabled(id, on);
            emit settings_changed();
        });
        search_card_layout->addWidget(box);
    }

    search_layout->addWidget(search_card);
    search_layout->addStretch();
    tab_widget_->addTab(search_tab, "🔎 Search");

    main_layout->addWidget(tab_widget_, 1);

    // Bottom bar
    auto* bottom_layout = new QHBoxLayout();
    auto* config_info = new QLabel("Changes are saved automatically to ~/.config/nexus/", this);
    config_info->setObjectName("infoLabel");
    bottom_layout->addWidget(config_info);

    bottom_layout->addStretch();

    auto* close_btn = new QPushButton("Close", this);
    close_btn->setCursor(Qt::PointingHandCursor);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
    bottom_layout->addWidget(close_btn);

    main_layout->addLayout(bottom_layout);
}

// ============================================================================
// Aliases Operations
// ============================================================================

void SettingsDialog::reload_aliases() {
    alias_table_->setRowCount(0);
    auto aliases = aliases_.get_aliases();

    for (const auto& a : aliases) {
        int row = alias_table_->rowCount();
        alias_table_->insertRow(row);

        auto* name_item = new QTableWidgetItem(QString::fromStdString(a.name));
        name_item->setFont(QFont(name_item->font().family(), -1, QFont::Bold));

        QString type_str;
        switch (a.type) {
            case core::AliasType::Url: type_str = "🌐 Web Link"; break;
            case core::AliasType::Directory: type_str = "📁 Directory"; break;
            case core::AliasType::File: type_str = "📄 File"; break;
            case core::AliasType::Command: type_str = "⚡ Command"; break;
            case core::AliasType::Shell: type_str = "⚡ Shell command"; break;
        }
        auto* type_item = new QTableWidgetItem(type_str);

        auto* target_item = new QTableWidgetItem(QString::fromStdString(a.target));
        target_item->setForeground(palette_.target_text);

        alias_table_->setItem(row, 0, name_item);
        alias_table_->setItem(row, 1, type_item);
        alias_table_->setItem(row, 2, target_item);
    }
}

void SettingsDialog::on_filter_aliases(const QString& text) {
    QString q = text.trimmed().toLower();
    for (int r = 0; r < alias_table_->rowCount(); ++r) {
        bool match = false;
        for (int c = 0; c < alias_table_->columnCount(); ++c) {
            auto* item = alias_table_->item(r, c);
            if (item && item->text().toLower().contains(q)) {
                match = true;
                break;
            }
        }
        alias_table_->setRowHidden(r, !match);
    }
}

void SettingsDialog::on_add_alias() {
    AliasEditDialog dlg(palette_, this);
    if (dlg.exec() == QDialog::Accepted) {
        aliases_.add_alias(dlg.name().toStdString(), dlg.target().toStdString());
        reload_aliases();
        emit settings_changed();
    }
}

void SettingsDialog::on_edit_alias() {
    int row = alias_table_->currentRow();
    if (row < 0) return;

    QString current_name = alias_table_->item(row, 0)->text();
    QString current_target = alias_table_->item(row, 2)->text();

    AliasEditDialog dlg(palette_, this, current_name, current_target);
    if (dlg.exec() == QDialog::Accepted) {
        if (dlg.name() != current_name) {
            aliases_.remove_alias(current_name.toStdString());
        }
        aliases_.add_alias(dlg.name().toStdString(), dlg.target().toStdString());
        reload_aliases();
        emit settings_changed();
    }
}

void SettingsDialog::on_delete_alias() {
    int row = alias_table_->currentRow();
    if (row < 0) return;

    QString name = alias_table_->item(row, 0)->text();
    auto res = QMessageBox::question(this, "Confirm Deletion",
                                     QString("Are you sure you want to remove alias '%1'?").arg(name),
                                     QMessageBox::Yes | QMessageBox::No);
    if (res == QMessageBox::Yes) {
        aliases_.remove_alias(name.toStdString());
        reload_aliases();
        emit settings_changed();
    }
}

// ============================================================================
// Directory Operations
// ============================================================================

void SettingsDialog::reload_directories() {
    dir_list_->clear();
    auto dirs = config_.get_search_directories();
    QIcon folder_icon = QIcon::fromTheme("folder");

    for (const auto& d : dirs) {
        auto* item = new QListWidgetItem(folder_icon, QString::fromStdString(d), dir_list_);
        item->setSizeHint(QSize(item->sizeHint().width(), 32));
    }
}

void SettingsDialog::on_add_directory() {
    QString dir = QFileDialog::getExistingDirectory(this, "Choose Search Directory", QDir::homePath());
    if (!dir.isEmpty()) {
        if (config_.add_search_directory(dir.toStdString())) {
            reload_directories();
            if (g_directories_changed) g_directories_changed();
            emit settings_changed();
        } else {
            QMessageBox::warning(this, "Failed to Add Directory", "Directory could not be added or already exists.");
        }
    }
}

void SettingsDialog::on_remove_directory() {
    auto* item = dir_list_->currentItem();
    if (!item) return;

    QString path = item->text();
    auto res = QMessageBox::question(this, "Confirm Removal",
                                     QString("Remove '%1' from search paths?").arg(path),
                                     QMessageBox::Yes | QMessageBox::No);
    if (res == QMessageBox::Yes) {
        config_.remove_search_directory(path.toStdString());
        reload_directories();
        if (g_directories_changed) g_directories_changed();
        emit settings_changed();
    }
}

void SettingsDialog::reload_exclusions() {
    exclusion_list_->clear();
    for (const auto& n : config_.get_file_exclusions()) {
        exclusion_list_->addItem(QString::fromStdString(n));
    }
}

void SettingsDialog::on_add_exclusion() {
    const std::string name = exclusion_edit_->text().trimmed().toStdString();
    if (name.empty()) return;
    auto& cfg = config_;
    auto names = cfg.get_file_exclusions();
    names.push_back(name);
    cfg.set_file_exclusions(names);
    const auto after = cfg.get_file_exclusions();
    if (std::find(after.begin(), after.end(), name) == after.end()) {
        QMessageBox::warning(this, "Invalid name", "Use a bare file or folder name without slashes, quotes or backslashes.");
        return;
    }
    exclusion_edit_->clear();
    reload_exclusions();
    if (g_directories_changed) g_directories_changed();  // exclusions only apply when the index is rebuilt
    emit settings_changed();
}

void SettingsDialog::on_remove_exclusion() {
    auto* item = exclusion_list_->currentItem();
    if (!item) return;
    auto& cfg = config_;
    auto names = cfg.get_file_exclusions();
    names.erase(std::remove(names.begin(), names.end(), item->text().toStdString()), names.end());
    cfg.set_file_exclusions(names);
    reload_exclusions();
    if (g_directories_changed) g_directories_changed();
    emit settings_changed();
}

void SettingsDialog::on_open_directory() {
    auto* item = dir_list_->currentItem();
    if (!item) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(item->text()));
}

void SettingsDialog::on_ctrl_np_toggled(bool checked) {
    config_.set_ctrl_np_navigation_enabled(checked);
    emit settings_changed();
}

void SettingsDialog::on_theme_changed(int index) {
    const auto mode = static_cast<utils::ThemeMode>(theme_combo_->itemData(index).toInt());
    config_.set_theme(mode);
    if (g_theme) g_theme->set_mode(mode);
}

void SettingsDialog::on_history_toggled(bool checked) {
    config_.set_history_enabled(checked);
    emit settings_changed();
}

void SettingsDialog::on_apply_shortcut() {
    const std::string text = shortcut_edit_->text().toStdString();
    if (!g_shortcut_applier || !g_shortcut_applier(text)) {
        QMessageBox::warning(this, "Invalid shortcut",
                             "Use modifiers plus a key, for example Alt+Space or Ctrl+Shift+K.");
        return;
    }
    emit settings_changed();
}

bool SettingsDialog::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonRelease) {
        if (ctrl_np_checkbox_) {
            ctrl_np_checkbox_->toggle();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

} // namespace nexus::ui

