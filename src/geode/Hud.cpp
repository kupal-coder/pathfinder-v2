#include "Hud.hpp"

#include "State.hpp"

#include <cstdio>

using namespace geode::prelude;

namespace pf {
namespace {

CCLabelBMFont* g_label = nullptr;
CCNode* g_box = nullptr;
float g_sinceRefresh = 0.f;

} // namespace

void detachHud() {
    // The label is owned by the layer we attached it to, which is being torn
    // down. Drop our pointers without touching the nodes.
    g_label = nullptr;
    g_box = nullptr;
    g_sinceRefresh = 0.f;
}

void attachHud(gd::PlayLayer* layer) {
    if (!layer) return;
    if (g_box) {
        // Re-attach if we were left pointing at a different (dead) layer.
        if (g_box->getParent() == layer) return;
        detachHud();
    }

    auto* label = CCLabelBMFont::create("", "chatFont.fnt");
    if (!label) return;
    label->setID(geode::prelude::Mod::get()->expandSpriteName("pathfinder-hud-label"));
    label->setAnchorPoint({0.f, 1.f});
    label->setScale(0.45f);
    label->setOpacity(235);

    auto* box = CCNode::create();
    if (!box) return;
    box->setID(geode::prelude::Mod::get()->expandSpriteName("pathfinder-hud"));
    box->setPosition({10.f, CCDirector::get()->getWinSize().height - 10.f});
    box->setZOrder(1000);
    box->addChild(label);

    layer->addChild(box);
    g_box = box;
    g_label = label;
}

void hudTick(float dt) {
    g_sinceRefresh += dt;
    if (g_sinceRefresh < 0.1f) return;
    g_sinceRefresh = 0.f;

    if (!g_label) return;
    if (!g_hud.enabled) {
        g_label->setString("");
        return;
    }

    const char* verdict =
        g_hud.deadEnd ? "DEAD END" : (g_hud.confidence > 0.66 ? "SAFE" : "TIGHT");

    // Hand-rolled instead of fmt::format: this runs on the frame path and a
    // formatted std::string is a heap allocation every time.
    char practiceSuffix[192];
    if (g_hud.practiceNote.empty()) {
        practiceSuffix[0] = '\0';
    } else {
        std::snprintf(practiceSuffix, sizeof(practiceSuffix), " | %s", g_hud.practiceNote.c_str());
    }

    char buf[768];
    std::snprintf(buf, sizeof(buf),
                  "Pathfinder  [ON]\n"
                  "mode %s   scan %d solids / %d hazards\n"
                  "presses %d   replans %d\n"
                  "plan %df @ %df   %.2f ms\n"
                  "horizon %.0f%%  [%s]%s\n"
                  "practice %s   deaths %d   rewinds %d\n"
                  "%s%s",
                  g_hud.mode.c_str(), g_hud.solids, g_hud.hazards, g_hud.presses,
                  g_hud.replans, g_hud.planFrames, g_hud.cursor, g_hud.planMs,
                  g_hud.confidence * 100.0, verdict, g_hud.unsupported ? "  UNSUPPORTED" : "",
                  g_hud.practice ? "ON" : "off", g_hud.deaths, g_hud.reverts,
                  g_hud.note.c_str(), practiceSuffix);
    g_label->setString(buf);
}

} // namespace pf
