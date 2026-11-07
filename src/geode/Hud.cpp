#include "Hud.hpp"

#include "State.hpp"

#ifdef PF_STUB_BUILD
#define _spr
#endif

using namespace geode::prelude;

namespace pf {
namespace {

CCLabelBMFont* g_label = nullptr;
CCNode* g_box = nullptr;
float g_sinceRefresh = 0.f;

} // namespace

void attachHud(gd::PlayLayer* layer) {
    if (!layer || g_box) return;

    auto* label = CCLabelBMFont::create("", "chatFont.fnt");
    if (!label) return;
    label->setID("pathfinder-hud-label"_spr);
    label->setAnchorPoint({0.f, 1.f});
    label->setScale(0.45f);
    label->setOpacity(235);

    auto* box = CCNode::create();
    if (!box) return;
    box->setID("pathfinder-hud"_spr);
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

    g_label->setString(fmt::format(
                            "Pathfinder  [{}]\n"
                            "mode {}   scan {} solids / {} hazards\n"
                            "presses {}   replans {}\n"
                            "plan {}f @ {}f   {:.2f} ms\n"
                            "horizon {:.0f}%  [{}]\n"
                            "{}",
                            "ON", g_hud.mode, g_hud.solids, g_hud.hazards, g_hud.presses,
                            g_hud.replans, g_hud.planFrames, g_hud.cursor, g_hud.planMs,
                            g_hud.confidence * 100.0, verdict, g_hud.note)
                            .c_str());
}

} // namespace pf
