// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Robert Nisipeanu
#include "place_setting.h"

#include <fcntl.h>
#include <unistd.h>

#include <fstream>
#include <iterator>

#include "log.h"

namespace woc {

namespace {

const char kTarget[] = "target", kHudRect[] = "hud_rect", kAlsoHud[] = "also_hud";

/* The value of `key = value` in the file's lines; found false without it. */
std::string value_of(const std::string &text, const char *key, bool *found)
{
    const std::string prefix = std::string(key) + " = ";
    std::string v;
    *found = false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos)
            nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        if (line.compare(0, prefix.size(), prefix) == 0) {
            v = line.substr(prefix.size());
            *found = true;
        }
        pos = nl + 1;
    }
    return v;
}

bool known_target(const std::string &t)
{
    return t == "map" || t == "sail" || t == "panel" || t == "hud";
}

}  // namespace

PlaceSetting::PlaceSetting(const std::string &target, const std::string &hud_rect, const std::string &also_hud,
                           const std::string &path)
    : path_(path), target_(target), also_hud_(also_hud == "1")
{
    if (const char *why = parse_map_rect(hud_rect, &hud_rect_))
        log_warn("hud_rect = %s is ignored: %s; the map area instead", hud_rect.c_str(), why);
    if (also_hud != "0" && also_hud != "1")
        log_warn("also_hud = %s is not 0 or 1; the map is on the HUD only when it goes there", also_hud.c_str());
    if (access(path_.c_str(), F_OK) != 0)
        return;
    std::ifstream f(path_);
    if (!f) {
        log_warn("%s: cannot read it: the map goes where the settings say", path_.c_str());
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    bool found;
    const std::string t = value_of(text, kTarget, &found);
    if (found && known_target(t))
        target_ = t;
    else if (found)
        log_warn("%s: target = %s is not map, panel or hud; %s instead", path_.c_str(), t.c_str(), target_.c_str());
    const std::string r = value_of(text, kHudRect, &found);
    MapRect saved;
    if (found && !parse_map_rect(r, &saved))
        hud_rect_ = saved;
    else if (found)
        log_warn("%s: hud_rect = %s does not parse; the settings' instead", path_.c_str(), r.c_str());
    const std::string a = value_of(text, kAlsoHud, &found);
    if (found && (a == "0" || a == "1"))
        also_hud_ = a == "1";
    else if (found)
        log_warn("%s: also_hud = %s is not 0 or 1; the settings' instead", path_.c_str(), a.c_str());
}

bool PlaceSetting::set(const std::string &target, const std::string &rect, const std::string &also_hud)
{
    MapRect r = hud_rect_;
    if (!known_target(target) || (!rect.empty() && parse_map_rect(rect, &r)) ||
        (!also_hud.empty() && also_hud != "0" && also_hud != "1"))
        return false;
    target_ = target;
    hud_rect_ = r;
    if (!also_hud.empty())
        also_hud_ = also_hud == "1";
    save();
    return true;
}

std::string PlaceSetting::format(const MapRect &r)
{
    if (!r.set())
        return "";
    return std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.w) + "," + std::to_string(r.h);
}

void PlaceSetting::save() const
{
    const std::string text = std::string(kTarget) + " = " + target_ + "\n" + kHudRect + " = " + format(hud_rect_) +
                                 "\n" + kAlsoHud + " = " + (also_hud_ ? "1" : "0") + "\n",
                      tmp = path_ + ".tmp";
    const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    const bool whole = fd >= 0 && write(fd, text.data(), text.size()) == (ssize_t)text.size() && fsync(fd) == 0;
    if (fd >= 0)
        close(fd);
    if (!whole || rename(tmp.c_str(), path_.c_str()) != 0) {
        unlink(tmp.c_str());
        log_warn("place: cannot write %s; the map stays on %s until the service stops", path_.c_str(), target_.c_str());
    }
}

}  // namespace woc
