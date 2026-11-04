#pragma once

#include <string>

constexpr float kEngineFarClipMin = 183.333f;
constexpr float kEngineFarClipMax = 1583.33f;
constexpr float kFarClipMaxKeepsClientCap = 0.0f;
constexpr float kRegionLocalLightsMax = 128.0f;
constexpr float kRegionLocalLightsSliderMax = 96.0f;

constexpr unsigned kVirtualKeyF7 = 0x76;

struct Hotkey
{
    unsigned virtualKey;
    bool ctrl;
    bool shift;
    bool alt;
};

constexpr Hotkey kDefaultOverlayHotkey = {kVirtualKeyF7, true, false, false};

bool ParseHotkey(const char* text, Hotkey& out);
std::string HotkeyName(const Hotkey& key);

enum class DayPhase
{
    Day,
    Night,
};

enum class PhaseChoice
{
    Auto,
    Day,
    Night,
};

constexpr float kDefaultNightStart = 21.0f;
constexpr float kDefaultNightEnd = 6.0f;
constexpr float kDefaultNightFade = 1.0f;

struct Config
{
    bool enable = true;
    bool hooks = true;
    bool lightStay = true;
    bool overlay = true;
    Hotkey overlayKey = kDefaultOverlayHotkey;
    int quality = 2;
    float density = 1.0f;
    float haze = 1.0f;
    float groundFog = 0.6f;
    float farFog = 1.0f;
    int stockFog = 1;
    bool transparentFog = true;
    int dataMode = 1;
    float azerothDensity = 1.0f;
    float outlandDensity = 0.15f;
    float northrendDensity = 0.15f;
    int outlandClassicFog = 1;
    int northrendClassicFog = 1;
    float azerothSkyColor = 0.0f;
    float outlandSkyColor = 0.0f;
    float northrendSkyColor = 0.0f;
    float azerothLocalLights = 1.0f;
    float outlandLocalLights = 8.0f;
    float northrendLocalLights = 8.0f;
    int colorSpace = 1;
    int classicPhase = 0;
    float sunScatter = 1.0f;
    float moonScatter = 0.2f;
    float ambient = 1.0f;
    float exposure = 1.0f;
    float classicExposure = 1.0f;
    bool localLights = true;
    float localLightIntensity = 1.0f;
    float localLightPhase = 0.3f;
    bool interiorAware = true;
    float interiorDensity = 0.15f;
    float interiorFarFog = 1.0f;
    float noiseAmount = 0.15f;
    float noiseScale = 0.025f;
    float noiseWindSpeed = 0.5f;
    bool classicNoise = false;
    float godRays = 0.0f;
    bool glowCompensation = true;
    int foreverGlow = 0;
    float colorGrading = 0.0f;
    float farClipMax = kFarClipMaxKeepsClientCap;
    float maxDistance = 5000.0f;
    float temporal = 0.85f;
    bool underwater = false;
    bool liquidDepth = true;
    bool multisampling = true;
    int debugView = 0;
    bool sunMarker = false;
    int logLevel = 1;
    bool water = true;
    int waterQuality = 2;
    float waterWaves = 1.0f;
    float waterWind = 2.0f;
    float waterFoam = 1.0f;
    float waterReflections = 1.0f;
    float waterSpecular = 1.0f;
    float waterClarity = 1.0f;
    float waterZoneColors = 0.5f;
    float waterRipples = 0.5f;
    bool waterClientSplashes = false;
    int waterDebugView = 0;
    float nightStart = kDefaultNightStart;
    float nightEnd = kDefaultNightEnd;
    float nightFade = kDefaultNightFade;
    unsigned settingsRevision = 0;
};

bool SameFogSettings(const Config& a, const Config& b);
bool SameLiveSettings(const Config& a, const Config& b);
std::string SettingChanges(const Config& before, const Config& after);
bool SameFogHistorySettings(const Config& a, const Config& b);
float NightWeight(float hour, float nightStart, float nightEnd, float fade);
Config BlendDayAndNight(const Config& day, const Config& night, float nightWeight);

constexpr unsigned kNoZone = 0;
constexpr const char* kZoneSettingsFolder = "LuxVelumZones";
constexpr const char* kNightSection = "LuxVelumNight";

class ConfigStore
{
public:
    void Load(const std::string& path);
    bool ReloadIfChanged();
    bool EnterZone(unsigned zone);
    void SetDayFraction(float dayFraction);
    void ChoosePhase(PhaseChoice choice);
    void Override(const Config& config);
    void Apply(const Config& edited);
    void LogSettledEdits();
    bool Save();
    bool SaveForAllZones();
    bool ForgetZoneSettings();
    void Revert();
    bool HasUnsavedChanges() const { return !SameLiveSettings(m_config, m_saved); }
    const Config& Get() const { return m_effective; }
    const Config& Edited() const { return m_config; }
    DayPhase Phase() const { return m_phase; }
    bool PhasePending() const { return m_pendingPhase != m_phase; }
    PhaseChoice ChosenPhase() const { return m_choice; }
    float NightWeightNow() const { return m_nightWeight; }
    float GameHour() const { return m_gameHour; }
    const std::string& Path() const { return m_path; }
    unsigned Zone() const { return m_zone; }
    unsigned PendingZone() const { return m_pendingZone; }
    bool HasZoneSettings() const;
    std::string ZonePath() const { return ZonePath(m_zone); }
    std::string ZonePath(unsigned zone) const;

private:
    void Read();
    Config ReadFile(DayPhase phase) const;
    void ReadKeepingStartupSwitches();
    bool LogChanges(const char* origin);
    bool SaveInto(const std::string& target, const std::string& clearedFrom);
    void RememberStamps();
    bool FollowPhase(DayPhase wanted);
    void SwitchPhase(DayPhase phase);
    void RefreshEffective();

    std::string m_path;
    std::string m_zoneFolder;
    unsigned long long m_stamp = 0;
    unsigned long long m_zoneStamp = 0;
    unsigned m_zone = kNoZone;
    unsigned m_pendingZone = kNoZone;
    DayPhase m_phase = DayPhase::Day;
    DayPhase m_pendingPhase = DayPhase::Day;
    PhaseChoice m_choice = PhaseChoice::Auto;
    float m_nightWeight = 0.0f;
    float m_gameHour = -1.0f;
    unsigned m_revision = 0;
    Config m_config;
    Config m_other;
    Config m_effective;
    Config m_saved;
    Config m_logged;
};

const char* AdoptEarlierSettings(const std::string& directory, const std::string& ini);
ConfigStore& GlobalConfig();
