// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Robert Nisipeanu
/* Host test of the place setting (place_setting.h): the configuration's
 * target and HUD rect without a place file; the app's choice kept across a
 * restart and replacing the configuration's; a start without a rect keeping
 * the HUD's; an unknown target or a rect that does not parse refused,
 * nothing changed; saved values that cannot be used ignored; the shipped
 * waze-on-cluster.conf's map target and HUD rect; a place file that cannot
 * be written leaving the choice in effect. */
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "place_setting.h"

using namespace woc;

static int failures;

static void ok(const char *name, bool cond)
{
    std::printf("%-4s %s\n", cond ? "ok" : "FAIL", name);
    if (!cond) failures++;
}

static std::string shipped(const char *key)
{
    std::ifstream conf("waze-on-cluster.conf");
    const std::string prefix = std::string(key) + " = ";
    std::string line, v;
    while (std::getline(conf, line))
        if (line.compare(0, prefix.size(), prefix) == 0)
            v = line.substr(prefix.size());
    return v;
}

int main()
{
    const std::string path = "/tmp/woc-place-test-" + std::to_string(getpid()) + ".conf";
    unlink(path.c_str());
    {
        PlaceSetting p("map", "0,0,800,352", path);
        ok("no place file: the configuration's target and HUD rect",
           p.target() == "map" && PlaceSetting::format(p.hud_rect()) == "0,0,800,352");
        ok("the app's hud with a rect", p.set("hud", "100,20,600,264") && p.target() == "hud" &&
                                            PlaceSetting::format(p.hud_rect()) == "100,20,600,264");
        ok("an unknown target: refused, nothing changed", !p.set("cockpit", "") && p.target() == "hud");
        ok("a rect that does not parse: refused, nothing changed",
           !p.set("map", "1,2,3") && !p.set("hud", "0,0,8,8") && p.target() == "hud" &&
               PlaceSetting::format(p.hud_rect()) == "100,20,600,264");
    }
    {
        PlaceSetting p("map", "0,0,800,352", path);
        ok("after a restart: the app's hud and rect, over the configuration's",
           p.target() == "hud" && PlaceSetting::format(p.hud_rect()) == "100,20,600,264");
        ok("a start without a rect keeps the HUD's", p.set("map", "") && p.target() == "map" &&
                                                         PlaceSetting::format(p.hud_rect()) == "100,20,600,264");
    }
    ok("after a restart: map, the HUD's rect kept", PlaceSetting("hud", "", path).target() == "map" &&
                                                    PlaceSetting::format(PlaceSetting("hud", "", path).hud_rect()) ==
                                                        "100,20,600,264");
    std::ofstream(path) << "target = cockpit\nhud_rect = x\n";
    {
        PlaceSetting p("map", "0,0,800,352", path);
        ok("saved values that cannot be used: the configuration's",
           p.target() == "map" && PlaceSetting::format(p.hud_rect()) == "0,0,800,352");
    }
    std::ofstream(path) << "garbage\n";
    ok("a place file without the settings: the configuration's", PlaceSetting("panel", "", path).target() == "panel" &&
                                                                 !PlaceSetting("panel", "", path).hud_rect().set());
    unlink(path.c_str());
    PlaceSetting s(shipped("target"), shipped("hud_rect"), path);
    ok("shipped: target map, the HUD's fitted centre's top at the map's shape, above BMW's speed row",
       s.target() == "map" && PlaceSetting::format(s.hud_rect()) == "214,140,372,164");
    ok("shipped: the place file under the state folder",
       shipped("place_file") == "/var/opt/waze-on-cluster/state/waze-on-cluster.place.conf");
    PlaceSetting w("map", "", "/nonexistent-dir/place.conf");
    ok("a place file that cannot be written: the choice still in effect", w.set("hud", "0,0,400,176") &&
                                                                         w.target() == "hud" &&
                                                                         w.hud_rect().w == 400);
    std::printf(failures ? "%d FAILED\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
