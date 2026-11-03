// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Robert Nisipeanu
#include "map_feed.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "log.h"
#include "map_geometry.h"

namespace woc {

const char *const MapFeed::kWaiting = "Waiting for the phone's map — start navigation";

namespace {

/* The size of a target in the renderer's hello ("map:1088x480,panel:..."). */
bool target_size(const std::string &targets, const std::string &name, uint32_t *w, uint32_t *h)
{
    std::stringstream ss(targets);
    std::string t;
    while (std::getline(ss, t, ',')) {
        const size_t colon = t.find(':');
        if (colon == std::string::npos || t.compare(0, colon, name) != 0 || colon != name.size())
            continue;
        return std::sscanf(t.c_str() + colon + 1, "%ux%u", w, h) == 2;
    }
    return false;
}

}  // namespace

MapFeed::MapFeed(const FeedConfig &cfg) : cfg_(cfg), client_("waze-on-cluster", cfg.socket)
{
    client_.on_ready = [this] {
        connected_ = true;
        /* The layer is made again on this connection, so only a refusal on it
         * counts, and no status request is outstanding on it. */
        refused_.clear();
        status_asked_ = false;
        log_info("render: connected to the cluster renderer (%s; %s)", client_.backend.c_str(), client_.targets.c_str());
        if (!layer_)
            make_layer();
        if (!mirror_)
            make_mirror();
    };
    client_.on_layer = [this](uint32_t ref, const std::string &state, const std::string &reason) {
        if (ref && ref == mirror_) {
            if (state != mirror_state_ || reason != mirror_reason_)
                log_info("render: map on the HUD too %s%s%s", state.c_str(), reason.empty() ? "" : " - ",
                         reason.c_str());
            mirror_state_ = state;
            mirror_reason_ = reason;
            return;
        }
        if (ref != layer_)
            return;
        if (state != layer_state_ || reason != layer_reason_)
            log_info("render: map %s%s%s", state.c_str(), reason.empty() ? "" : " - ", reason.c_str());
        layer_state_ = state;
        layer_reason_ = reason;
    };
    client_.on_target = [this](const std::string &t, const std::string &state, const std::string &holder,
                               const std::string &) {
        if (t != cfg_.target)
            return;
        target_state_ = state;
        holder_ = holder;
    };
    client_.on_budget = [this](unsigned cap, unsigned max, const std::string &reason) {
        cap_kb_ = cap;
        max_kb_ = max;
        budget_reason_ = reason;
    };
    client_.on_error = [this](const std::string &op, uint32_t, const std::string &reason) {
        log_error("render: %s refused: %s", op.c_str(), reason.c_str());
        if (op == "layer") {
            refused_ = reason;
        }
    };
    client_.on_status = [this](const std::string &text) {
        report_ = text;
        status_asked_ = false;
    };
}

uint32_t MapFeed::make_video(const std::string &target, const MapRect &rect, bool guide_hold, cr::LayerOptions *lo)
{
    uint32_t tw = 0, th = 0;
    if (!target_size(client_.targets, target, &tw, &th))
        return 0;
    /* The map area: all of the map slot's scene, the centred 1088x480 of a
     * whole-panel one, a fitted centre on the HUD; or the target's rect. */
    crk::QuadRect q = crk::map_quad(tw, th, cfg_.video.w, cfg_.video.h);
    if (rect.set() && rect.fits(tw, th))
        q = crk::QuadRect{rect.x, rect.y, rect.w, rect.h};
    else if (rect.set())
        log_warn("render: %s %u,%u,%u,%u is outside %s (%ux%u); the map area instead",
                 target == "hud" ? "hud_rect" : "map_rect", rect.x, rect.y, rect.w, rect.h, target.c_str(), tw, th);
    *lo = cr::LayerOptions();
    lo->target = target;
    lo->klass = "content";
    lo->contend = true;   /* the map takes the slot back from the stock map */
    lo->guide_hold = guide_hold;
    lo->x = (int)q.x;
    lo->y = (int)q.y;
    lo->w = q.w;
    lo->h = q.h;
    lo->visible = live_ && !frozen_ && picture_ == Picture::Shown;
    const uint32_t ref = client_.video(*lo, cfg_.video);
    log_info("render: map layer on %s (%ux%u), quad %ux%u at %u,%u", target.c_str(), tw, th, q.w, q.h, q.x, q.y);
    return ref;
}

void MapFeed::make_layer()
{
    refused_.clear();
    layer_state_ = "stalled";
    layer_reason_.clear();
    target_state_.clear();
    holder_.clear();
    const bool hud = cfg_.target == "hud";
    layer_ = make_video(cfg_.target, hud ? cfg_.hud_rect : cfg_.rect, cfg_.guide_hold, &lo_);
    if (!layer_) {
        refused_ = "the cluster renderer has no target " + cfg_.target;
        log_error("render: %s", refused_.c_str());
    }
}

void MapFeed::make_mirror()
{
    if (!cfg_.also_hud || cfg_.target == "hud")
        return;
    mirror_state_ = "stalled";
    mirror_reason_.clear();
    mirror_ = make_video("hud", cfg_.hud_rect, false, &mirror_lo_);
    if (!mirror_)
        log_warn("render: the cluster renderer has no target hud; the map is on %s only", cfg_.target.c_str());
}

void MapFeed::remake_mirror()
{
    if (mirror_)
        client_.drop(mirror_);
    mirror_ = 0;
    if (client_.connected())
        make_mirror();
}

bool MapFeed::set_target(const std::string &name)
{
    const std::string t = name == "sail" ? "map" : name;
    uint32_t w, h;
    if (client_.connected() && !target_size(client_.targets, t, &w, &h))
        return false;
    if (t == cfg_.target && layer_)
        return true;
    if (layer_)
        client_.drop(layer_);
    layer_ = 0;
    cfg_.target = t;
    if (client_.connected())
        make_layer();
    remake_mirror();
    return true;
}

void MapFeed::set_hud_rect(const MapRect &rect)
{
    const MapRect &was = cfg_.hud_rect;
    if (was.x == rect.x && was.y == rect.y && was.w == rect.w && was.h == rect.h)
        return;
    cfg_.hud_rect = rect;
    if (cfg_.target == "hud" && layer_) {
        client_.drop(layer_);
        layer_ = 0;
        if (client_.connected())
            make_layer();
    }
    remake_mirror();
}

void MapFeed::set_also_hud(bool on)
{
    if (on == cfg_.also_hud)
        return;
    cfg_.also_hud = on;
    remake_mirror();
}

void MapFeed::set_guide_hold(bool hold)
{
    cfg_.guide_hold = hold;
    if (layer_ && lo_.guide_hold != hold) {
        lo_.guide_hold = hold;
        client_.set(layer_, lo_);
    }
}

void MapFeed::poll(int64_t now_ms)
{
    client_.poll(now_ms);
    if (connected_ && !client_.connected()) {
        connected_ = false;
        log_warn("render: the cluster renderer went away; reconnecting");
    }
    if (client_.connected()) {
        down_since_ms_ = -1;
    } else if (down_since_ms_ < 0) {
        down_since_ms_ = now_ms;
    } else if (!down_long_ && now_ms - down_since_ms_ >= kUnreachableMs) {
        down_long_ = true;
    }
    if (client_.connected() && down_long_) {
        down_long_ = false;
    }
    count_second(now_ms);
}

void MapFeed::count_second(int64_t now_ms)
{
    if (now_ms - second_ms_ < 1000)
        return;
    const int rate = second_ms_ ? (int)((int64_t)frames_ * 1000 / (now_ms - second_ms_)) : 0;
    rate_ = rate;
    frames_ = 0;
    second_ms_ = now_ms;
}

void MapFeed::offer(std::vector<uint8_t> *atlas, uint64_t id, int64_t now_ms)
{
    if (mirror_)
        client_.frame(mirror_, atlas->data(), atlas->size(), id);   /* a copy, before the first takes it */
    if (layer_ && client_.frame(layer_, atlas, id))
        frames_++;
    count_second(now_ms);
}

void MapFeed::set_stream(bool live, bool frozen, Picture picture)
{
    if (live == live_ && frozen == frozen_ && picture == picture_)
        return;
    const char *line = !live                       ? "is not live"
                       : frozen                    ? "froze"
                       : picture == Picture::None  ? "is live, no frame yet"
                       : picture == Picture::Blank ? "is live, its picture blank"
                                                   : "is live, with a picture";
    if (line != stream_line_)   /* frames after the stream ends change only the verdicts */
        log_info("render: CarPlay's cluster stream %s", line);
    stream_line_ = line;
    live_ = live;
    frozen_ = frozen;
    picture_ = picture;
    const bool visible = live && !frozen && picture == Picture::Shown;
    if (layer_ && lo_.visible != visible) {
        lo_.visible = visible;
        client_.set(layer_, lo_);
    }
    if (mirror_ && mirror_lo_.visible != visible) {
        mirror_lo_.visible = visible;
        client_.set(mirror_, mirror_lo_);
    }
}

void MapFeed::request_report()
{
    if (client_.connected() && !status_asked_) {
        client_.request_status();
        status_asked_ = true;
    }
}

bool StatusLog::due(const FeedStatus &s, int64_t now_ms)
{
    const std::string state = s.state + '\n' + s.target;
    if (logged_ && state == state_) {
        if (s.reason == reason_)
            return false;
        if (s.stream_reason || stream_) {
            if (now_ms - stream_ms_ < kStreamMs)
                return false;
            stream_ms_ = now_ms;
        }
    }
    state_ = state;
    reason_ = s.reason;
    stream_ = s.stream_reason;
    logged_ = true;
    return true;
}

FeedStatus MapFeed::status(const std::string &decode, const std::string &stream) const
{
    FeedStatus s;
    s.target = cfg_.target;
    const auto fmt = [](const cr::LayerOptions &lo) {
        return std::to_string(lo.x) + "," + std::to_string(lo.y) + "," + std::to_string(lo.w) + "," +
               std::to_string(lo.h);
    };
    if (layer_)
        s.rect = fmt(lo_);
    s.also_hud = cfg_.also_hud;
    if (layer_ && cfg_.target == "hud")
        s.hud_rect = s.rect;
    else if (mirror_)
        s.hud_rect = fmt(mirror_lo_);
    s.upload_cap_kb = cap_kb_;
    s.upload_reason = budget_reason_;
    s.guide_hold = cfg_.guide_hold;
    auto g = client_.guide_disabled.find(cfg_.target);
    s.guide_disabled = client_.connected() && g != client_.guide_disabled.end() && g->second;
    char cap[96] = "";
    if (cap_kb_ && cap_kb_ < max_kb_)
        std::snprintf(cap, sizeof cap, ", upload cap %u KB/s", cap_kb_);
    if (!refused_.empty()) {
        s.state = "error";
        s.reason = refused_;
        s.page = decode;
    } else if (!client_.connected()) {
        s.state = down_long_ ? "error" : "starting";
        s.reason = down_long_ ? "cluster renderer not reachable" : "waiting for the cluster renderer";
        s.page = decode;
    } else if (frozen_ && live_) {
        s.state = "frozen";
        s.reason = "the phone's cluster stream froze";
        s.page = decode;
    } else if (!live_) {
        s.state = "idle";
        s.page = decode;
    } else if (picture_ != Picture::Shown) {
        s.state = "idle";   /* the layer hidden: the stock map shows */
        s.page = decode;
        s.reason = stream;
        s.stream_reason = !stream.empty();
        if (stream.empty() && picture_ == Picture::Blank)
            s.reason = kWaiting;
    } else if (layer_state_ == "shown") {
        s.state = "showing";
        char p[400];
        std::snprintf(p, sizeof p, "Waze map — %d frames/s (%s%s)", rate_, decode.c_str(), cap);
        s.page = p;
        s.reason = stream;
        s.stream_reason = !stream.empty();
    } else if (layer_state_ == "waiting" || layer_state_ == "suspended") {
        s.state = "starting";
        s.page = decode;
        if (layer_state_ == "suspended")
            s.reason = "under an alert on the cluster";
        else if (holder_ == "stock")
            s.reason = "the stock map holds the slot";
        else if (holder_ == "foreign")
            s.reason = "another app holds the slot";
        else
            s.reason = layer_reason_;
    } else if (layer_state_ == "error") {
        s.state = "error";
        s.reason = layer_reason_;
        s.page = decode;
    } else {
        s.state = "idle";   /* stalled: no frames yet, or none for idle_ms */
        s.page = decode;
        s.reason = stream;
        s.stream_reason = !stream.empty();
    }
    return s;
}

}  // namespace woc
