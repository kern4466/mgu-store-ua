// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Robert Nisipeanu
/* Where the map goes, as the app last chose it: the target (`target` in the
 * configuration), where on the HUD (`hud_rect`) and whether a map on the
 * cluster is on the HUD too (`also_hud`), unless the app's choice is saved
 * in the place file, which a control start writes
 * ({"cmd":"start","target":...,"rect":...,"hud":0|1}). A saved value that cannot be
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
    /* target, hud_rect, also_hud: the configuration's; path: the app's choice. */
    PlaceSetting(const std::string &target, const std::string &hud_rect, const std::string &also_hud,
                 const std::string &path);
    const std::string &target() const { return target_; }
    const MapRect &hud_rect() const { return hud_rect_; }
    bool also_hud() const { return also_hud_; }
    /* The app's choice: the target (map, sail, panel or hud), the HUD's
     * rect when rect is not empty ("X,Y,W,H"), and also_hud when also_hud
     * is not empty ("0" or "1"). False for another target, a rect that does
     * not parse or another also_hud, nothing changed. */
    bool set(const std::string &target, const std::string &rect, const std::string &also_hud = std::string());
    static std::string format(const MapRect &r);

private:
    void save() const;

    std::string path_, target_;
    MapRect hud_rect_;
    bool also_hud_ = false;
};

}  // namespace woc

#endif
