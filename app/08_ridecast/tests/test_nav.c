#include <assert.h>
#include <stdio.h>
#include "rc_demo_nav.h"
int64_t fake_uptime;
int main(void) {
    heltec_nav_screen_t nav = {0};
    assert(!rc_demo_nav_latest(&nav));
    assert(rc_demo_nav_receive("NAV RIGHT 250 35 1") == RC_OK);
    assert(rc_demo_nav_latest(&nav));
    assert(nav.turn == HELTEC_TURN_RIGHT && nav.distance_m == 250 &&
           nav.speed_kmh == 35 && nav.gps_valid);
    assert(rc_demo_nav_receive("NAV LEFT 80 24 1") == RC_OK);
    assert(rc_demo_nav_latest(&nav) && nav.turn == HELTEC_TURN_LEFT);
    assert(rc_demo_nav_receive("NAV LEFT 1000 24 1") != RC_OK);
    assert(rc_demo_nav_receive("NAV LEFT 100 241 1") != RC_OK);
    assert(rc_demo_nav_receive("NAV LEFT 80 24 2") != RC_OK);
    assert(rc_demo_nav_receive("NAV LEFT 80 24 1 EXTRA") != RC_OK);
    assert(rc_demo_nav_receive("NAV WRONG 80 24 1") != RC_OK);
    fake_uptime = 9000;
    assert(!rc_demo_nav_latest(&nav));
    rc_demo_nav_clear();
    assert(!rc_demo_nav_latest(&nav));
    puts("PASS navigation parser");
    return 0;
}
