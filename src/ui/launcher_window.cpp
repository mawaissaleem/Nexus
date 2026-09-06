#include "nexus/ui/launcher_window.hpp"
#include "nexus/ui/result_widget.hpp"
#include "nexus/core/query.hpp"
#include "nexus/utils/logger.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QScreen>
#include <QWindow>
#include <QGuiApplication>
#include <QGraphicsDropShadowEffect>

namespace nexus::ui {

LauncherWindow::LauncherWindow(
    std::shared_ptr<core::SearchEngine> search_engine,
    QWidget* parent
) : QWidget(parent), search_engine_(std::move(search_engine)) {
    setup_ui();

    debounce_timer_ = new QTimer(this);
    debounce_timer_->setSingleShot(true);
    connect(debounce_timer_, &QTimer::timeout, this, &LauncherWindow::perform_search);
}

void LauncherWindow::setup_ui() {
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(680);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(10, 10, 10, 10);

    // Main rounded container
    container_ = new QWidget(this);
    container_->setObjectName("container");
    container_->setStyleSheet(
        "#container {"
        "  background-color: rgba(28, 28, 34, 0.96);"
        "  border: 1px solid rgba(255, 255, 255, 0.16);"
        "  border-radius: 14px;"
        "}"
    );

    // Drop shadow effect
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(28);
    shadow->setColor(QColor(0, 0, 0, 160));
    shadow->setOffset(0, 10);
    container_->setGraphicsEffect(shadow);

    auto* container_layout = new QVBoxLayout(container_);
    container_layout->setContentsMargins(16, 14, 16, 12);
    container_layout->setSpacing(10);

    // 1. Search Bar Layout
    auto* search_layout = new QHBoxLayout();
    search_layout->setContentsMargins(6, 2, 6, 2);
    search_layout->setSpacing(10);

    QLabel* search_icon = new QLabel(container_);
    QIcon s_icon = QIcon::fromTheme("system-search");
    if (!s_icon.isNull()) {
        search_icon->setPixmap(s_icon.pixmap(22, 22));
    }
    search_layout->addWidget(search_icon);

    search_input_ = new QLineEdit(container_);
    search_input_->setPlaceholderText("Search applications, calc 125 * 37, or > commands...");
    search_input_->setStyleSheet(
        "QLineEdit {"
        "  background: transparent;"
        "  border: none;"
        "  color: #FFFFFF;"
        "  font-size: 18px;"
        "  font-weight: 500;"
        "  padding: 4px;"
        "  selection-background-color: #007AFF;"
        "}"
    );
    search_input_->installEventFilter(this);
    connect(search_input_, &QLineEdit::textChanged, this, &LauncherWindow::on_text_changed);
    search_layout->addWidget(search_input_, 1);

    container_layout->addLayout(search_layout);

    // Separator line
    QFrame* separator = new QFrame(container_);
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet("background-color: rgba(255, 255, 255, 0.08); max-height: 1px;");
    container_layout->addWidget(separator);

    // 2. Results List
    result_list_ = new QListWidget(container_);
    result_list_->setStyleSheet(
        "QListWidget {"
        "  background: transparent;"
        "  border: none;"
        "  outline: none;"
        "}"
        "QListWidget::item {"
        "  border-radius: 8px;"
        "  padding: 0px;"
        "}"
        "QListWidget::item:selected {"
        "  background: transparent;"
        "}"
        "QScrollBar:vertical {"
        "  border: none;"
        "  background: transparent;"
        "  width: 6px;"
        "  margin: 0px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: rgba(255, 255, 255, 0.2);"
        "  min-height: 20px;"
        "  border-radius: 3px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0px;"
        "}"
    );
    result_list_->setFocusPolicy(Qt::NoFocus);
    connect(result_list_, &QListWidget::itemClicked, this, &LauncherWindow::on_item_clicked);
    connect(result_list_, &QListWidget::currentRowChanged, this, [this](int row) {
        for (int i = 0; i < result_list_->count(); ++i) {
            auto* item = result_list_->item(i);
            auto* widget = qobject_cast<ResultWidget*>(result_list_->itemWidget(item));
            if (widget) {
                widget->set_selected(i == row);
            }
        }
    });

    container_layout->addWidget(result_list_);

    // 3. Footer hints
    footer_ = new QWidget(container_);
    auto* footer_layout = new QHBoxLayout(footer_);
    footer_layout->setContentsMargins(6, 4, 6, 2);

    QLabel* hint = new QLabel("↑↓ Navigate   •   ↵ Launch   •   Esc Close", footer_);
    hint->setStyleSheet("color: #72727A; font-size: 11px; font-weight: 500;");
    footer_layout->addWidget(hint);

    footer_layout->addStretch();

    QLabel* brand = new QLabel("Nexus Launcher", footer_);
    brand->setStyleSheet("color: #55555E; font-size: 11px; font-weight: 600;");
    footer_layout->addWidget(brand);

    container_layout->addWidget(footer_);

    main_layout->addWidget(container_);

    // Initial empty list state
    result_list_->hide();
    adjustSize();
}

void LauncherWindow::center_on_screen() {
    // Detect screen under cursor (where user is currently working)
    QPoint cursor_pos = QCursor::pos();
    QScreen* screen = QGuiApplication::screenAt(cursor_pos);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) return;

    QRect geom = screen->geometry();
    int x = geom.x() + (geom.width() - width()) / 2;
    int y = geom.y() + (geom.height() / 4); // Position in upper third of active monitor

    // Update screen and geometry
    if (windowHandle()) {
        windowHandle()->setScreen(screen);
    }
    move(x, y);
}

void LauncherWindow::show_launcher() {
    center_on_screen();
    show();
    raise();
    activateWindow();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    search_input_->setFocus(Qt::OtherFocusReason);
    search_input_->selectAll();

    // Trigger initial search if there is text
    if (!search_input_->text().trimmed().isEmpty()) {
        perform_search();
    }
}

void LauncherWindow::hide_launcher() {
    hide();
}

void LauncherWindow::toggle_launcher() {
    if (isVisible() && isActiveWindow()) {
        hide_launcher();
    } else {
        show_launcher();
    }
}

void LauncherWindow::on_text_changed(const QString& /*text*/) {
    debounce_timer_->start(25); // 25 ms debounce
}

void LauncherWindow::perform_search() {
    QString q_str = search_input_->text().trimmed();
    if (q_str.isEmpty()) {
        result_list_->clear();
        result_list_->hide();
        current_results_.clear();
        adjustSize();
        return;
    }

    core::Query query(q_str.toStdString());
    auto results = search_engine_->search(query, 7);
    update_results(results);
}

void LauncherWindow::update_results(const std::vector<core::SearchResult>& results) {
    result_list_->clear();
    current_results_ = results;

    if (results.empty()) {
        result_list_->hide();
        adjustSize();
        return;
    }

    for (const auto& res : results) {
        auto* item = new QListWidgetItem(result_list_);
        auto* widget = new ResultWidget(res, result_list_);
        item->setSizeHint(widget->sizeHint());
        result_list_->addItem(item);
        result_list_->setItemWidget(item, widget);
    }

    int visible_items = std::min<int>(results.size(), 6);
    int list_height = visible_items * 52;
    result_list_->setFixedHeight(list_height);
    result_list_->show();

    result_list_->setCurrentRow(0);
    for (int i = 0; i < result_list_->count(); ++i) {
        auto* item = result_list_->item(i);
        auto* widget = qobject_cast<ResultWidget*>(result_list_->itemWidget(item));
        if (widget) {
            widget->set_selected(i == 0);
        }
    }
    adjustSize();
}

void LauncherWindow::execute_selected() {
    int row = result_list_->currentRow();
    if (row >= 0 && row < static_cast<int>(current_results_.size())) {
        const auto& res = current_results_[row];
        hide_launcher();
        if (res.action) {
            res.action();
        }
    }
}

void LauncherWindow::on_item_clicked(QListWidgetItem* item) {
    int row = result_list_->row(item);
    if (row >= 0 && row < static_cast<int>(current_results_.size())) {
        const auto& res = current_results_[row];
        hide_launcher();
        if (res.action) {
            res.action();
        }
    }
}

bool LauncherWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == search_input_ && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Down) {
            int next_row = std::min(result_list_->currentRow() + 1, result_list_->count() - 1);
            result_list_->setCurrentRow(next_row);
            return true;
        } else if (keyEvent->key() == Qt::Key_Up) {
            int prev_row = std::max(result_list_->currentRow() - 1, 0);
            result_list_->setCurrentRow(prev_row);
            return true;
        } else if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            execute_selected();
            return true;
        } else if (keyEvent->key() == Qt::Key_Escape) {
            hide_launcher();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void LauncherWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide_launcher();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void LauncherWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::ActivationChange) {
        if (!isActiveWindow() && isVisible()) {
            hide_launcher();
        }
    }
    QWidget::changeEvent(event);
}

} // namespace nexus::ui

