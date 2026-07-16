#include "PrismaUI_API.h"
#include <keyhandler/keyhandler.h>

// windows.h (pulled in transitively) defines max/min function-like macros
// that shadow std::max/std::min textually — undo that so the DDS decoder
// below can use the real std:: algorithms.
#undef max
#undef min

PRISMA_UI_API::IVPrismaUI1* PrismaUI;
static PrismaView uiView;

// AS path of the RaceMenu UI instance (see docs/BRIDGE-SPEC.md).
static constexpr const char* kPanels = "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance";

// Preset folder, exactly as the original PresetEditor.as uses it.
static constexpr const char* kPresetDir = "Data\\SKSE\\Plugins\\CharGen\\Presets\\";

// ---------------------------------------------------------------------
// Minimal DDS -> BMP thumbnail decoder, no external dependencies. Browsers
// can't display DDS directly, so tint/paint texture hover-previews need a
// format Ultralight can show as-is; BMP needs no compression library.
// Handles uncompressed 32bpp RGBA and BC1/BC2/BC3 (DXT1/3/5) — the formats
// actually used by vanilla + common body/skin mod tint masks. Anything else
// (BC7, DX10 extended header, ...) is reported as "no preview available"
// rather than guessed at. Verified against hand-built synthetic DDS files
// (uncompressed RGBA incl. alpha, DXT1, DXT5) before wiring into the plugin.
// ---------------------------------------------------------------------
namespace DdsPreview
{
#pragma pack(push, 1)
    struct PixelFormat
    {
        std::uint32_t size, flags, fourCC, rgbBitCount;
        std::uint32_t rBitMask, gBitMask, bBitMask, aBitMask;
    };
    struct Header
    {
        std::uint32_t size, flags, height, width, pitchOrLinearSize, depth, mipMapCount;
        std::uint32_t reserved1[11];
        PixelFormat   pf;
        std::uint32_t caps, caps2, caps3, caps4, reserved2;
    };
#pragma pack(pop)

    constexpr std::uint32_t FourCC(const char* c)
    {
        return static_cast<std::uint32_t>(static_cast<std::uint8_t>(c[0])) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c[1])) << 8) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c[2])) << 16) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c[3])) << 24);
    }

    struct Rgba { std::uint8_t r, g, b, a; };

    static void DecodeBC1Block(const std::uint8_t* block, Rgba out[16], bool hasAlpha)
    {
        std::uint16_t c0 = block[0] | (block[1] << 8);
        std::uint16_t c1 = block[2] | (block[3] << 8);
        auto unpack565 = [](std::uint16_t c, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {
            r = static_cast<std::uint8_t>(((c >> 11) & 0x1F) * 255 / 31);
            g = static_cast<std::uint8_t>(((c >> 5) & 0x3F) * 255 / 63);
            b = static_cast<std::uint8_t>((c & 0x1F) * 255 / 31);
        };
        Rgba colors[4];
        unpack565(c0, colors[0].r, colors[0].g, colors[0].b); colors[0].a = 255;
        unpack565(c1, colors[1].r, colors[1].g, colors[1].b); colors[1].a = 255;
        if (c0 > c1 || !hasAlpha) {
            colors[2] = { static_cast<std::uint8_t>((2 * colors[0].r + colors[1].r) / 3),
                static_cast<std::uint8_t>((2 * colors[0].g + colors[1].g) / 3),
                static_cast<std::uint8_t>((2 * colors[0].b + colors[1].b) / 3), 255 };
            colors[3] = { static_cast<std::uint8_t>((colors[0].r + 2 * colors[1].r) / 3),
                static_cast<std::uint8_t>((colors[0].g + 2 * colors[1].g) / 3),
                static_cast<std::uint8_t>((colors[0].b + 2 * colors[1].b) / 3), 255 };
        } else {
            colors[2] = { static_cast<std::uint8_t>((colors[0].r + colors[1].r) / 2),
                static_cast<std::uint8_t>((colors[0].g + colors[1].g) / 2),
                static_cast<std::uint8_t>((colors[0].b + colors[1].b) / 2), 255 };
            colors[3] = { 0, 0, 0, 0 };
        }
        std::uint32_t bits = block[4] | (block[5] << 8) | (block[6] << 16) | (block[7] << 24);
        for (int i = 0; i < 16; ++i) {
            out[i] = colors[(bits >> (i * 2)) & 0x3];
        }
    }

    static void DecodeBC3Block(const std::uint8_t* block, Rgba out[16])
    {
        DecodeBC1Block(block + 8, out, false);
        std::uint8_t a0 = block[0], a1 = block[1];
        std::uint8_t alphas[8];
        alphas[0] = a0; alphas[1] = a1;
        if (a0 > a1) {
            for (int i = 1; i <= 6; ++i) alphas[1 + i] = static_cast<std::uint8_t>(((7 - i) * a0 + i * a1) / 7);
        } else {
            for (int i = 1; i <= 4; ++i) alphas[1 + i] = static_cast<std::uint8_t>(((5 - i) * a0 + i * a1) / 5);
            alphas[6] = 0; alphas[7] = 255;
        }
        std::uint64_t bits = 0;
        for (int i = 0; i < 6; ++i) bits |= static_cast<std::uint64_t>(block[2 + i]) << (8 * i);
        for (int i = 0; i < 16; ++i) {
            out[i].a = alphas[(bits >> (i * 3)) & 0x7];
        }
    }

    // Decodes into RGBA8 (top-down). Picks the smallest available mip level
    // that's still >= 48px so tooltip previews stay small and fast to decode.
    static bool Decode(const std::vector<std::uint8_t>& file, std::vector<Rgba>& outPixels,
        std::uint32_t& outW, std::uint32_t& outH)
    {
        if (file.size() < 128 || file[0] != 'D' || file[1] != 'D' || file[2] != 'S' || file[3] != ' ') {
            return false;
        }
        Header hdr;
        std::memcpy(&hdr, file.data() + 4, sizeof(Header));
        if (hdr.size != 124) {
            return false;
        }

        std::uint32_t mipCount = hdr.mipMapCount ? hdr.mipMapCount : 1;
        const std::uint8_t* data = file.data() + 4 + sizeof(Header);
        const std::uint8_t* end = file.data() + file.size();

        const bool compressed = (hdr.pf.flags & 0x4) != 0;
        static const std::uint32_t kDXT1 = FourCC("DXT1"), kDXT3 = FourCC("DXT3"), kDXT5 = FourCC("DXT5");
        if (compressed && hdr.pf.fourCC != kDXT1 && hdr.pf.fourCC != kDXT3 && hdr.pf.fourCC != kDXT5) {
            return false;  // BC7/DX10/etc - not handled by this minimal decoder
        }
        const bool isRgb = !compressed && (hdr.pf.flags & 0x40) && hdr.pf.rgbBitCount == 32;
        if (!compressed && !isRgb) {
            return false;
        }

        std::uint32_t w = hdr.width, h = hdr.height;
        const std::uint8_t* levelData = data;
        std::uint32_t levelW = w, levelH = h;
        const std::uint8_t* chosen = data;
        for (std::uint32_t mip = 0; mip < mipCount; ++mip) {
            std::uint32_t bw = std::max(1u, (w + 3) / 4), bh = std::max(1u, (h + 3) / 4);
            std::size_t levelBytes = compressed
                ? static_cast<std::size_t>(bw) * bh * (hdr.pf.fourCC == kDXT1 ? 8 : 16)
                : static_cast<std::size_t>(w) * h * 4;
            if (levelData + levelBytes > end) {
                break;  // truncated/corrupt — use whatever level we already have
            }
            chosen = levelData;
            levelW = w; levelH = h;
            if (w <= 48 || h <= 48) {
                break;
            }
            levelData += levelBytes;
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
        }

        outW = levelW;
        outH = levelH;
        outPixels.assign(static_cast<std::size_t>(levelW) * levelH, Rgba{ 0, 0, 0, 255 });

        if (!compressed) {
            const std::uint8_t* p = chosen;
            for (std::uint32_t y = 0; y < levelH; ++y) {
                for (std::uint32_t x = 0; x < levelW; ++x) {
                    if (p + 4 > end) {
                        return true;
                    }
                    outPixels[y * levelW + x] = Rgba{ p[2], p[1], p[0], p[3] };  // BGRA -> RGBA
                    p += 4;
                }
            }
            return true;
        }

        const std::uint32_t blockBytes = (hdr.pf.fourCC == kDXT1) ? 8 : 16;
        std::uint32_t bw = std::max(1u, (levelW + 3) / 4), bh = std::max(1u, (levelH + 3) / 4);
        const std::uint8_t* p = chosen;
        for (std::uint32_t by = 0; by < bh; ++by) {
            for (std::uint32_t bx = 0; bx < bw; ++bx) {
                if (p + blockBytes > end) {
                    return true;
                }
                Rgba block[16];
                if (hdr.pf.fourCC == kDXT1) {
                    DecodeBC1Block(p, block, true);
                } else if (hdr.pf.fourCC == kDXT5) {
                    DecodeBC3Block(p, block);
                } else {
                    // DXT3: explicit 4-bit alpha, not interpolated — close
                    // enough for a thumbnail to treat like flat-alpha BC1.
                    DecodeBC1Block(p + 8, block, false);
                }
                for (std::uint32_t py = 0; py < 4; ++py) {
                    std::uint32_t y = by * 4 + py;
                    if (y >= levelH) {
                        continue;
                    }
                    for (std::uint32_t px = 0; px < 4; ++px) {
                        std::uint32_t x = bx * 4 + px;
                        if (x >= levelW) {
                            continue;
                        }
                        outPixels[y * levelW + x] = block[py * 4 + px];
                    }
                }
                p += blockBytes;
            }
        }
        return true;
    }

    // 24-bit BGR, bottom-up rows padded to 4 bytes — the simplest widely
    // supported BMP variant, no compression codec needed to emit it.
    static std::string EncodeBmp(const std::vector<Rgba>& pixels, std::uint32_t w, std::uint32_t h)
    {
        const std::uint32_t rowBytes = ((w * 3 + 3) / 4) * 4;
        const std::uint32_t pixelDataSize = rowBytes * h;
        const std::uint32_t fileSize = 14 + 40 + pixelDataSize;

        std::string out;
        out.resize(fileSize);
        auto put16 = [&](std::size_t off, std::uint16_t v) { std::memcpy(&out[off], &v, 2); };
        auto put32 = [&](std::size_t off, std::uint32_t v) { std::memcpy(&out[off], &v, 4); };

        out[0] = 'B'; out[1] = 'M';
        put32(2, fileSize);
        put32(6, 0);
        put32(10, 14 + 40);
        put32(14, 40);
        put32(18, w);
        put32(22, h);
        put16(26, 1);
        put16(28, 24);
        put32(30, 0);
        put32(34, pixelDataSize);
        put32(38, 2835); put32(42, 2835);
        put32(46, 0); put32(50, 0);

        for (std::uint32_t y = 0; y < h; ++y) {
            std::uint32_t srcY = h - 1 - y;  // BMP rows are bottom-up
            std::size_t rowOff = 14 + 40 + static_cast<std::size_t>(y) * rowBytes;
            for (std::uint32_t x = 0; x < w; ++x) {
                const Rgba& px = pixels[srcY * w + x];
                std::size_t o = rowOff + x * 3;
                out[o + 0] = static_cast<char>(px.b);
                out[o + 1] = static_cast<char>(px.g);
                out[o + 2] = static_cast<char>(px.r);
            }
        }
        return out;
    }
}

static std::string Base64Encode(const std::string& data)
{
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 3 <= data.size()) {
        std::uint32_t n = (static_cast<std::uint8_t>(data[i]) << 16) |
                           (static_cast<std::uint8_t>(data[i + 1]) << 8) |
                           static_cast<std::uint8_t>(data[i + 2]);
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += table[(n >> 6) & 0x3F];
        out += table[n & 0x3F];
        i += 3;
    }
    const std::size_t rem = data.size() - i;
    if (rem == 1) {
        std::uint32_t n = static_cast<std::uint8_t>(data[i]) << 16;
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += "==";
    } else if (rem == 2) {
        std::uint32_t n = (static_cast<std::uint8_t>(data[i]) << 16) | (static_cast<std::uint8_t>(data[i + 1]) << 8);
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += table[(n >> 6) & 0x3F];
        out += "=";
    }
    return out;
}

static std::string JsEscape(const std::string& input)
{
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': break;
        default:
            if (static_cast<unsigned char>(c) >= 0x20 || c < 0) {
                out += c;
            }
        }
    }
    return out;
}

static void InvokeJs(const std::string& js)
{
    if (PrismaUI && PrismaUI->IsValid(uiView)) {
        PrismaUI->Invoke(uiView, js.c_str());
    }
}

// BodySlide is launched through MO2 too, so writing under Data is redirected
// by USVFS into MO2's Overwrite directory. Keep the filename conservative and
// never accept a path supplied by the web view.
static bool IsSafePresetName(const std::string& name)
{
    if (name.empty() || name.size() > 80 || name.find("..") != std::string::npos) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == ' ' || c == '_' || c == '-' || c == '(' || c == ')' || c == '.';
    });
}

static bool IsSafeSliderName(const std::string& name)
{
    return !name.empty() && name.size() <= 128 && std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-' || c == ' ';
    });
}

static std::string XmlEscape(const std::string& input)
{
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '\"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out += c; break;
        }
    }
    return out;
}

static void SaveBodySlidePreset(const std::string& payload)
{
    const auto first = payload.find('|');
    const auto second = payload.find('|', first == std::string::npos ? first : first + 1);
    if (first == std::string::npos || second == std::string::npos) {
        InvokeJs("rmpBodySlidePresetResult('invalid export request')");
        return;
    }
    const auto kind = payload.substr(0, first);
    const auto name = payload.substr(first + 1, second - first - 1);
    if (!IsSafePresetName(name) || (kind != "cbbe" && kind != "himbo")) {
        InvokeJs("rmpBodySlidePresetResult('invalid preset name')");
        return;
    }

    std::vector<std::pair<std::string, double>> sliders;
    std::size_t at = second + 1;
    while (at < payload.size() && sliders.size() < 600) {
        const auto end = payload.find(';', at);
        const auto token = payload.substr(at, end == std::string::npos ? std::string::npos : end - at);
        const auto equals = token.rfind('=');
        if (equals != std::string::npos) {
            const auto sliderName = token.substr(0, equals);
            const auto valueText = token.substr(equals + 1);
            char* parsedEnd = nullptr;
            const double value = std::strtod(valueText.c_str(), &parsedEnd);
            if (IsSafeSliderName(sliderName) && parsedEnd != valueText.c_str() && *parsedEnd == '\0' && std::isfinite(value)) {
                sliders.emplace_back(sliderName, value);
            }
        }
        if (end == std::string::npos) break;
        at = end + 1;
    }
    if (sliders.empty()) {
        InvokeJs("rmpBodySlidePresetResult('no compatible morphs found')");
        return;
    }

    const bool cbbe = kind == "cbbe";
    const std::string flavor = cbbe ? "CBBE 3BA" : "HIMBO";
    const std::string set = cbbe ? "CBBE 3BBB Body Amazing" : "HIMBO";
    const auto outPath = std::filesystem::path("Data\\CalienteTools\\BodySlide\\SliderPresets") /
        ("RaceMenu Prisma - " + name + " [" + flavor + "].xml");
    try {
        std::filesystem::create_directories(outPath.parent_path());
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            InvokeJs("rmpBodySlidePresetResult('could not write preset')");
            return;
        }
        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<SliderPresets>\n";
        out << "  <Preset name=\"" << XmlEscape(name) << "\" set=\"" << set << "\">\n";
        if (cbbe) out << "    <Group name=\"CBBE\"/>\n    <Group name=\"3BBB\"/>\n    <Group name=\"3BA\"/>\n";
        else out << "    <Group name=\"HIMBO\"/>\n";
        for (const auto& [slider, morph] : sliders) {
            // These custom body callbacks use the BodySlide-like zero-based
            // scale: 0 is Zeroed Sliders and 1 is 100%. Deliberately do not
            // clamp: Overdrive values should remain available to BodySlide.
            const double bodySlideValue = morph * 100.0;
            out << "    <SetSlider name=\"" << XmlEscape(slider) << "\" size=\"small\" value=\"" << bodySlideValue << "\"/>\n";
            out << "    <SetSlider name=\"" << XmlEscape(slider) << "\" size=\"big\" value=\"" << bodySlideValue << "\"/>\n";
        }
        out << "  </Preset>\n</SliderPresets>\n";
        out.close();
        logger::info("[bridge] exported {} BodySlide morphs to {}", sliders.size(), outPath.string());
        InvokeJs(std::format(R"(rmpBodySlidePresetResult("saved {} BodySlide preset"))", flavor));
    } catch (const std::exception& e) {
        logger::warn("[bridge] BodySlide export failed: {}", e.what());
        InvokeJs("rmpBodySlidePresetResult('BodySlide export failed')");
    }
}

static double GetNum(const RE::GFxValue& obj, const char* name, double fallback = 0.0)
{
    RE::GFxValue v;
    if (obj.GetMember(name, &v) && v.IsNumber()) {
        return v.GetNumber();
    }
    return fallback;
}

static bool GetBool(const RE::GFxValue& obj, const char* name, bool fallback = false)
{
    RE::GFxValue v;
    if (obj.GetMember(name, &v) && v.IsBool()) {
        return v.GetBool();
    }
    return fallback;
}

static std::string GetStr(const RE::GFxValue& obj, const char* name)
{
    RE::GFxValue v;
    if (obj.GetMember(name, &v) && v.IsString()) {
        return v.GetString();
    }
    return {};
}

// entry.textFilters is an AS array of strings -> "a|b|c"
static std::string GetStrArrayJoined(const RE::GFxValue& obj, const char* name)
{
    RE::GFxValue v;
    std::string out;
    if (obj.GetMember(name, &v) && v.IsArray()) {
        for (std::uint32_t i = 0; i < v.GetArraySize(); ++i) {
            RE::GFxValue el;
            if (v.GetElement(i, &el) && el.IsString()) {
                if (!out.empty()) {
                    out += "|";
                }
                out += el.GetString();
            }
        }
    }
    return out;
}

static void PushPlayerInfo()
{
    std::string name = "(no player)";
    std::string race = "(unknown)";
    std::string sex = "unknown";

    if (auto player = RE::PlayerCharacter::GetSingleton()) {
        if (auto playerName = player->GetName(); playerName && *playerName) {
            name = playerName;
        }
        if (auto playerRace = player->GetRace()) {
            if (auto raceName = playerRace->GetFullName(); raceName && *raceName) {
                race = raceName;
            }
        }
        if (auto base = player->GetActorBase()) {
            switch (base->GetSex()) {
            case RE::SEX::kMale: sex = "male"; break;
            case RE::SEX::kFemale: sex = "female"; break;
            default: break;
            }
        }
    }

    const auto runtime = REL::Module::get().version().string(".");

    InvokeJs(std::format(
        R"(rmpUpdate({{"name":"{}","race":"{}","sex":"{}","runtime":"{}"}}))",
        JsEscape(name), JsEscape(race), sex, runtime));
}

static void PushRaceMenuData();

// Translate a "$KEY" localization key through the game's own Scaleform
// translator (the same table the original SWF's _translator uses), so
// slider/category names render as "Face Part" instead of "$RM_Facepart".
static std::string TranslateUiString(const std::string& text)
{
    if (text.empty() || text[0] != '$') {
        return text;
    }

    static RE::BSScaleformTranslator* translator = []() -> RE::BSScaleformTranslator* {
        auto scaleform = RE::BSScaleformManager::GetSingleton();
        auto loader = scaleform ? scaleform->loader : nullptr;
        if (!loader) {
            return nullptr;
        }
        return static_cast<RE::BSScaleformTranslator*>(loader->GetStateAddRef(RE::GFxState::StateType::kTranslator));
    }();

    if (translator) {
        std::wstring wideKey(text.begin(), text.end());
        auto& map = translator->translator.translationMap;
        auto it = map.find(RE::BSFixedStringW(wideKey.c_str()));
        if (it != map.end()) {
            const wchar_t* value = it->second.c_str();
            if (value) {
                auto utf8 = SKSE::stl::utf16_to_utf8(std::wstring_view(value));
                if (utf8 && !utf8->empty()) {
                    return *utf8;
                }
            }
        }
    }

    // No translation found — strip the "$RM_" / "$" prefix as a fallback.
    if (text.rfind("$RM_", 0) == 0) {
        return text.substr(4);
    }
    return text.substr(1);
}

// Watch the SWF's slider list for as long as the menu stays open, pushing a
// fresh snapshot whenever the entry count settles on a new value. Vanilla
// sliders appear synchronously at open, but skee/CBBE/3BA/HIMBO customs are
// injected by Papyrus seconds later (and a race change rebuilds the whole
// list) — a one-shot poll can never catch all of that.
static std::atomic<int> g_watchGeneration{ 0 };

static void WatchSliderData(int generation, std::uint32_t lastSeen, std::uint32_t lastPushed,
    std::uint32_t lastMakeupSeen, std::uint32_t lastMakeupPushed)
{
    SKSE::GetTaskInterface()->AddUITask([generation, lastSeen, lastPushed, lastMakeupSeen, lastMakeupPushed]() {
        if (generation != g_watchGeneration.load()) {
            return;  // the menu we were watching is gone
        }
        auto ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME)) {
            return;
        }

        std::uint32_t count = 0;
        std::uint32_t makeupTotal = 0;
        if (auto movie = ui->GetMovieView(RE::RaceSexMenu::MENU_NAME)) {
            RE::GFxValue entryList;
            if (movie->GetVariable(&entryList, (std::string(kPanels) + ".racePanel.itemList.entryList").c_str()) && entryList.IsArray()) {
                count = entryList.GetArraySize();
            }
            // Paint texture catalogs (makeupList[0..4]) are filled in by
            // Papyrus via RSM_AddWarpaints/BodyPaints/... independently of
            // the slider list, and not necessarily on the same timeline —
            // tracked separately so the texture pickers aren't left empty.
            RE::GFxValue makeupList;
            if (movie->GetVariable(&makeupList, (std::string(kPanels) + ".makeupList").c_str()) && makeupList.IsArray()) {
                for (std::uint32_t i = 0; i < makeupList.GetArraySize(); ++i) {
                    RE::GFxValue sub;
                    if (makeupList.GetElement(i, &sub) && sub.IsArray()) {
                        makeupTotal += sub.GetArraySize();
                    }
                }
            }
        }

        std::uint32_t pushed = lastPushed;
        std::uint32_t makeupPushed = lastMakeupPushed;
        // Push when either count has been stable across two consecutive
        // reads and differs from what the web view currently has.
        const bool slidersSettled = count > 0 && count == lastSeen && count != lastPushed;
        const bool makeupSettled = makeupTotal > 0 && makeupTotal == lastMakeupSeen && makeupTotal != lastMakeupPushed;
        if (slidersSettled || makeupSettled) {
            logger::info("[bridge] data settled (entries {}->{}, makeup textures {}->{}), pushing",
                lastPushed, count, lastMakeupPushed, makeupTotal);
            PushRaceMenuData();
            PushPlayerInfo();
            pushed = count;
            makeupPushed = makeupTotal;
        }

        std::thread([generation, count, pushed, makeupTotal, makeupPushed]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            WatchSliderData(generation, count, pushed, makeupTotal, makeupPushed);
        }).detach();
    });
}

// Serialize the live slider/category lists out of the (soon hidden) SWF
// and hand them to the web view as JSON.
static void PushRaceMenuData()
{
    auto ui = RE::UI::GetSingleton();
    auto movie = ui ? ui->GetMovieView(RE::RaceSexMenu::MENU_NAME) : nullptr;

    if (!movie) {
        InvokeJs("rmpData(null)");
        return;
    }

    std::string json;
    json.reserve(96 * 1024);
    json += "{\"categories\":[";

    RE::GFxValue catList;
    bool first = true;
    if (movie->GetVariable(&catList, (std::string(kPanels) + ".racePanel.slidingCategoryList.categoryList.entryList").c_str()) && catList.IsArray()) {
        for (std::uint32_t i = 0; i < catList.GetArraySize(); ++i) {
            RE::GFxValue e;
            if (!catList.GetElement(i, &e) || !e.IsObject()) {
                continue;
            }
            if (!first) {
                json += ",";
            }
            first = false;
            json += std::format(R"({{"text":"{}","flag":{},"textFilter":"{}"}})",
                JsEscape(TranslateUiString(GetStr(e, "text"))), GetNum(e, "flag"), JsEscape(GetStr(e, "textFilter")));
        }
    }

    json += "],\"entries\":[";

    RE::GFxValue entryList;
    std::uint32_t count = 0;
    first = true;
    if (movie->GetVariable(&entryList, (std::string(kPanels) + ".racePanel.itemList.entryList").c_str()) && entryList.IsArray()) {
        count = entryList.GetArraySize();
        for (std::uint32_t i = 0; i < count && i < 4000; ++i) {
            RE::GFxValue e;
            if (!entryList.GetElement(i, &e) || !e.IsObject()) {
                continue;
            }
            if (!first) {
                json += ",";
            }
            first = false;
            const auto rawText = GetStr(e, "text");
            // filterFlag: -1 = the SWF entry has NO filterFlag at all, which
            // the original CategoryFilter treats as "matches any category" —
            // this is how most skee-injected sliders (3BA/HIMBO/...) look.
            json += std::format(
                R"({{"text":"{}","rawText":"{}","type":{},"filterFlag":{},"callbackName":"{}","sliderID":{},"min":{},"max":{},"interval":{},"position":{},"enabled":{},"textFilters":"{}","tintType":{},"tintIndex":{},"fillColor":{},"raceID":{},"texture":"{}","listType":{},"glowColor":{}}})",
                JsEscape(TranslateUiString(rawText)),
                JsEscape(rawText),
                GetNum(e, "type", -1),
                GetNum(e, "filterFlag", -1),
                JsEscape(GetStr(e, "callbackName")),
                GetNum(e, "sliderID", -1),
                GetNum(e, "sliderMin"),
                GetNum(e, "sliderMax", 1.0),
                GetNum(e, "interval", 0.1),
                GetNum(e, "position"),
                GetBool(e, "enabled", true) ? "true" : "false",
                JsEscape(GetStrArrayJoined(e, "textFilters")),
                GetNum(e, "tintType", -1),
                GetNum(e, "tintIndex", 0),
                GetNum(e, "fillColor", 0),
                GetNum(e, "raceID", -1),
                JsEscape(GetStr(e, "texture")),
                GetNum(e, "listType", -1),
                GetNum(e, "glowColor", 0));
        }
    }

    // Texture catalogs for the five paint types (RaceMenuDefines PAINT_*:
    // 0 war, 1 body, 2 hand, 3 feet, 4 face) — the "Change texture" choices.
    json += "],\"makeup\":[";
    RE::GFxValue makeupList;
    if (movie->GetVariable(&makeupList, (std::string(kPanels) + ".makeupList").c_str()) && makeupList.IsArray()) {
        for (std::uint32_t i = 0; i < makeupList.GetArraySize(); ++i) {
            if (i) {
                json += ",";
            }
            json += "[";
            RE::GFxValue sub;
            bool firstTex = true;
            if (makeupList.GetElement(i, &sub) && sub.IsArray()) {
                for (std::uint32_t j = 0; j < sub.GetArraySize() && j < 2000; ++j) {
                    RE::GFxValue t;
                    if (!sub.GetElement(j, &t) || !t.IsObject()) {
                        continue;
                    }
                    if (!firstTex) {
                        json += ",";
                    }
                    firstTex = false;
                    json += std::format(R"({{"text":"{}","texture":"{}"}})",
                        JsEscape(GetStr(t, "text")), JsEscape(GetStr(t, "texture")));
                }
            }
            json += "]";
        }
    }

    json += "]}";

    logger::info("[bridge] pushed {} entries ({} bytes) to web view", count, json.size());
    InvokeJs("rmpData(" + json + ")");

    // Drop the same JSON next to the log so slider/category issues can be
    // diagnosed offline from real game data (overwritten on every push).
    if (auto dir = SKSE::log::log_directory()) {
        std::ofstream out(*dir / "RaceMenuPrisma-data.json", std::ios::trunc);
        if (out) {
            out << json;
        }
    }
}

// Show/hide the whole original Flash UI (our web view replaces it).
static bool g_swfHidden = false;

static void SetSwfVisible(bool visible)
{
    auto ui = RE::UI::GetSingleton();
    auto movie = ui ? ui->GetMovieView(RE::RaceSexMenu::MENU_NAME) : nullptr;
    if (!movie) {
        return;
    }
    movie->SetVariable("_root.RaceSexMenuBaseInstance._visible", RE::GFxValue(visible));
    g_swfHidden = !visible;
    logger::info("[bridge] original SWF {}", visible ? "shown" : "hidden");
}

static bool InvokeGameDelegate(const char* callback, const std::vector<RE::GFxValue>& callArgs)
{
    auto ui = RE::UI::GetSingleton();
    auto movie = ui ? ui->GetMovieView(RE::RaceSexMenu::MENU_NAME) : nullptr;
    if (!movie) {
        return false;
    }

    RE::GFxValue args[2];
    args[0] = callback;
    movie->CreateArray(&args[1]);
    for (const auto& a : callArgs) {
        args[1].PushBack(a);
    }

    bool ok = movie->Invoke("_global.gfx.io.GameDelegate.call", nullptr, args, 2);
    if (!ok) {
        ok = movie->Invoke("gfx.io.GameDelegate.call", nullptr, args, 2);
    }
    logger::info("[bridge] GameDelegate.call({}) ok={}", callback, ok);
    return ok;
}

// Replay a slider change into the engine exactly like the original SWF's
// SliderListEntry.as does (slider.changedCallback): GameDelegate.call is the
// ACTUAL apply path for every slider — vanilla and custom (skee/CBBE/3BA/ECE)
// alike — because shared callback names like "ChangeDoubleMorph" are only
// disambiguated by sliderID on the C++ side (see GetRaceMenuSlider). The
// RSM_SliderChange mod event is a secondary notification RaceMenu's Papyrus
// side uses for bookkeeping (e.g. saving tint colors) on select callbacks;
// it is NOT an alternate apply mechanism and never carries sliderID.
static void DispatchSliderChange(const std::string& callback, double sliderId, double value)
{
    InvokeGameDelegate(callback.c_str(), { RE::GFxValue(value), RE::GFxValue(sliderId) });

    SKSE::ModCallbackEvent evn{
        RE::BSFixedString("RSM_SliderChange"),
        RE::BSFixedString(callback),
        static_cast<float>(value),
        nullptr
    };
    SKSE::GetModCallbackEventSource()->SendEvent(&evn);
}

static const char* GFxTypeName(const RE::GFxValue& value)
{
    switch (value.GetType()) {
    case RE::GFxValue::ValueType::kUndefined: return "undefined";
    case RE::GFxValue::ValueType::kNull: return "null";
    case RE::GFxValue::ValueType::kBoolean: return "bool";
    case RE::GFxValue::ValueType::kNumber: return "number";
    case RE::GFxValue::ValueType::kString: return "string";
    case RE::GFxValue::ValueType::kStringW: return "wstring";
    case RE::GFxValue::ValueType::kObject: return "object";
    case RE::GFxValue::ValueType::kArray: return "array";
    case RE::GFxValue::ValueType::kDisplayObject: return "displayObject";
    default: return "?";
    }
}

static void DumpObjectTree(RE::GFxMovieView* movie, const std::string& path, int depth, int maxDepth, int& total)
{
    RE::GFxValue obj;
    if (!movie->GetVariable(&obj, path.c_str()) || (!obj.IsObject() && !obj.IsDisplayObject())) {
        logger::info("[dump] {} : <missing>", path);
        return;
    }

    std::vector<std::string> children;
    obj.VisitMembers([&](const char* name, const RE::GFxValue& val) {
        logger::info("[dump] {}{}.{} : {}", std::string(static_cast<size_t>(depth) * 2, ' '), path, name, GFxTypeName(val));
        ++total;
        if (depth < maxDepth && (val.IsObject() || val.IsDisplayObject()) && std::string_view(name).rfind("__", 0) != 0) {
            children.emplace_back(name);
        }
    });

    for (const auto& name : children) {
        DumpObjectTree(movie, path + "." + name, depth + 1, maxDepth, total);
    }
}

static void DumpRaceMenuInterface()
{
    auto ui = RE::UI::GetSingleton();
    auto movie = ui ? ui->GetMovieView(RE::RaceSexMenu::MENU_NAME) : nullptr;

    if (!movie) {
        logger::info("[dump] RaceSex Menu movie is not loaded — open RaceMenu first");
        InvokeJs("rmpDumpResult('RaceMenu is not open')");
        return;
    }

    int total = 0;
    DumpObjectTree(movie.get(), "_root", 0, 0, total);
    DumpObjectTree(movie.get(), kPanels, 0, 3, total);
    logger::info("[dump] done, {} members total", total);
    InvokeJs(std::format("rmpDumpResult('dumped {} members to log')", total));
}

class MenuWatcher : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
    static MenuWatcher* GetSingleton()
    {
        static MenuWatcher singleton;
        return &singleton;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
    {
        if (!event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (event->menuName == RE::RaceSexMenu::MENU_NAME) {
            logger::info("RaceSex Menu {}", event->opening ? "opened" : "closed");
            InvokeJs(std::format("rmpMenuState({})", event->opening ? "true" : "false"));

            if (event->opening) {
                // Hide the Flash UI and take focus immediately — no need to
                // wait for slider data just to do that. The data itself is
                // polled separately since it's filled in over a few frames.
                SKSE::GetTaskInterface()->AddUITask([]() {
                    SetSwfVisible(false);
                    if (PrismaUI && !PrismaUI->HasFocus(uiView)) {
                        PrismaUI->Focus(uiView);
                        InvokeJs("rmpSetFocus(true)");
                    }
                });
                WatchSliderData(++g_watchGeneration, 0, 0, 0, 0);
            } else {
                ++g_watchGeneration;  // stop the slider-list watcher
                g_swfHidden = false;
                if (PrismaUI && PrismaUI->HasFocus(uiView)) {
                    PrismaUI->Unfocus(uiView);
                    InvokeJs("rmpSetFocus(false)");
                }
            }
        } else if (event->menuName == RE::MessageBoxMenu::MENU_NAME ||
                   event->menuName == RE::Console::MENU_NAME) {
            // Native menus that open on top of RaceMenu (confirmation dialogs,
            // the developer console) need real input — our view holds focus,
            // so yield it while they're up and take it back when they close.
            if (event->opening) {
                logger::info("{} opened over RaceMenu — yielding input focus", event->menuName.c_str());
                if (PrismaUI && PrismaUI->HasFocus(uiView)) {
                    PrismaUI->Unfocus(uiView);
                    InvokeJs("rmpSetFocus(false)");
                }
            } else {
                logger::info("{} closed", event->menuName.c_str());
                if (RE::UI::GetSingleton() && RE::UI::GetSingleton()->IsMenuOpen(RE::RaceSexMenu::MENU_NAME) &&
                    PrismaUI && !PrismaUI->HasFocus(uiView)) {
                    PrismaUI->Focus(uiView);
                    InvokeJs("rmpSetFocus(true)");
                }
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};

static void SKSEMessageHandler(SKSE::MessagingInterface::Message* message)
{
    switch (message->type) {
    case SKSE::MessagingInterface::kDataLoaded:
        PrismaUI = static_cast<PRISMA_UI_API::IVPrismaUI1*>(PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1));

        if (!PrismaUI) {
            logger::critical("PrismaUI API not found — is Prisma UI installed?");
            return;
        }

        uiView = PrismaUI->CreateView("RaceMenuPrisma/index.html", [](PrismaView view) -> void {
            logger::info("RaceMenuPrisma view DOM ready ({})", view);
            PushPlayerInfo();
        });

        PrismaUI->RegisterJSListener(uiView, "rmpPing", [](const char* data) -> void {
            logger::info("Ping from view: {}", data);
            PushPlayerInfo();
        });

        PrismaUI->RegisterJSListener(uiView, "rmpRequestData", [](const char*) -> void {
            SKSE::GetTaskInterface()->AddUITask([]() { PushRaceMenuData(); });
        });

        PrismaUI->RegisterJSListener(uiView, "rmpRequestDump", [](const char*) -> void {
            SKSE::GetTaskInterface()->AddUITask([]() { DumpRaceMenuInterface(); });
        });

        // Done: finalize chargen. Payload = new player name ("" keeps current).
        PrismaUI->RegisterJSListener(uiView, "rmpDone", [](const char* data) -> void {
            std::string name = data ? data : "";
            SKSE::GetTaskInterface()->AddUITask([name]() {
                if (PrismaUI && PrismaUI->HasFocus(uiView)) {
                    PrismaUI->Unfocus(uiView);
                    InvokeJs("rmpSetFocus(false)");
                }
                if (name.empty()) {
                    InvokeGameDelegate("ChangeName", {});
                } else {
                    InvokeGameDelegate("ChangeName", { RE::GFxValue(name.c_str()) });
                }
            });
        });

        // Ask skee what a slider value means (head part name — i.e. which
        // mesh/nif is selected — part count, form id). Payload: "sliderID|value".
        // Reply: rmpSliderInfo({sliderID, partName, parts}).
        PrismaUI->RegisterJSListener(uiView, "rmpGetSliderInfo", [](const char* data) -> void {
            std::string payload = data ? data : "";
            const auto p1 = payload.find('|');
            if (p1 == std::string::npos) {
                return;
            }
            double sliderId = std::atof(payload.substr(0, p1).c_str());
            double value = std::atof(payload.substr(p1 + 1).c_str());

            SKSE::GetTaskInterface()->AddUITask([sliderId, value]() {
                auto ui = RE::UI::GetSingleton();
                auto movie = ui ? ui->GetMovieView(RE::RaceSexMenu::MENU_NAME) : nullptr;
                if (!movie) {
                    return;
                }
                RE::GFxValue result;
                RE::GFxValue invokeArgs[2]{ sliderId, value };
                if (!movie->Invoke("_global.skse.plugins.CharGen.GetSliderData", &result, invokeArgs, 2) || !result.IsObject()) {
                    return;
                }
                const auto partName = GetStr(result, "partName");
                const auto parts = GetNum(result, "parts", 0);
                InvokeJs(std::format(
                    R"(rmpSliderInfo({{"sliderID":{},"partName":"{}","parts":{}}}))",
                    sliderId, JsEscape(partName), parts));
            });
        });

        // Generic RaceMenu mod-event channel. Payload: "RSM_EventName|strArg|numArg".
        // This is how the original UI sends colors, textures and overlay changes.
        PrismaUI->RegisterJSListener(uiView, "rmpModEvent", [](const char* data) -> void {
            std::string payload = data ? data : "";
            const auto p1 = payload.find('|');
            const auto p2 = payload.find('|', p1 == std::string::npos ? p1 : p1 + 1);
            if (p1 == std::string::npos || p2 == std::string::npos || payload.rfind("RSM_", 0) != 0) {
                logger::warn("[bridge] bad modevent payload: {}", payload);
                return;
            }
            std::string eventName = payload.substr(0, p1);
            std::string strArg = payload.substr(p1 + 1, p2 - p1 - 1);
            float numArg = static_cast<float>(std::atof(payload.substr(p2 + 1).c_str()));

            SKSE::GetTaskInterface()->AddUITask([eventName, strArg, numArg]() {
                SKSE::ModCallbackEvent evn{
                    RE::BSFixedString(eventName),
                    RE::BSFixedString(strArg),
                    numArg,
                    nullptr
                };
                SKSE::GetModCallbackEventSource()->SendEvent(&evn);
                logger::info("[bridge] modevent {}(\"{}\", {})", eventName, strArg, numArg);
            });
        });

        // Rotate the player model, like mouse-dragging in the vanilla menu
        // (our view holds input focus, so the engine never sees the mouse).
        // Payload: signed degrees. Rotates the loaded 3D root directly, the
        // way skee's SetPlayerRotation does — changing the game-logic angle
        // (SetHeading) only turned the head because head-tracking compensated.
        PrismaUI->RegisterJSListener(uiView, "rmpRotate", [](const char* data) -> void {
            const float deg = data ? static_cast<float>(std::atof(data)) : 0.0f;
            if (deg == 0.0f) {
                return;
            }
            SKSE::GetTaskInterface()->AddTask([deg]() {
                auto ui = RE::UI::GetSingleton();
                if (!ui || !ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME)) {
                    return;
                }
                auto player = RE::PlayerCharacter::GetSingleton();
                auto root = player ? player->Get3D(false) : nullptr;
                if (!root) {
                    return;
                }
                const float rad = deg * 0.017453292f;
                const float c = std::cos(rad);
                const float s = std::sin(rad);
                RE::NiMatrix3 rz;
                rz.entry[0][0] = c;  rz.entry[0][1] = -s; rz.entry[0][2] = 0.0f;
                rz.entry[1][0] = s;  rz.entry[1][1] = c;  rz.entry[1][2] = 0.0f;
                rz.entry[2][0] = 0.0f; rz.entry[2][1] = 0.0f; rz.entry[2][2] = 1.0f;
                root->local.rotate = root->local.rotate * rz;
                RE::NiUpdateData ctx;
                root->UpdateWorldData(&ctx);
            });
        });

        // Camera zoom toggle, same as the original bottom-bar button:
        // ZoomPC(true) = zoomed to the face, ZoomPC(false) = full body.
        // Payload: "1"/"0".
        PrismaUI->RegisterJSListener(uiView, "rmpZoom", [](const char* data) -> void {
            const bool zoomIn = data && data[0] == '1';
            SKSE::GetTaskInterface()->AddUITask([zoomIn]() {
                InvokeGameDelegate("ZoomPC", { RE::GFxValue(zoomIn) });
            });
        });

        // Tilde in the web view: yield focus and open the developer console
        // (the engine never sees the key while our view is focused).
        PrismaUI->RegisterJSListener(uiView, "rmpConsole", [](const char*) -> void {
            SKSE::GetTaskInterface()->AddUITask([]() {
                if (PrismaUI && PrismaUI->HasFocus(uiView)) {
                    PrismaUI->Unfocus(uiView);
                    InvokeJs("rmpSetFocus(false)");
                }
                if (auto queue = RE::UIMessageQueue::GetSingleton()) {
                    queue->AddMessage(RE::Console::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
                }
            });
        });

        // Undress/dress the player for a clear look at body morphs.
        // Payload: "1" = undress, "0" = re-equip what we removed.
        PrismaUI->RegisterJSListener(uiView, "rmpUndress", [](const char* data) -> void {
            const bool undress = data && data[0] == '1';
            SKSE::GetTaskInterface()->AddTask([undress]() {
                static std::vector<RE::TESBoundObject*> stash;
                auto player = RE::PlayerCharacter::GetSingleton();
                auto equipMgr = RE::ActorEquipManager::GetSingleton();
                if (!player || !equipMgr) {
                    return;
                }
                if (undress) {
                    stash.clear();
                    for (auto& [obj, entry] : player->GetInventory()) {
                        if (obj && obj->IsArmor() && entry.second && entry.second->IsWorn()) {
                            stash.push_back(obj);
                        }
                    }
                    for (auto* obj : stash) {
                        equipMgr->UnequipObject(player, obj, nullptr, 1, nullptr, false, false, false, true);
                    }
                    logger::info("[bridge] undressed {} pieces", stash.size());
                } else {
                    for (auto* obj : stash) {
                        equipMgr->EquipObject(player, obj, nullptr, 1, nullptr, false, false, false, true);
                    }
                    logger::info("[bridge] re-equipped {} pieces", stash.size());
                    stash.clear();
                }
                // The menu pauses the world, so force the biped to rebuild —
                // without this the un/equip is real but invisible until close.
                player->Update3DModel();
            });
        });

        // Play an animation event on the player (poses). Payload = event name,
        // e.g. "IdleWave"; "IdleForceDefaultState" resets to the default idle.
        PrismaUI->RegisterJSListener(uiView, "rmpPose", [](const char* data) -> void {
            std::string eventName = data ? data : "";
            if (eventName.empty()) {
                return;
            }
            SKSE::GetTaskInterface()->AddTask([eventName]() {
                if (auto player = RE::PlayerCharacter::GetSingleton()) {
                    const bool ok = player->NotifyAnimationGraph(eventName);
                    logger::info("[bridge] pose {} ok={}", eventName, ok);
                    InvokeJs(std::format("rmpPoseResult({})", ok ? "true" : "false"));
                }
            });
        });

        // List .jslot/.slot presets via skee's own file browser API.
        // Reply: rmpPresets(["name1.jslot", ...]).
        PrismaUI->RegisterJSListener(uiView, "rmpListPresets", [](const char*) -> void {
            SKSE::GetTaskInterface()->AddUITask([]() {
                auto ui = RE::UI::GetSingleton();
                auto movie = ui ? ui->GetMovieView(RE::RaceSexMenu::MENU_NAME) : nullptr;
                if (!movie) {
                    return;
                }
                RE::GFxValue args[2];
                args[0] = kPresetDir;
                movie->CreateArray(&args[1]);
                args[1].PushBack(RE::GFxValue("*.jslot"));
                args[1].PushBack(RE::GFxValue("*.slot"));

                RE::GFxValue result;
                if (!movie->Invoke("_global.skse.plugins.CharGen.GetExternalFiles", &result, args, 2) || !result.IsArray()) {
                    logger::warn("[bridge] GetExternalFiles failed");
                    InvokeJs("rmpPresets([])");
                    return;
                }
                std::string json = "[";
                bool first = true;
                for (std::uint32_t i = 0; i < result.GetArraySize(); ++i) {
                    RE::GFxValue e;
                    if (!result.GetElement(i, &e) || !e.IsObject() || GetBool(e, "directory")) {
                        continue;
                    }
                    const auto name = GetStr(e, "name");
                    if (name.empty()) {
                        continue;
                    }
                    if (!first) {
                        json += ",";
                    }
                    first = false;
                    json += "\"" + JsEscape(name) + "\"";
                }
                json += "]";
                logger::info("[bridge] presets: {}", json);
                InvokeJs("rmpPresets(" + json + ")");
            });
        });

        // Save the current character as a preset. Payload = file name
        // without extension. Reply: rmpPresetResult("saved"/"save failed").
        PrismaUI->RegisterJSListener(uiView, "rmpSavePreset", [](const char* data) -> void {
            std::string name = data ? data : "";
            if (name.empty()) {
                return;
            }
            SKSE::GetTaskInterface()->AddUITask([name]() {
                auto ui = RE::UI::GetSingleton();
                auto movie = ui ? ui->GetMovieView(RE::RaceSexMenu::MENU_NAME) : nullptr;
                if (!movie) {
                    return;
                }
                const std::string path = std::string(kPresetDir) + name + ".jslot";
                RE::GFxValue result;
                RE::GFxValue args[2]{ path.c_str(), true };
                movie->Invoke("_global.skse.plugins.CharGen.SavePreset", &result, args, 2);
                // Like the SWF: falsy return value = success.
                const bool failed = result.IsBool() ? result.GetBool() : (result.IsNumber() && result.GetNumber() != 0);
                logger::info("[bridge] SavePreset({}) failed={}", path, failed);
                InvokeJs(failed ? "rmpPresetResult('save failed')" : "rmpPresetResult('saved')");
            });
        });

        // Payload = kind|preset name|BodySlideSlider=value;... . The view
        // sends only current custom CBBE/HIMBO morphs; file writing is kept on
        // the native side so MO2 can redirect Data/... into Overwrite.
        PrismaUI->RegisterJSListener(uiView, "rmpSaveBodySlidePreset", [](const char* data) -> void {
            const std::string payload = data ? data : "";
            SKSE::GetTaskInterface()->AddTask([payload]() { SaveBodySlidePreset(payload); });
        });

        // Load a preset by file name (as returned by rmpListPresets). After a
        // successful load, replay the SWF's follow-up (RSM_RequestTintSave)
        // and re-push slider data once the engine has applied everything.
        PrismaUI->RegisterJSListener(uiView, "rmpLoadPreset", [](const char* data) -> void {
            std::string name = data ? data : "";
            if (name.empty()) {
                return;
            }
            SKSE::GetTaskInterface()->AddUITask([name]() {
                auto ui = RE::UI::GetSingleton();
                auto movie = ui ? ui->GetMovieView(RE::RaceSexMenu::MENU_NAME) : nullptr;
                if (!movie) {
                    return;
                }
                const std::string path = std::string(kPresetDir) + name;
                const bool isJslot = path.size() >= 6 && path.compare(path.size() - 6, 6, ".jslot") == 0;
                RE::GFxValue dataOut;
                movie->CreateObject(&dataOut);
                RE::GFxValue result;
                RE::GFxValue args[3]{ path.c_str(), dataOut, isJslot };
                movie->Invoke("_global.skse.plugins.CharGen.LoadPreset", &result, args, 3);
                const bool failed = result.IsBool() ? result.GetBool() : (result.IsNumber() && result.GetNumber() != 0);
                logger::info("[bridge] LoadPreset({}) failed={}", path, failed);
                if (failed) {
                    InvokeJs("rmpPresetResult('load failed')");
                    return;
                }
                // Replay the SWF's full post-load sequence (onLoadPreset +
                // ReloadSliders): skee has applied morphs/head parts, but the
                // tints and hair color are applied by the UI side, and the
                // slider list must be rebuilt from the new character.
                const auto hairColor = static_cast<std::int32_t>(GetNum(dataOut, "hairColor"));
                SKSE::ModCallbackEvent evn{ RE::BSFixedString("RSM_RequestTintSave"), RE::BSFixedString(""), 0.0f, nullptr };
                SKSE::GetModCallbackEventSource()->SendEvent(&evn);
                InvokeJs("rmpPresetResult('loaded')");
                std::thread([hairColor]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    SKSE::GetTaskInterface()->AddUITask([hairColor]() {
                        auto ui2 = RE::UI::GetSingleton();
                        auto movie2 = ui2 ? ui2->GetMovieView(RE::RaceSexMenu::MENU_NAME) : nullptr;
                        if (movie2) {
                            movie2->Invoke("_global.skse.plugins.CharGen.ReloadSliders", nullptr, nullptr, 0);
                        }
                        SKSE::ModCallbackEvent tintLoad{ RE::BSFixedString("RSM_RequestTintLoad"), RE::BSFixedString(""), 0.0f, nullptr };
                        SKSE::GetModCallbackEventSource()->SendEvent(&tintLoad);
                        SKSE::ModCallbackEvent hair{ RE::BSFixedString("RSM_HairColorChange"), RE::BSFixedString(std::to_string(hairColor)), 0.0f, nullptr };
                        SKSE::GetModCallbackEventSource()->SendEvent(&hair);
                    });
                    // Positions change without changing the entry count, so the
                    // stable-count watcher won't fire — push explicitly.
                    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
                    SKSE::GetTaskInterface()->AddUITask([]() {
                        PushRaceMenuData();
                        PushPlayerInfo();
                    });
                }).detach();
            });
        });

        // Read a preset file's raw JSON so the view can show what's inside
        // (mods used, head parts, tints). Reply: rmpPresetInfo(name, <json>).
        PrismaUI->RegisterJSListener(uiView, "rmpReadPresetFile", [](const char* data) -> void {
            std::string name = data ? data : "";
            if (name.empty() || name.find("..") != std::string::npos) {
                return;
            }
            SKSE::GetTaskInterface()->AddTask([name]() {
                std::string content;
                if (auto wide = SKSE::stl::utf8_to_utf16(name)) {
                    auto path = std::filesystem::path(L"Data\\SKSE\\Plugins\\CharGen\\Presets") / *wide;
                    std::ifstream in(path, std::ios::binary);
                    if (in) {
                        content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                    }
                }
                if (content.empty() || content[0] != '{') {
                    InvokeJs(std::format(R"(rmpPresetInfo("{}", null))", JsEscape(name)));
                    return;
                }
                // .jslot is JSON, which is a valid JS literal — pass through.
                InvokeJs(std::format(R"(rmpPresetInfo("{}", {}))", JsEscape(name), content));
            });
        });

        // Check which plugins from a preset are actually installed.
        // Payload: "Mod1.esp|Mod2.esm|...". Reply: rmpModsChecked(["missing", ...]).
        PrismaUI->RegisterJSListener(uiView, "rmpCheckMods", [](const char* data) -> void {
            std::string payload = data ? data : "";
            SKSE::GetTaskInterface()->AddTask([payload]() {
                auto dataHandler = RE::TESDataHandler::GetSingleton();
                if (!dataHandler) {
                    return;
                }
                std::string json = "[";
                bool first = true;
                size_t start = 0;
                while (start <= payload.size()) {
                    const auto end = payload.find('|', start);
                    const auto len = (end == std::string::npos ? payload.size() : end) - start;
                    std::string mod = payload.substr(start, len);
                    if (!mod.empty() && !dataHandler->LookupModByName(mod)) {
                        if (!first) {
                            json += ",";
                        }
                        first = false;
                        json += "\"" + JsEscape(mod) + "\"";
                    }
                    if (end == std::string::npos) {
                        break;
                    }
                    start = end + 1;
                }
                json += "]";
                InvokeJs("rmpModsChecked(" + json + ")");
            });
        });

        // Best-effort DDS -> BMP thumbnail for hover previews of tint/paint
        // textures (SkinTone.dds and similar). Handles uncompressed RGBA and
        // BC1/BC2/BC3; anything else (BC7, DX10 header, ...) replies with a
        // null preview and the tooltip just shows the filename.
        // Payload: texture path as stored on the entry (relative to Data\).
        PrismaUI->RegisterJSListener(uiView, "rmpGetTexturePreview", [](const char* data) -> void {
            std::string relPath = data ? data : "";
            if (relPath.empty() || relPath.find("..") != std::string::npos) {
                return;
            }
            SKSE::GetTaskInterface()->AddTask([relPath]() {
                const auto reject = [&]() {
                    InvokeJs(std::format(R"(rmpTexturePreview("{}", null))", JsEscape(relPath)));
                };

                const std::string fullPath = "Data\\" + relPath;
                RE::BSResourceNiBinaryStream stream(fullPath.c_str());
                if (!stream.good()) {
                    reject();
                    return;
                }
                RE::NiBinaryStream::BufferInfo info{};
                stream.get_info(info);
                const std::uint32_t size = info.totalSize;
                if (size == 0 || size > 32u * 1024u * 1024u) {
                    reject();
                    return;
                }
                std::vector<std::uint8_t> bytes(size);
                if (!stream.read<std::uint8_t>(bytes.data(), size)) {
                    reject();
                    return;
                }

                std::vector<DdsPreview::Rgba> pixels;
                std::uint32_t w = 0, h = 0;
                if (!DdsPreview::Decode(bytes, pixels, w, h) || w == 0 || h == 0) {
                    reject();
                    return;
                }

                const auto bmp = DdsPreview::EncodeBmp(pixels, w, h);
                const auto b64 = Base64Encode(bmp);
                InvokeJs(std::format(R"(rmpTexturePreview("{}", "data:image/bmp;base64,{}"))", JsEscape(relPath), b64));
            });
        });

        // Payload: "callbackName|sliderID|value"
        PrismaUI->RegisterJSListener(uiView, "rmpSliderChange", [](const char* data) -> void {
            std::string payload = data ? data : "";
            const auto p1 = payload.find('|');
            const auto p2 = payload.find('|', p1 == std::string::npos ? p1 : p1 + 1);
            if (p1 == std::string::npos || p2 == std::string::npos) {
                logger::warn("[bridge] bad slider payload: {}", payload);
                return;
            }
            std::string callback = payload.substr(0, p1);
            double sliderId = std::atof(payload.substr(p1 + 1, p2 - p1 - 1).c_str());
            double value = std::atof(payload.substr(p2 + 1).c_str());

            SKSE::GetTaskInterface()->AddUITask([callback, sliderId, value]() {
                DispatchSliderChange(callback, sliderId, value);
            });
        });

        if (auto ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuWatcher::GetSingleton());
        }

        KeyHandler::RegisterSink();

        // F4 toggles UI focus. While RaceMenu is open it also swaps between
        // our web UI and the original Flash UI (escape hatch for sculpt etc.).
        std::ignore = KeyHandler::GetSingleton()->Register(0x3E, KeyEventType::KEY_DOWN, []() {
            const bool menuOpen = RE::UI::GetSingleton() && RE::UI::GetSingleton()->IsMenuOpen(RE::RaceSexMenu::MENU_NAME);
            if (!PrismaUI->HasFocus(uiView)) {
                PrismaUI->Focus(uiView);
                PushPlayerInfo();
                InvokeJs("rmpSetFocus(true)");
                if (menuOpen) {
                    SKSE::GetTaskInterface()->AddUITask([]() { SetSwfVisible(false); });
                }
            } else {
                PrismaUI->Unfocus(uiView);
                InvokeJs("rmpSetFocus(false)");
                if (menuOpen) {
                    SKSE::GetTaskInterface()->AddUITask([]() { SetSwfVisible(true); });
                }
            }
        });

        // F6 dumps the RaceSex Menu Scaleform interface to the log.
        std::ignore = KeyHandler::GetSingleton()->Register(0x40, KeyEventType::KEY_DOWN, []() {
            SKSE::GetTaskInterface()->AddUITask([]() { DumpRaceMenuInterface(); });
        });
        break;
    }
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    REL::Module::reset();

    auto g_messaging = reinterpret_cast<SKSE::MessagingInterface*>(a_skse->QueryInterface(SKSE::LoadInterface::kMessaging));

    if (!g_messaging) {
        logger::critical("Failed to load messaging interface! This error is fatal, plugin will not load.");
        return false;
    }

    SKSE::Init(a_skse);
    SKSE::AllocTrampoline(1 << 10);

    g_messaging->RegisterListener("SKSE", SKSEMessageHandler);

    return true;
}
