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

namespace nexus::ui {

// ============================================================================
// AliasEditDialog
// ============================================================================

AliasEditDialog::AliasEditDialog(QWidget* parent,
                                 const QString& initial_name,
                                 const QString& initial_target)
    : QDialog(parent) {
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
    QString t = target_edit_->text().trimmed();
    if (t.isEmpty()) {
        type_badge_->setText("Empty");
        type_badge_->setStyleSheet("background-color: #383844; color: #888894; border-radius: 4px; padding: 2px 8px;");
        return;
    }

    auto type = core::AliasManager::detect_type(t.toStdString());
    switch (type) {
        case core::AliasType::Url:
            type_badge_->setText("🌐 Web Link");
            type_badge_->setStyleSheet("background-color: #0d47a1; color: #90caf9; border-radius: 4px; padding: 2px 8px; font-weight: bold;");
            break;
        case core::AliasType::Directory:
            type_badge_->setText("📁 Directory");
            type_badge_->setStyleSheet("background-color: #1b5e20; color: #a5d6a7; border-radius: 4px; padding: 2px 8px; font-weight: bold;");
            break;
        case core::AliasType::File:
            type_badge_->setText("📄 File");
            type_badge_->setStyleSheet("background-color: #4a148c; color: #ce93d8; border-radius: 4px; padding: 2px 8px; font-weight: bold;");
            break;
        case core::AliasType::Command:
            type_badge_->setText("⚡ Command / App");
            type_badge_->setStyleSheet("background-color: #e65100; color: #ffcc80; border-radius: 4px; padding: 2px 8px; font-weight: bold;");
            break;
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

    class ModernCheckBox : public QCheckBox {
    public:
        explicit ModernCheckBox(const QString& text, QWidget* parent = nullptr)
            : QCheckBox(text, parent) {
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
                painter.setBrush(QColor(0, 122, 255)); // #007AFF
                painter.setPen(Qt::NoPen);
                painter.drawRoundedRect(box_rect, 5, 5);

                // Draw crisp white checkmark
                QPen pen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                painter.setPen(pen);
                QPainterPath path;
                path.moveTo(box_rect.x() + 5, box_rect.y() + 10);
                path.lineTo(box_rect.x() + 8.5, box_rect.y() + 14);
                path.lineTo(box_rect.x() + 15, box_rect.y() + 6.5);
                painter.drawPath(path);
            } else {
                painter.setBrush(QColor(22, 22, 28));
                QColor border_color = underMouse() ? QColor(0, 122, 255) : QColor(255, 255, 255, 75);
                painter.setPen(QPen(border_color, 1.5));
                painter.drawRoundedRect(box_rect, 5, 5);
            }

            // Draw label text
            painter.setPen(underMouse() ? QColor(255, 255, 255) : QColor(240, 240, 245));
            QFont f = font();
            f.setPointSize(10);
            f.setWeight(QFont::DemiBold);
            painter.setFont(f);
            QRect text_rect(30, 0, width() - 30, h);
            painter.drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter, text());
        }

        void enterEvent(QEvent* event) override {
            QCheckBox::enterEvent(event);
            update();
        }

        void leaveEvent(QEvent* event) override {
            QCheckBox::leaveEvent(event);
            update();
        }
    };
}

void SettingsDialog::show_settings(QWidget* parent) {
    if (!g_active_settings) {
        g_active_settings = new SettingsDialog(parent);
    }
    if (g_active_settings->ctrl_np_checkbox_) {
        bool blocked = g_active_settings->ctrl_np_checkbox_->blockSignals(true);
        g_active_settings->ctrl_np_checkbox_->setChecked(
            core::ConfigManager::instance().is_ctrl_np_navigation_enabled()
        );
        g_active_settings->ctrl_np_checkbox_->blockSignals(blocked);
        g_active_settings->ctrl_np_checkbox_->update();
    }
    g_active_settings->show();
    g_active_settings->raise();
    g_active_settings->activateWindow();
}

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Nexus Settings & Configuration");
    resize(640, 480);
    setMinimumSize(540, 400);

    apply_theme();
    setup_ui();
    reload_aliases();
    reload_directories();
}

void SettingsDialog::apply_theme() {
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(24, 24, 30));
    pal.setColor(QPalette::WindowText, QColor(240, 240, 245));
    pal.setColor(QPalette::Base, QColor(20, 20, 24));
    pal.setColor(QPalette::Text, QColor(240, 240, 245));
    pal.setColor(QPalette::Button, QColor(45, 45, 56));
    pal.setColor(QPalette::ButtonText, QColor(255, 255, 255));
    pal.setColor(QPalette::Highlight, QColor(0, 122, 255));
    pal.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    setPalette(pal);

    setStyleSheet(
        "QDialog, QMessageBox {"
        "  background-color: #18181E;"
        "  color: #F0F0F5;"
        "  font-family: system-ui, -apple-system, sans-serif;"
        "}"
        "QLabel {"
        "  color: #F0F0F5;"
        "}"
        "QMessageBox QLabel {"
        "  color: #F0F0F5;"
        "  font-size: 13px;"
        "}"
        "QTabWidget::pane {"
        "  border: 1px solid rgba(255, 255, 255, 0.1);"
        "  background-color: #202028;"
        "  border-radius: 8px;"
        "  padding: 10px;"
        "}"
        "QTabBar::tab {"
        "  background: #18181E;"
        "  color: #9A9AA4;"
        "  padding: 8px 20px;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "  border-top-left-radius: 6px;"
        "  border-top-right-radius: 6px;"
        "  margin-right: 2px;"
        "}"
        "QTabBar::tab:selected {"
        "  background: #202028;"
        "  color: #FFFFFF;"
        "  border-bottom: 2px solid #007AFF;"
        "}"
        "QTableWidget, QListWidget {"
        "  background-color: #141418;"
        "  color: #F0F0F5;"
        "  border: 1px solid rgba(255, 255, 255, 0.08);"
        "  border-radius: 6px;"
        "  gridline-color: rgba(255, 255, 255, 0.05);"
        "  selection-background-color: rgba(0, 122, 255, 0.35);"
        "  selection-color: #FFFFFF;"
        "}"
        "QHeaderView::section {"
        "  background-color: #1C1C24;"
        "  color: #A0A0B0;"
        "  padding: 6px 10px;"
        "  border: none;"
        "  font-weight: 600;"
        "  font-size: 12px;"
        "}"
        "QPushButton {"
        "  background-color: #2D2D38;"
        "  color: #FFFFFF;"
        "  border: 1px solid rgba(255, 255, 255, 0.12);"
        "  border-radius: 6px;"
        "  padding: 6px 14px;"
        "  font-weight: 600;"
        "  font-size: 12px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #383846;"
        "  border-color: #007AFF;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #007AFF;"
        "}"
        "QLineEdit {"
        "  background-color: #141418;"
        "  color: #FFFFFF;"
        "  border: 1px solid rgba(255, 255, 255, 0.15);"
        "  border-radius: 6px;"
        "  padding: 6px 10px;"
        "}"
        "QLineEdit:focus {"
        "  border: 1px solid #007AFF;"
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
    add_alias_btn_->setStyleSheet("background-color: #007AFF; border: none; padding: 6px 16px;");
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
    delete_alias_btn_->setStyleSheet("QPushButton:hover { background-color: #c92a2a; border-color: #ff6b6b; }");
    connect(delete_alias_btn_, &QPushButton::clicked, this, &SettingsDialog::on_delete_alias);
    alias_actions->addWidget(delete_alias_btn_);

    alias_actions->addStretch();
    auto* alias_hint = new QLabel("Tip: Double-click an item to edit it", alias_tab);
    alias_hint->setStyleSheet("color: #72727E; font-size: 11px;");
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
    dir_desc->setStyleSheet("color: #A0A0B0; font-size: 12px;");
    dir_layout->addWidget(dir_desc);

    dir_list_ = new QListWidget(dir_tab);
    dir_layout->addWidget(dir_list_, 1);

    auto* dir_actions = new QHBoxLayout();
    add_dir_btn_ = new QPushButton("+ Add Directory...", dir_tab);
    add_dir_btn_->setStyleSheet("background-color: #007AFF; border: none; padding: 6px 16px;");
    add_dir_btn_->setCursor(Qt::PointingHandCursor);
    connect(add_dir_btn_, &QPushButton::clicked, this, &SettingsDialog::on_add_directory);
    dir_actions->addWidget(add_dir_btn_);

    remove_dir_btn_ = new QPushButton("Remove Selected", dir_tab);
    remove_dir_btn_->setCursor(Qt::PointingHandCursor);
    remove_dir_btn_->setStyleSheet("QPushButton:hover { background-color: #c92a2a; border-color: #ff6b6b; }");
    connect(remove_dir_btn_, &QPushButton::clicked, this, &SettingsDialog::on_remove_directory);
    dir_actions->addWidget(remove_dir_btn_);

    open_dir_btn_ = new QPushButton("Open Folder", dir_tab);
    open_dir_btn_->setCursor(Qt::PointingHandCursor);
    connect(open_dir_btn_, &QPushButton::clicked, this, &SettingsDialog::on_open_directory);
    dir_actions->addWidget(open_dir_btn_);

    dir_actions->addStretch();
    dir_layout->addLayout(dir_actions);

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
    card->setStyleSheet(
        "#navCard {"
        "  background-color: #141418;"
        "  border: 1px solid rgba(255, 255, 255, 0.08);"
        "  border-radius: 8px;"
        "}"
    );
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setSpacing(8);
    card_layout->setContentsMargins(18, 16, 18, 16);

    ctrl_np_checkbox_ = new ModernCheckBox("Enable Ctrl+N / Ctrl+P navigation for search results", card);
    ctrl_np_checkbox_->setChecked(core::ConfigManager::instance().is_ctrl_np_navigation_enabled());
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
    desc_label->setStyleSheet("color: #9A9AA4; font-size: 12px; line-height: 1.4; border: none; background: transparent; padding-left: 30px;");
    desc_label->installEventFilter(this);
    card_layout->addWidget(desc_label);

    nav_layout->addWidget(card);
    nav_layout->addStretch();

    tab_widget_->addTab(nav_tab, "⌨ Navigation");

    main_layout->addWidget(tab_widget_, 1);

    // Bottom bar
    auto* bottom_layout = new QHBoxLayout();
    auto* config_info = new QLabel("Changes are saved automatically to ~/.config/nexus/", this);
    config_info->setStyleSheet("color: #666675; font-size: 11px;");
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
    auto aliases = core::AliasManager::instance().get_aliases();

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
        }
        auto* type_item = new QTableWidgetItem(type_str);

        auto* target_item = new QTableWidgetItem(QString::fromStdString(a.target));
        target_item->setForeground(QColor(180, 200, 230));

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
    AliasEditDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        core::AliasManager::instance().add_alias(dlg.name().toStdString(), dlg.target().toStdString());
        reload_aliases();
        emit settings_changed();
    }
}

void SettingsDialog::on_edit_alias() {
    int row = alias_table_->currentRow();
    if (row < 0) return;

    QString current_name = alias_table_->item(row, 0)->text();
    QString current_target = alias_table_->item(row, 2)->text();

    AliasEditDialog dlg(this, current_name, current_target);
    if (dlg.exec() == QDialog::Accepted) {
        if (dlg.name() != current_name) {
            core::AliasManager::instance().remove_alias(current_name.toStdString());
        }
        core::AliasManager::instance().add_alias(dlg.name().toStdString(), dlg.target().toStdString());
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
        core::AliasManager::instance().remove_alias(name.toStdString());
        reload_aliases();
        emit settings_changed();
    }
}

// ============================================================================
// Directory Operations
// ============================================================================

void SettingsDialog::reload_directories() {
    dir_list_->clear();
    auto dirs = core::ConfigManager::instance().get_search_directories();
    QIcon folder_icon = QIcon::fromTheme("folder");

    for (const auto& d : dirs) {
        auto* item = new QListWidgetItem(folder_icon, QString::fromStdString(d), dir_list_);
        item->setSizeHint(QSize(item->sizeHint().width(), 32));
    }
}

void SettingsDialog::on_add_directory() {
    QString dir = QFileDialog::getExistingDirectory(this, "Choose Search Directory", QDir::homePath());
    if (!dir.isEmpty()) {
        if (core::ConfigManager::instance().add_search_directory(dir.toStdString())) {
            reload_directories();
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
        core::ConfigManager::instance().remove_search_directory(path.toStdString());
        reload_directories();
        emit settings_changed();
    }
}

void SettingsDialog::on_open_directory() {
    auto* item = dir_list_->currentItem();
    if (!item) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(item->text()));
}

void SettingsDialog::on_ctrl_np_toggled(bool checked) {
    core::ConfigManager::instance().set_ctrl_np_navigation_enabled(checked);
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

