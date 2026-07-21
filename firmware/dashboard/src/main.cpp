/**
 * @file main.cpp
 * @brief Ninebot G30 Max custom **dashboard firmware** entry point (STM32F103C8,
 *        app @ 0x08001000). The application logic lives in `dash_app.h` (shared
 *        verbatim with the chip simulator, so what is simulated is what ships).
 *
 * On the target the loop runs forever; the 5000 ms IWDG (Req 16) resets the MCU
 * if `DashApp::step()` stops feeding it (a missing/stalled required subsystem).
 */
#include "dash_app.h"

extern "C" int main() {
    dash::DashApp app;
    app.init();
    for (;;) {
        app.step();
    }
}
