// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Robert Nisipeanu
/* Where the map goes, as the app last chose it: the target (`target` in the
 * configuration) and where on the HUD (`hud_rect`), unless the app's choice
 * is saved in the place file, which a control start writes
 * ({"cmd":"start","target":...,"rect":...}). A saved value that cannot be
 * used is ignored with a warning, and the configuration's is used. The
 * place file is written whole and renamed into place; one that cannot be
 * written leaves the choice in effect until the service stops, with a
 * warning. */
#ifndef WOC_PLACE_SETTING_H
#define WOC_PLACE_SETTING_H

#include <string>

#include "map_rect.h"

namespace woc {

class PlaceSetting {
public:
    /* target, hud_rect: the configuration's; path: the app's choice. */
    PlaceSetting(const std::string &target, const std::string &hud_rect, const std::string &path);
    const std::string &target() const { return target_; }
    const MapRect &hud_rect() const { return hud_rect_; }
    /* The app's choice: the target (map, sail, panel or hud), and the HUD's
     * rect when rect is not empty ("X,Y,W,H"). False for another target or
     * a rect that does not parse, nothing changed. */
    bool set(const std::string &target, const std::string &rect);
    static std::string format(const MapRect &r);

private:
    void save() const;

    std::string path_, target_;
    MapRect hud_rect_;
};

}  // namespace woc

#endif
