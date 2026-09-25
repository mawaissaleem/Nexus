#include "nexus/ui/query_manager.hpp"
#include <QMetaObject>

namespace nexus::ui {

QueryManager::QueryManager(std::shared_ptr<core::SearchEngine> engine, QObject* parent)
    : QObject(parent), coordinator_(std::move(engine)) {
    qRegisterMetaType<std::vector<nexus::core::SearchResult>>("std::vector<nexus::core::SearchResult>");
}

void QueryManager::search(const QString& text, size_t max_results) {
    core::Query query(text.toStdString());
    coordinator_.search(
        query,
        max_results,
        [this, text](std::vector<core::SearchResult> results, uint64_t /*generation*/) {
            QMetaObject::invokeMethod(
                this,
                [this, results = std::move(results), text]() mutable {
                    emit results_ready(std::move(results), text);
                },
                Qt::QueuedConnection
            );
        }
    );
}

void QueryManager::cancel_all() {
    coordinator_.cancel_all();
}

} // namespace nexus::ui

