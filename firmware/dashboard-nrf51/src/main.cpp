/**
 * @file main.cpp
 * @brief Custom G30 dashboard firmware entry point (nRF51822, app slot above the S110 SoftDevice).
 */
#include "nrf51_hal.h"
#include "tm1637.h"
#include "dashboard.h"

using namespace dash;

/** @brief Vector table entry point. @sideeffects never returns */
extern "C" int main()
{
    static Nrf51Hal hal;
    static Tm1637 display(hal);
    static Dashboard dashboard(hal, display);

    hal.begin();
    dashboard.begin();

    for (;;) {
        dashboard.poll();
    }
}
