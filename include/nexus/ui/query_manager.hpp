#pragma once

#include "nexus/core/search_engine.hpp"
#include "nexus/core/async_search_coordinator.hpp"
#include <QObject>
#include <QString>
#include <memory>
#include <vector>

namespace nexus::ui {

class QueryManager : public QObject {
    Q_OBJECT
public:
    explicit QueryManager(std::shared_ptr<core::SearchEngine> engine, QObject* parent = nullptr);
    ~QueryManager() override = default;

    void search(const QString& text, size_t max_results = 20);
    void cancel_all();

signals:
    void results_ready(std::vector<nexus::core::SearchResult> results, QString query_text = QString());

private:
    core::AsyncSearchCoordinator coordinator_;
};

} // namespace nexus::ui

