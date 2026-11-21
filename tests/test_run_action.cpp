// run_action: results with `confirm` set run only after a yes; "no" dispatches nothing. Fake provider + fake confirm
// callback, so no dialog is shown and nothing real runs.
#include "nexus/ui/run_action.hpp"
#include <QApplication>
#include <QKeyEvent>
#include <QWidget>
#include <QTimer>
#include <cassert>

using namespace nexus;

namespace {
struct Recorder : core::ISearchProvider {
    int executed{0};
    std::string id() const override { return "rec"; }
    std::string name() const override { return "rec"; }
    std::vector<core::SearchResult> search(const core::Query&, const std::atomic<bool>&) override { return {}; }
    bool on_executed(const core::SearchResult&) override {
        ++executed;
        return true;
    }
};
}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    auto rec = std::make_shared<Recorder>();
    const std::vector<core::SearchProviderPtr> providers{rec};

    core::SearchResult res;
    res.provider_id = "rec";
    res.action_kind = core::ActionKind::Internal;

    int asked = 0;
    bool answer = false;
    const ui::ConfirmFn confirm = [&](const core::SearchResult&) {
        ++asked;
        return answer;
    };

    res.confirm = true;
    assert(!ui::run_action(res, providers, confirm));  // "no": nothing dispatched
    assert(asked == 1 && rec->executed == 0);

    answer = true;
    assert(ui::run_action(res, providers, confirm));  // "yes": dispatched
    assert(asked == 2 && rec->executed == 1);

    res.confirm = false;  // no confirm flag: never asked
    assert(ui::run_action(res, providers, confirm));
    assert(asked == 2 && rec->executed == 2);

    // The question quotes the title; the subtitle (a shell alias's command) is shown below it.
    res.title = "Shut down";
    assert(ui::confirm_question(res) == "Run \"Shut down\"?");
    res.subtitle = "Alias (runs in shell) → >rm -rf ~/build";
    assert(ui::confirm_question(res) == "Run \"Shut down\"?\n\nAlias (runs in shell) → >rm -rf ~/build");

    // Default confirm dialog: a bare Enter (double/held Enter on "Shut down") must answer No.
    QTimer::singleShot(0, [] {
        QWidget* box = QApplication::activeModalWidget();
        assert(box);
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(box, &enter);
    });
    assert(!ui::ask_yes_no(res));
    return 0;
}
