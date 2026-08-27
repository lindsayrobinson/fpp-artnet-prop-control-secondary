#include <fpp-pch.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "Player.h"
#include "Plugin.h"
#include "Sequence.h"
#include "log.h"

// FPP 10 native ChannelDataPlugin - Secondary variant.
//
// Art-Net control layout (mapped consecutively from APCSControlBaseChannel):
//   1  Global master
//   2  Letters dimmer
//   3  Letters red
//   4  Letters green
//   5  Letters blue
//   6  Letters colour mode
//   7-9 spare
//   10 Festoon A dimmer          (pixels 1,3,5,...)
//   11 Festoon A red
//   12 Festoon A green
//   13 Festoon A blue
//   14 Festoon A colour mode
//   15-19 spare
//   20 Festoon B dimmer          (pixels 2,4,6,...)
//   21 Festoon B red
//   22 Festoon B green
//   23 Festoon B blue
//   24 Festoon B colour mode
//
// Colour mode for Letters / Festoon A / Festoon B:
//   0-127   Full source colour: preserve Sequence/Effect RGB while active.
//   128-255 Desk colour override: preserve source intensity/pattern but paint
//           it with the corresponding Art-Net RGB controls.
//
// With no Sequence/Effect active, Art-Net RGB generates a solid colour on the
// pixels assigned to that group.  Per-group brightness is applied next, and
// Channel 1 global Master is ALWAYS the final operation.

class ArtNetPropControlSecondaryPlugin final : public FPPPlugins::Plugin,
                                               public FPPPlugins::ChannelDataPlugin {
public:
    ArtNetPropControlSecondaryPlugin()
        : FPPPlugins::Plugin("fpp-artnet-prop-control-secondary", true) {
        setDefaultSettings();
        applySettings();
        LogInfo(VB_PLUGIN, "Art-Net Prop Control (Secondary) loaded\n");
    }

    ~ArtNetPropControlSecondaryPlugin() override = default;

    // FPP invokes this before legacy Effects / Pixel Overlays are applied.
    // Capture current Art-Net control values and snapshot the source pixel data
    // so modifyChannelData() can detect Effect/overlay activity.
    void modifySequenceData(int /*ms*/, uint8_t* data) override {
        if (data == nullptr || bypass_.load(std::memory_order_relaxed)) {
            return;
        }

        captureControls(data);
        snapshotPreOverlayData(data);
    }

    // FPP invokes this after Effects/overlays and immediately before output
    // processors / physical-network outputs.
    void modifyChannelData(int /*ms*/, uint8_t* data) override {
        if (data == nullptr || bypass_.load(std::memory_order_relaxed)) {
            return;
        }

        // With Bridge Data Priority = Prioritize Bridge, these control channels
        // remain live during playback. Refresh again here so console changes are
        // applied on the current output frame.
        captureControls(data);

        const uint16_t master = master_.load(std::memory_order_relaxed);

        const uint16_t lettersDim  = lettersDim_.load(std::memory_order_relaxed);
        const uint16_t lettersR    = lettersR_.load(std::memory_order_relaxed);
        const uint16_t lettersG    = lettersG_.load(std::memory_order_relaxed);
        const uint16_t lettersB    = lettersB_.load(std::memory_order_relaxed);
        const uint16_t lettersMode = lettersMode_.load(std::memory_order_relaxed);

        const uint16_t festoonADim  = festoonADim_.load(std::memory_order_relaxed);
        const uint16_t festoonAR    = festoonAR_.load(std::memory_order_relaxed);
        const uint16_t festoonAG    = festoonAG_.load(std::memory_order_relaxed);
        const uint16_t festoonAB    = festoonAB_.load(std::memory_order_relaxed);
        const uint16_t festoonAMode = festoonAMode_.load(std::memory_order_relaxed);

        const uint16_t festoonBDim  = festoonBDim_.load(std::memory_order_relaxed);
        const uint16_t festoonBR    = festoonBR_.load(std::memory_order_relaxed);
        const uint16_t festoonBG    = festoonBG_.load(std::memory_order_relaxed);
        const uint16_t festoonBB    = festoonBB_.load(std::memory_order_relaxed);
        const uint16_t festoonBMode = festoonBMode_.load(std::memory_order_relaxed);

        const bool sequencePlaying = Player::INSTANCE.IsPlaying();
        const int64_t now = steadyNowMs();

        const int lettersStart = lettersStartChannel_.load(std::memory_order_relaxed);
        const int lettersPixels = lettersPixels_.load(std::memory_order_relaxed);
        const int festoonStart = festoonStartChannel_.load(std::memory_order_relaxed);
        const int festoonPixels = festoonPixels_.load(std::memory_order_relaxed);

        // Detect Effects/overlays independently for Letters and the two
        // alternating Festoon pixel sets. This allows an Effect to act as the
        // source pattern even though Player::IsPlaying() is false.
        if (lettersOverlayChanged(data, lettersStart, lettersPixels)) {
            lettersOverlayUntilMs_.store(now + kOverlayHoldMs, std::memory_order_relaxed);
        }
        if (festoonOverlayChanged(data, festoonStart, festoonPixels, 0)) {
            festoonAOverlayUntilMs_.store(now + kOverlayHoldMs, std::memory_order_relaxed);
        }
        if (festoonOverlayChanged(data, festoonStart, festoonPixels, 1)) {
            festoonBOverlayUntilMs_.store(now + kOverlayHoldMs, std::memory_order_relaxed);
        }

        const bool lettersPatternActive = sequencePlaying ||
            now <= lettersOverlayUntilMs_.load(std::memory_order_relaxed);
        const bool festoonAPatternActive = sequencePlaying ||
            now <= festoonAOverlayUntilMs_.load(std::memory_order_relaxed);
        const bool festoonBPatternActive = sequencePlaying ||
            now <= festoonBOverlayUntilMs_.load(std::memory_order_relaxed);

        processLetters(data,
                       lettersStart,
                       lettersPixels,
                       lettersColorOrder_.load(std::memory_order_relaxed),
                       lettersR, lettersG, lettersB, lettersMode,
                       lettersDim, master, lettersPatternActive);

        // Festoon A = human pixel numbers 1,3,5,... (zero-based index 0,2,4,...)
        processFestoonAlternating(data,
                                  festoonStart,
                                  festoonPixels,
                                  festoonColorOrder_.load(std::memory_order_relaxed),
                                  0,
                                  festoonAR, festoonAG, festoonAB, festoonAMode,
                                  festoonADim, master, festoonAPatternActive);

        // Festoon B = human pixel numbers 2,4,6,... (zero-based index 1,3,5,...)
        processFestoonAlternating(data,
                                  festoonStart,
                                  festoonPixels,
                                  festoonColorOrder_.load(std::memory_order_relaxed),
                                  1,
                                  festoonBR, festoonBG, festoonBB, festoonBMode,
                                  festoonBDim, master, festoonBPatternActive);
    }

    std::function<bool()> shutdown() override {
        return nullptr;
    }

protected:
    void settingChanged(const std::string& /*key*/, const std::string& /*value*/) override {
        applySettings();
    }

private:
    static constexpr int kMaxChannels = FPPD_MAX_CHANNELS;
    static constexpr int64_t kOverlayHoldMs = 750;

    std::atomic<bool> bypass_{true};
    std::atomic<int> controlBaseChannel_{10001};

    // Latched control values. 255 defaults are neutral/full except colour modes,
    // which default to desk-colour override.
    std::atomic<uint16_t> master_{255};

    std::atomic<uint16_t> lettersDim_{255};
    std::atomic<uint16_t> lettersR_{255};
    std::atomic<uint16_t> lettersG_{255};
    std::atomic<uint16_t> lettersB_{255};
    std::atomic<uint16_t> lettersMode_{255};

    std::atomic<uint16_t> festoonADim_{255};
    std::atomic<uint16_t> festoonAR_{255};
    std::atomic<uint16_t> festoonAG_{255};
    std::atomic<uint16_t> festoonAB_{255};
    std::atomic<uint16_t> festoonAMode_{255};

    std::atomic<uint16_t> festoonBDim_{255};
    std::atomic<uint16_t> festoonBR_{255};
    std::atomic<uint16_t> festoonBG_{255};
    std::atomic<uint16_t> festoonBB_{255};
    std::atomic<uint16_t> festoonBMode_{255};

    std::atomic<int> lettersStartChannel_{6001};
    std::atomic<int> lettersPixels_{149};
    std::atomic<int> lettersColorOrder_{0};

    std::atomic<int> festoonStartChannel_{1};
    std::atomic<int> festoonPixels_{2000};
    std::atomic<int> festoonColorOrder_{0};

    std::mutex snapshotMutex_;
    std::vector<uint8_t> lettersPreOverlay_;
    std::vector<uint8_t> festoonPreOverlay_;
    std::atomic<int64_t> lettersOverlayUntilMs_{0};
    std::atomic<int64_t> festoonAOverlayUntilMs_{0};
    std::atomic<int64_t> festoonBOverlayUntilMs_{0};

    static int64_t steadyNowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    void captureControls(const uint8_t* data) {
        const int base = controlBaseChannel_.load(std::memory_order_relaxed) - 1;

        master_.store(data[base + 0], std::memory_order_relaxed); // 1

        lettersDim_.store(data[base + 1], std::memory_order_relaxed);  // 2
        lettersR_.store(data[base + 2], std::memory_order_relaxed);    // 3
        lettersG_.store(data[base + 3], std::memory_order_relaxed);    // 4
        lettersB_.store(data[base + 4], std::memory_order_relaxed);    // 5
        lettersMode_.store(data[base + 5], std::memory_order_relaxed); // 6

        festoonADim_.store(data[base + 9], std::memory_order_relaxed);   // 10
        festoonAR_.store(data[base + 10], std::memory_order_relaxed);    // 11
        festoonAG_.store(data[base + 11], std::memory_order_relaxed);    // 12
        festoonAB_.store(data[base + 12], std::memory_order_relaxed);    // 13
        festoonAMode_.store(data[base + 13], std::memory_order_relaxed); // 14

        festoonBDim_.store(data[base + 19], std::memory_order_relaxed);   // 20
        festoonBR_.store(data[base + 20], std::memory_order_relaxed);     // 21
        festoonBG_.store(data[base + 21], std::memory_order_relaxed);     // 22
        festoonBB_.store(data[base + 22], std::memory_order_relaxed);     // 23
        festoonBMode_.store(data[base + 23], std::memory_order_relaxed);  // 24
    }

    void snapshotPreOverlayData(const uint8_t* data) {
        const int lettersStart0 = lettersStartChannel_.load(std::memory_order_relaxed) - 1;
        const int lettersChannels = lettersPixels_.load(std::memory_order_relaxed) * 3;
        const int festoonStart0 = festoonStartChannel_.load(std::memory_order_relaxed) - 1;
        const int festoonChannels = festoonPixels_.load(std::memory_order_relaxed) * 3;

        std::lock_guard<std::mutex> lock(snapshotMutex_);

        if (lettersChannels > 0) {
            lettersPreOverlay_.assign(data + lettersStart0,
                                      data + lettersStart0 + lettersChannels);
        } else {
            lettersPreOverlay_.clear();
        }

        if (festoonChannels > 0) {
            festoonPreOverlay_.assign(data + festoonStart0,
                                      data + festoonStart0 + festoonChannels);
        } else {
            festoonPreOverlay_.clear();
        }
    }

    bool lettersOverlayChanged(const uint8_t* data, int startChannel1, int pixelCount) {
        if (pixelCount <= 0) {
            return false;
        }

        const int start0 = startChannel1 - 1;
        const int channelCount = pixelCount * 3;
        std::lock_guard<std::mutex> lock(snapshotMutex_);

        if (static_cast<int>(lettersPreOverlay_.size()) != channelCount) {
            return false;
        }

        return !std::equal(lettersPreOverlay_.begin(),
                           lettersPreOverlay_.end(),
                           data + start0);
    }

    bool festoonOverlayChanged(const uint8_t* data,
                               int startChannel1,
                               int pixelCount,
                               int parity) {
        if (pixelCount <= 0) {
            return false;
        }

        const int start0 = startChannel1 - 1;
        const int channelCount = pixelCount * 3;
        std::lock_guard<std::mutex> lock(snapshotMutex_);

        if (static_cast<int>(festoonPreOverlay_.size()) != channelCount) {
            return false;
        }

        for (int pixel = parity; pixel < pixelCount; pixel += 2) {
            const int rel = pixel * 3;
            if (festoonPreOverlay_[rel]     != data[start0 + rel] ||
                festoonPreOverlay_[rel + 1] != data[start0 + rel + 1] ||
                festoonPreOverlay_[rel + 2] != data[start0 + rel + 2]) {
                return true;
            }
        }

        return false;
    }

    static uint8_t scale8(uint16_t value, uint16_t level) {
        return static_cast<uint8_t>((value * level + 127u) / 255u);
    }

    static std::array<int, 3> colorOffsets(int order) {
        switch (order) {
        case 1: return {0, 2, 1}; // RBG
        case 2: return {1, 0, 2}; // GRB
        case 3: return {2, 0, 1}; // GBR
        case 4: return {1, 2, 0}; // BRG
        case 5: return {2, 1, 0}; // BGR
        case 0:
        default: return {0, 1, 2}; // RGB
        }
    }

    static void calculateOutputPixel(const uint8_t* data,
                                     int ch,
                                     const std::array<int, 3>& offsets,
                                     uint16_t redLevel,
                                     uint16_t greenLevel,
                                     uint16_t blueLevel,
                                     uint16_t colorMode,
                                     uint16_t localDimmer,
                                     uint16_t master,
                                     bool patternActive,
                                     uint8_t& outR,
                                     uint8_t& outG,
                                     uint8_t& outB) {
        uint16_t r;
        uint16_t g;
        uint16_t b;

        if (patternActive && colorMode < 128) {
            // Full source colour: preserve Sequence / Effect RGB.
            r = data[ch + offsets[0]];
            g = data[ch + offsets[1]];
            b = data[ch + offsets[2]];
        } else if (patternActive) {
            // Desk-colour override: preserve source intensity/pattern only.
            const uint16_t sourceR = data[ch + offsets[0]];
            const uint16_t sourceG = data[ch + offsets[1]];
            const uint16_t sourceB = data[ch + offsets[2]];
            const uint16_t patternLevel = std::max({sourceR, sourceG, sourceB});

            r = scale8(redLevel, patternLevel);
            g = scale8(greenLevel, patternLevel);
            b = scale8(blueLevel, patternLevel);
        } else {
            // Idle: desk directly supplies solid colour.
            r = redLevel;
            g = greenLevel;
            b = blueLevel;
        }

        // Local/group dimmer.
        r = scale8(r, localDimmer);
        g = scale8(g, localDimmer);
        b = scale8(b, localDimmer);

        // Global Master is explicitly the FINAL operation.
        outR = scale8(r, master);
        outG = scale8(g, master);
        outB = scale8(b, master);
    }

    static void processLetters(uint8_t* data,
                               int startChannel1,
                               int pixelCount,
                               int colorOrder,
                               uint16_t redLevel,
                               uint16_t greenLevel,
                               uint16_t blueLevel,
                               uint16_t colorMode,
                               uint16_t localDimmer,
                               uint16_t master,
                               bool patternActive) {
        if (pixelCount <= 0) {
            return;
        }

        const int start0 = startChannel1 - 1;
        const auto offsets = colorOffsets(colorOrder);

        for (int pixel = 0; pixel < pixelCount; ++pixel) {
            const int ch = start0 + pixel * 3;
            uint8_t r, g, b;
            calculateOutputPixel(data, ch, offsets,
                                 redLevel, greenLevel, blueLevel, colorMode,
                                 localDimmer, master, patternActive,
                                 r, g, b);
            data[ch + offsets[0]] = r;
            data[ch + offsets[1]] = g;
            data[ch + offsets[2]] = b;
        }
    }

    static void processFestoonAlternating(uint8_t* data,
                                          int startChannel1,
                                          int pixelCount,
                                          int colorOrder,
                                          int parity,
                                          uint16_t redLevel,
                                          uint16_t greenLevel,
                                          uint16_t blueLevel,
                                          uint16_t colorMode,
                                          uint16_t localDimmer,
                                          uint16_t master,
                                          bool patternActive) {
        if (pixelCount <= 0) {
            return;
        }

        const int start0 = startChannel1 - 1;
        const auto offsets = colorOffsets(colorOrder);

        // parity 0 -> pixel numbers 1,3,5,...
        // parity 1 -> pixel numbers 2,4,6,...
        for (int pixel = parity; pixel < pixelCount; pixel += 2) {
            const int ch = start0 + pixel * 3;
            uint8_t r, g, b;
            calculateOutputPixel(data, ch, offsets,
                                 redLevel, greenLevel, blueLevel, colorMode,
                                 localDimmer, master, patternActive,
                                 r, g, b);
            data[ch + offsets[0]] = r;
            data[ch + offsets[1]] = g;
            data[ch + offsets[2]] = b;
        }
    }

    void setDefaultSettings() {
        if (settings.find("APCSBypass") == settings.end()) settings["APCSBypass"] = "1";
        if (settings.find("APCSControlBaseChannel") == settings.end()) settings["APCSControlBaseChannel"] = "10001";

        if (settings.find("APCSLettersStartChannel") == settings.end()) settings["APCSLettersStartChannel"] = "6001";
        if (settings.find("APCSLettersPixels") == settings.end()) settings["APCSLettersPixels"] = "149";
        if (settings.find("APCSLettersColorOrder") == settings.end()) settings["APCSLettersColorOrder"] = "0";

        if (settings.find("APCSFestoonStartChannel") == settings.end()) settings["APCSFestoonStartChannel"] = "1";
        if (settings.find("APCSFestoonPixels") == settings.end()) settings["APCSFestoonPixels"] = "2000";
        if (settings.find("APCSFestoonColorOrder") == settings.end()) settings["APCSFestoonColorOrder"] = "0";
    }

    int settingInt(const std::string& key, int fallback) const {
        const auto it = settings.find(key);
        if (it == settings.end() || it->second.empty()) {
            return fallback;
        }

        try {
            size_t used = 0;
            const long value = std::stol(it->second, &used, 10);
            if (used != it->second.size() || value < INT32_MIN || value > INT32_MAX) {
                return fallback;
            }
            return static_cast<int>(value);
        } catch (...) {
            return fallback;
        }
    }

    bool settingBool(const std::string& key, bool fallback) const {
        const auto it = settings.find(key);
        if (it == settings.end()) {
            return fallback;
        }
        return it->second == "1" || it->second == "true" ||
               it->second == "on" || it->second == "yes";
    }

    static int clampStart(int value) {
        return std::clamp(value, 1, kMaxChannels);
    }

    static int clampPixels(int requested, int startChannel1) {
        const int availableChannels = kMaxChannels - (startChannel1 - 1);
        const int maxPixels = std::max(0, availableChannels / 3);
        return std::clamp(requested, 0, maxPixels);
    }

    void applySettings() {
        bypass_.store(settingBool("APCSBypass", true), std::memory_order_relaxed);

        // Twenty-four consecutive Art-Net/FPP control channels must fit.
        int controlBase = settingInt("APCSControlBaseChannel", 10001);
        controlBase = std::clamp(controlBase, 1, kMaxChannels - 23);
        controlBaseChannel_.store(controlBase, std::memory_order_relaxed);

        const int lettersStart = clampStart(settingInt("APCSLettersStartChannel", 6001));
        const int lettersPixels = clampPixels(settingInt("APCSLettersPixels", 149), lettersStart);
        const int lettersOrder = std::clamp(settingInt("APCSLettersColorOrder", 0), 0, 5);
        lettersStartChannel_.store(lettersStart, std::memory_order_relaxed);
        lettersPixels_.store(lettersPixels, std::memory_order_relaxed);
        lettersColorOrder_.store(lettersOrder, std::memory_order_relaxed);

        const int festoonStart = clampStart(settingInt("APCSFestoonStartChannel", 1));
        const int festoonPixels = clampPixels(settingInt("APCSFestoonPixels", 2000), festoonStart);
        const int festoonOrder = std::clamp(settingInt("APCSFestoonColorOrder", 0), 0, 5);
        festoonStartChannel_.store(festoonStart, std::memory_order_relaxed);
        festoonPixels_.store(festoonPixels, std::memory_order_relaxed);
        festoonColorOrder_.store(festoonOrder, std::memory_order_relaxed);
    }
};

FPP_PLUGIN_SUPPORTS_UNLOAD()

extern "C" FPPPlugins::Plugin* createPlugin() {
    return new ArtNetPropControlSecondaryPlugin();
}
