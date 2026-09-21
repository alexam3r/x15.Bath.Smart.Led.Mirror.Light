// Task 6: Protocol implementation (ARCHITECTURE.md sections 5.2, 5.3, 8.1).
#include "Protocol.h"

#include <ArduinoJson.h>

#include <cstring>
#include <strings.h>

#include "Config.h"
#include "effects/EffectRegistry.h"

namespace {

int16_t clampByte(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return static_cast<int16_t>(v);
}

}  // namespace

bool parseSwitch(const uint8_t* payload, size_t len, bool& out) {
    char buf[8];  // longest accepted token is "false" (5 chars) + NUL
    if (len >= sizeof(buf)) return false;
    std::memcpy(buf, payload, len);
    buf[len] = '\0';

    if (strcasecmp(buf, "ON") == 0 || strcasecmp(buf, "1") == 0 || strcasecmp(buf, "TRUE") == 0) {
        out = true;
        return true;
    }
    if (strcasecmp(buf, "OFF") == 0 || strcasecmp(buf, "0") == 0 || strcasecmp(buf, "FALSE") == 0) {
        out = false;
        return true;
    }
    return false;
}

bool parseLight(const uint8_t* payload, size_t len, LightCommand& out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) return false;
    if (!doc.is<JsonObject>()) return false;

    JsonVariant stateV = doc["state"];
    if (stateV.is<const char*>()) {
        const char* s = stateV.as<const char*>();
        if (strcasecmp(s, "ON") == 0) {
            out.state = 1;
        } else if (strcasecmp(s, "OFF") == 0) {
            out.state = 0;
        }
        // else: field ignored, out.state stays at whatever it was (-1 default).
    }

    JsonVariant brightnessV = doc["brightness"];
    if (brightnessV.is<int>()) {
        out.brightness = clampByte(brightnessV.as<int>());
    }

    JsonVariant colorV = doc["color"];
    if (colorV.is<JsonObject>()) {
        JsonObject color = colorV.as<JsonObject>();
        JsonVariant r = color["r"];
        if (r.is<int>()) out.r = clampByte(r.as<int>());
        JsonVariant g = color["g"];
        if (g.is<int>()) out.g = clampByte(g.as<int>());
        JsonVariant b = color["b"];
        if (b.is<int>()) out.b = clampByte(b.as<int>());
    }

    JsonVariant effectV = doc["effect"];
    if (effectV.is<const char*>()) {
        const char* e = effectV.as<const char*>();
        if (std::strcmp(e, "solid") == 0) {
            out.effect = EffectRequest::Solid;
        } else if (std::strcmp(e, "makeup") == 0) {
            out.effect = EffectRequest::Makeup;
        } else if (std::strcmp(e, "random") == 0) {
            out.effect = EffectRequest::Random;
        } else {
            const EffectId id = effectIdFromName(e);
            if (id != EffectId::None) {
                out.effect   = EffectRequest::Temporary;
                out.effectId = id;
            }
            // else: unknown name -> field ignored
        }
    }

    return true;
}

bool toCommand(Route route, const uint8_t* payload, size_t len, Command& out) {
    switch (route) {
        case Route::Light:
            out.type = CommandType::Light;
            return parseLight(payload, len, out.light);
        case Route::Automation:
            out.type = CommandType::Automation;
            return parseSwitch(payload, len, out.flag);
        case Route::Makeup:
            out.type = CommandType::Makeup;
            return parseSwitch(payload, len, out.flag);
        case Route::NightMode:
            out.type = CommandType::NightMode;
            return parseSwitch(payload, len, out.flag);
        case Route::Effect:
            out.type = CommandType::RandomEffect;
            return true;  // any payload, even empty
        case Route::Unknown:
            break;
    }
    return false;
}

size_t buildStateJson(const StateSnapshot& s, char* buf, size_t cap) {
    JsonDocument doc;
    doc["state"] = s.on ? "ON" : "OFF";
    doc["brightness"] = s.brightness;
    doc["color_mode"] = "rgb";

    JsonObject color = doc["color"].to<JsonObject>();
    color["r"] = s.r;
    color["g"] = s.g;
    color["b"] = s.b;

    const char* runningEffect = effectName(s.effect);  // nullptr for None
    doc["effect"] = (runningEffect != nullptr) ? runningEffect : baseModeName(s.base);

    doc["automation"] = s.automation ? "ON" : "OFF";
    doc["night_mode"] = s.nightMode ? "ON" : "OFF";
    doc["fw"] = cfg::FW_VERSION;
    doc["brightness_pct"] = static_cast<int>(s.brightness) * 100 / 255;
    doc["moveDetection"] = s.automation ? "ON" : "OFF";  // legacy, == automation
    doc["makeup"] = (s.base == BaseMode::Makeup) ? "ON" : "OFF";

    if (measureJson(doc) >= cap) return 0;
    return serializeJson(doc, buf, cap);
}
