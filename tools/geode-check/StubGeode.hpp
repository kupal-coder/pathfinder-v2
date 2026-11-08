// Minimal stand-in for the Geode/GD headers, used ONLY to syntax-check the mod's
// Geode glue on a machine without the SDK (`make -C tests geode-check`).
//
// It is not a replacement for building against the real SDK - it declares exactly
// the API surface this mod touches, with the same names, types and signatures as
// Geode 4.x / GD 2.2081. If mod code compiles here, its API usage is consistent;
// final verification is still a real `geode build`.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cocos2d {
struct CCPoint {
    float x = 0.f, y = 0.f;
    CCPoint() = default;
    CCPoint(float a, float b) : x(a), y(b) {}
};
struct CCSize {
    float width = 0.f, height = 0.f;
    CCSize() = default;
    CCSize(float w, float h) : width(w), height(h) {}
};
struct CCRect {
    CCPoint origin;
    CCSize size;
};
struct ccColor3B {
    unsigned char r = 255, g = 255, b = 255;
};
class CCObject {
public:
    virtual ~CCObject() = default;
    void retain() {}
    void release() {}
};
class CCNode : public CCObject {
public:
    static CCNode* create() { return new CCNode(); }
    void setID(char const*) {}
    void setPosition(CCPoint) {}
    void setZOrder(int) {}
    void addChild(CCNode*) {}
    void setAnchorPoint(CCPoint) {}
    void setScale(float) {}
    void setOpacity(unsigned char) {}
    CCPoint getPosition() const { return {}; }
    CCSize getContentSize() const { return {}; }
    CCNode* getParent() const { return nullptr; }
    float getScaleX() const { return 1.f; }
    float getScaleY() const { return 1.f; }
    float getRotation() const { return 0.f; }
};
class CCLabelBMFont : public CCNode {
public:
    static CCLabelBMFont* create(char const*, char const*) { return new CCLabelBMFont(); }
    void setString(char const*) {}
};
class CCDirector {
public:
    static CCDirector* get();
    CCSize getWinSize() const { return {480.f, 320.f}; }
};
class CCArray : public CCObject {
public:
    unsigned int count() const { return 0; }
    CCObject* objectAtIndex(unsigned int) { return nullptr; }
};
} // namespace cocos2d

namespace tulip::hook {
enum class TulipConvention { Default };
}

namespace geode {
template <class T, class B>
struct Modify : B {};

namespace log {
template <class... Args>
void info(std::string_view, Args&&...) {}
template <class... Args>
void warn(std::string_view, Args&&...) {}
template <class... Args>
void error(std::string_view, Args&&...) {}
} // namespace log

inline char const* operator""_spr(char const*, size_t) { return ""; }

struct Mod {
    static Mod* get();
    template <class T>
    T getSettingValue(std::string_view) const {
        return T{};
    }
    // Geode's own node-id helper; the real one also applies the mod-id prefix.
    char const* expandSpriteName(std::string_view name) const { return ""; }
};


template <class T, class Fn>
void listenForSettingChanges(char const*, Fn&&) {}

namespace prelude {
using namespace ::cocos2d;
using namespace ::geode;
} // namespace prelude
} // namespace geode

// ---------------------------------------------------------------- fmt / logging
namespace fmt {
inline std::string format(std::string_view) { return {}; }
template <class... Args>
std::string format(std::string_view, Args&&...) {
    return {};
}
} // namespace fmt

// ---------------------------------------------------------------- GD types
namespace gd {
using namespace cocos2d;

enum class GameObjectType {
    Solid = 0,
    Hazard = 2,
    Decoration = 7,
    Modifier = 20,
    Breakable = 21,
    Slope = 25,
    Collectible = 30,
    CollisionObject = 39,
    EnterEffectObject = 45,
    AnimatedHazard = 47,
    Orb = 100,
    Portal = 101,
};
enum class PlayerButton { Jump = 1, Left = 2, Right = 3 };

class GJGameLevel : public CCNode {};

class GameObject : public CCNode {
public:
    GameObjectType m_objectType = GameObjectType::Solid;
    int m_objectID = 0;
    bool m_isGroupDisabled = false;
    CCRect getObjectRect() const { return {}; }
};

class PlayerObject : public GameObject {
public:
    bool m_isShip = false, m_isBall = false, m_isBird = false, m_isDart = false;
    bool m_isRobot = false, m_isSpider = false, m_isSwing = false;
    bool m_isUpsideDown = false, m_isOnGround = false;
    double m_yVelocity = 0.0;
    float m_playerSpeed = 0.9f;
    float m_speedMultiplier = 1.0f;
    bool m_isPlatformer = false;
    double getCurrentXVelocity() { return 0.0; }
    bool pushButton(PlayerButton) { return true; }
    bool releaseButton(PlayerButton) { return true; }
};

class GJBaseGameLayer : public CCNode {
public:
    PlayerObject* m_player1 = nullptr;
    CCArray* m_objects = nullptr;
    virtual void update(float dt) {}
    virtual void resetLevel() {}
};

class CheckpointObject : public CCObject {};

class PlayLayer : public GJBaseGameLayer {
public:
    CCArray* m_checkpointArray = nullptr;
    bool m_isPracticeMode = false;

    static PlayLayer* get() { return nullptr; }
    virtual bool init(GJGameLevel*, bool, bool) { return true; }
    virtual void onQuit() {}
    virtual void onExit() {}
    virtual void destroyPlayer(PlayerObject*, GameObject*) {}
    virtual CheckpointObject* markCheckpoint() { return nullptr; }
    virtual void removeCheckpoint(bool) {}
    virtual void togglePracticeMode(bool) {}
};
} // namespace gd

using namespace geode::prelude;

#define GEODE_CONCAT_I(a, b) a##b
#define GEODE_CONCAT(a, b) GEODE_CONCAT_I(a, b)
// Same shape as Geode's own macros: an unnamed namespace + static inline object.
#define $on_mod(type)                                                     \
    namespace {                                                          \
    struct GEODE_CONCAT(OnModInit, __LINE__) {                           \
        GEODE_CONCAT(OnModInit, __LINE__)();                             \
    };                                                                   \
    static inline auto GEODE_CONCAT(onModReg, __LINE__) =                \
        (GEODE_CONCAT(OnModInit, __LINE__)(), 0);                        \
    }                                                                    \
    static void GEODE_CONCAT(onModBody, __LINE__)()

#define $modify(NAME, BASE) NAME : public geode::Modify<NAME, BASE>
