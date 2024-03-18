#include "AntiAliasingApp.hpp"

int main() {
    AntiAliasingApp app;
    if (!app.init()) {
        return -1;
    }
    app.run();
    app.shutdown();
    return 0;
}
