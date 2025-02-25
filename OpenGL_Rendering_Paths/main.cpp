#include "RenderingPathsApp.hpp"

int main() {
    RenderingPathsApp app;
    if (!app.init()) {
        return -1;
    }
    app.run();
    app.shutdown();
    return 0;
}
