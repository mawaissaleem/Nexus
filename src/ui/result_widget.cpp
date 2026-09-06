#include "nexus/ui/result_widget.hpp"
#include <QIcon>
#include <QPixmap>
#include <QFileInfo>
#include <QStyle>

namespace nexus::ui {

ResultWidget::ResultWidget(const core::SearchResult& result, QWidget* parent)
    : QWidget(parent), result_(result) {
    setup_ui();
}

void ResultWidget::setup_ui() {
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName("resultWidget");

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 12, 6);
    layout->setSpacing(10);

    // 0. Selection indicator bar
    indicator_bar_ = new QWidget(this);
    indicator_bar_->setFixedWidth(4);
    indicator_bar_->setFixedHeight(32);
    indicator_bar_->setStyleSheet("background-color: transparent; border-radius: 2px;");
    layout->addWidget(indicator_bar_);

    // 1. Icon
    icon_label_ = new QLabel(this);
    icon_label_->setFixedSize(36, 36);
    icon_label_->setScaledContents(true);

    QIcon icon;
    if (!result_.icon.empty()) {
        QString icon_name = QString::fromStdString(result_.icon);
        if (QIcon::hasThemeIcon(icon_name)) {
            icon = QIcon::fromTheme(icon_name);
        } else if (QFileInfo::exists(icon_name)) {
            icon = QIcon(icon_name);
        }
    }

    if (icon.isNull()) {
        if (result_.provider_id == "calculator") {
            icon = QIcon::fromTheme("accessories-calculator");
        } else if (result_.provider_id == "shell") {
            icon = QIcon::fromTheme("utilities-terminal");
        } else if (result_.provider_id == "files") {
            icon = QIcon::fromTheme("folder");
        } else if (result_.provider_id == "alias") {
            icon = QIcon::fromTheme("emblem-favorite");
        } else {
            icon = QIcon::fromTheme("application-x-executable");
        }
    }

    if (!icon.isNull()) {
        icon_label_->setPixmap(icon.pixmap(36, 36));
    }

    layout->addWidget(icon_label_);

    // 2. Text layout (Title + Subtitle)
    auto* text_layout = new QVBoxLayout();
    text_layout->setContentsMargins(0, 0, 0, 0);
    text_layout->setSpacing(2);

    title_label_ = new QLabel(QString::fromStdString(result_.title), this);
    title_label_->setStyleSheet("color: #FFFFFF; font-weight: 600; font-size: 14px;");

    subtitle_label_ = new QLabel(QString::fromStdString(result_.subtitle), this);
    subtitle_label_->setStyleSheet("color: #9A9AA0; font-size: 12px;");

    text_layout->addWidget(title_label_);
    if (!result_.subtitle.empty()) {
        text_layout->addWidget(subtitle_label_);
    }

    layout->addLayout(text_layout, 1);

    // 3. Provider badge
    badge_label_ = new QLabel(QString::fromStdString(result_.provider_id), this);
    badge_label_->setAlignment(Qt::AlignCenter);
    layout->addWidget(badge_label_);

    set_selected(false);
}

void ResultWidget::set_selected(bool selected) {
    if (selected) {
        indicator_bar_->setStyleSheet("background-color: #007AFF; border-radius: 2px;");
        setStyleSheet(
            "#resultWidget {"
            "  background-color: rgba(0, 122, 255, 0.32);"
            "  border: 1px solid rgba(0, 140, 255, 0.85);"
            "  border-radius: 8px;"
            "}"
        );
        title_label_->setStyleSheet("color: #FFFFFF; font-weight: 700; font-size: 14px;");
        subtitle_label_->setStyleSheet("color: #CBE2FF; font-size: 12px;");
        badge_label_->setStyleSheet(
            "background-color: #007AFF;"
            "color: #FFFFFF;"
            "border-radius: 4px;"
            "padding: 3px 8px;"
            "font-size: 11px;"
            "font-weight: 600;"
        );
    } else {
        indicator_bar_->setStyleSheet("background-color: transparent;");
        setStyleSheet(
            "#resultWidget {"
            "  background-color: transparent;"
            "  border: 1px solid transparent;"
            "  border-radius: 8px;"
            "}"
        );
        title_label_->setStyleSheet("color: #D8D8DC; font-weight: 500; font-size: 14px;");
        subtitle_label_->setStyleSheet("color: #8C8C94; font-size: 12px;");
        badge_label_->setStyleSheet(
            "background-color: rgba(255, 255, 255, 0.08);"
            "color: #9A9AA4;"
            "border-radius: 4px;"
            "padding: 2px 8px;"
            "font-size: 11px;"
            "font-weight: 500;"
        );
    }
}

} // namespace nexus::ui

