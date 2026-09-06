#include "nexus/index/database.hpp"
#include <iostream>
#include <cassert>

int main() {
    using namespace nexus::index;

    Database db(":memory:");
    assert(db.open());

    // Save applications
    DesktopEntry app1;
    app1.id = "app1";
    app1.name = "App One";
    app1.exec = "app1 %u";
    app1.categories = {"Utility"};

    DesktopEntry app2;
    app2.id = "app2";
    app2.name = "App Two";
    app2.exec = "app2";
    app2.categories = {"Development"};

    bool saved = db.save_applications({app1, app2});
    assert(saved);

    auto loaded = db.load_applications();
    assert(loaded.size() == 2);

    // Test usage tracking
    assert(db.get_usage_count("app1") == 0);
    assert(db.record_usage("app1", "app"));
    assert(db.record_usage("app1", "one"));
    assert(db.get_usage_count("app1") == 2);
    assert(db.get_usage_count("app2") == 0);

    // Test query history
    assert(db.record_query("app", "app1", "applications"));

    db.close();

    std::cout << "All Database tests passed successfully!\n";
    return 0;
}

