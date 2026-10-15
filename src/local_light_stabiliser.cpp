#include "local_light_stabiliser.h"

#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
bool SameLight(const LocalPointLight& a, const LocalPointLight& b)
{
    double distanceSquared = 0.0;
    for (int i = 0; i < 3; ++i)
    {
        const double delta = static_cast<double>(a.position[i]) - b.position[i];
        distanceSquared += delta * delta;
    }
    return a.nativeId == b.nativeId && distanceSquared <= kLocalLightIdentityYards * kLocalLightIdentityYards;
}

double Smoothed(double previous, double current, float dt)
{
    const double alpha = 1.0 - std::exp(-static_cast<double>(dt) / kLocalLightPrioritySmoothingSeconds);
    return previous + (current - previous) * alpha;
}

bool StructuralFailure(const LocalLightInputs& lights)
{
    return lights.candidateCount == 0 && (lights.capture == LocalLightCapture::DamagedTable ||
                                          lights.capture == LocalLightCapture::TableChanged ||
                                          lights.capture == LocalLightCapture::ReadFault);
}

bool PersistentFailure(const LocalLightInputs& lights)
{
    return lights.capture == LocalLightCapture::UnsupportedClient ||
           lights.capture == LocalLightCapture::CameraOutOfRange;
}
}

void LocalLightStabiliser::Reset()
{
    m_count = 0;
    m_heldCount = 0;
    m_rejectedCaptures = 0;
}

void LocalLightStabiliser::Step(const LocalLightCamera& camera, int mapId, uint32_t frame, double seconds,
                                LocalLightInputs& lights)
{
    if (m_started && frame == m_lastFrame)
    {
        Replay(lights);
        return;
    }
    m_lastFrame = frame;
    const bool persistent = PersistentFailure(lights);
    if (persistent || !m_started || mapId != m_mapId || seconds < m_lastSeconds ||
        seconds - m_lastSeconds > kLocalLightResetSeconds)
        Reset();
    const double elapsed = m_started ? std::clamp(seconds - m_lastSeconds, 0.0, double{kLocalLightFadeSeconds}) : 0.0;
    const float dt = static_cast<float>(elapsed);
    if (!m_started)
        m_lastLogSeconds = seconds;
    m_started = true;
    m_mapId = mapId;
    m_lastSeconds = seconds;
    if (persistent)
    {
        lights.pointLightCount = 0;
        m_heldCount = 0;
        return;
    }
    if (HoldOutput(lights))
        return;
    MatchCandidates(camera, lights, dt);
    AdvanceWeights(dt);
    RemoveSpentEntries();
    FillFreeSlots();
    ChallengeWeakestSlot();
    WriteOutput(lights);
    LogActivity(seconds);
}

void LocalLightStabiliser::Replay(LocalLightInputs& lights) const
{
    std::memcpy(lights.pointLights, m_held, sizeof(m_held));
    lights.pointLightCount = m_heldCount;
}

bool LocalLightStabiliser::HoldOutput(LocalLightInputs& lights)
{
    if (!StructuralFailure(lights))
    {
        m_rejectedCaptures = 0;
        return false;
    }
    if (m_rejectedCaptures >= kLocalLightHoldCaptures)
        return false;
    ++m_rejectedCaptures;
    ++m_activity.heldCaptures;
    Replay(lights);
    return true;
}

LocalLightStabiliser::Entry* LocalLightStabiliser::FindEntry(const LocalPointLight& light)
{
    for (uint32_t i = 0; i < m_count; ++i)
        if (SameLight(m_entries[i].light, light))
            return &m_entries[i];
    return nullptr;
}

LocalLightStabiliser::Entry* LocalLightStabiliser::AddEntry(const LocalPointLight& light, double priority)
{
    if (m_count == kLocalLightEntries)
        return nullptr;
    Entry& entry = m_entries[m_count++];
    entry = Entry{};
    entry.light = light;
    entry.priority = priority;
    entry.smoothedPriority = priority;
    return &entry;
}

void LocalLightStabiliser::MatchCandidates(const LocalLightCamera& camera, const LocalLightInputs& lights, float dt)
{
    for (uint32_t i = 0; i < m_count; ++i)
    {
        m_entries[i].present = false;
        m_entries[i].outOfView = false;
    }
    const uint32_t count = std::min(lights.candidateCount, kLocalLightCandidatePool);
    for (uint32_t i = 0; i < count; ++i)
    {
        const LocalPointLight& candidate = lights.candidates[i];
        const double priority = engine::LocalLightPriority(candidate, camera.position);
        const bool inView = PointLightMeetsView(candidate, camera);
        Entry* entry = FindEntry(candidate);
        if (!entry && !inView)
            continue;
        if (!entry)
            entry = AddEntry(candidate, priority);
        if (!entry)
            continue;
        entry->light = candidate;
        entry->priority = priority;
        entry->smoothedPriority = Smoothed(entry->smoothedPriority, priority, dt);
        entry->present = inView;
        entry->outOfView = !inView;
    }
    for (uint32_t i = 0; i < m_count; ++i)
    {
        Entry& entry = m_entries[i];
        if (!entry.present && !PointLightMeetsView(entry.light, camera))
            entry.outOfView = true;
    }
}

void LocalLightStabiliser::AdvanceWeights(float dt)
{
    const float step = dt / kLocalLightFadeSeconds;
    for (uint32_t i = 0; i < m_count; ++i)
    {
        Entry& entry = m_entries[i];
        if (!entry.slotted)
            continue;
        if (entry.outOfView)
        {
            entry.weight = 0.0f;
            continue;
        }
        const bool shining = entry.present && !entry.evicted;
        entry.weight = shining ? std::min(entry.weight + step, 1.0f) : std::max(entry.weight - step, 0.0f);
    }
}

void LocalLightStabiliser::FillFreeSlots()
{
    uint32_t slotted = 0;
    for (uint32_t i = 0; i < m_count; ++i)
        slotted += m_entries[i].slotted ? 1 : 0;
    while (slotted < kMaxLocalPointLights)
    {
        Entry* best = nullptr;
        for (uint32_t i = 0; i < m_count; ++i)
        {
            Entry& entry = m_entries[i];
            if (!entry.slotted && entry.present && (!best || entry.smoothedPriority > best->smoothedPriority))
                best = &entry;
        }
        if (!best)
            return;
        best->slotted = true;
        best->evicted = false;
        best->weight = best->faded ? 0.0f : 1.0f;
        best->challengeCaptures = 0;
        ++slotted;
        ++m_activity.admissions;
    }
}

void LocalLightStabiliser::ChallengeWeakestSlot()
{
    Entry* weakest = nullptr;
    for (uint32_t i = 0; i < m_count; ++i)
    {
        Entry& entry = m_entries[i];
        if (entry.slotted && (entry.evicted || !entry.present))
            return;
        if (entry.slotted && (!weakest || entry.smoothedPriority < weakest->smoothedPriority))
            weakest = &entry;
    }
    if (!weakest)
        return;
    Entry* challenger = nullptr;
    for (uint32_t i = 0; i < m_count; ++i)
    {
        Entry& entry = m_entries[i];
        if (entry.slotted || !entry.present)
            continue;
        if (entry.smoothedPriority > kLocalLightSwapMargin * weakest->smoothedPriority)
            ++entry.challengeCaptures;
        else
            entry.challengeCaptures = 0;
        if (entry.challengeCaptures >= kLocalLightSwapCaptures &&
            (!challenger || entry.smoothedPriority > challenger->smoothedPriority))
            challenger = &entry;
    }
    if (!challenger)
        return;
    weakest->evicted = true;
    challenger->challengeCaptures = 0;
    ++m_activity.evictions;
}

void LocalLightStabiliser::RemoveSpentEntries()
{
    uint32_t kept = 0;
    for (uint32_t i = 0; i < m_count; ++i)
    {
        Entry entry = m_entries[i];
        const bool shining = entry.present && !entry.evicted && !entry.outOfView;
        if (entry.slotted && entry.weight <= 0.0f && !shining)
        {
            if (!entry.outOfView)
                ++m_activity.fadeOuts;
            entry.slotted = false;
            entry.evicted = false;
            entry.faded = true;
            entry.challengeCaptures = 0;
        }
        if (entry.slotted || entry.present)
            m_entries[kept++] = entry;
    }
    m_count = kept;
}

void LocalLightStabiliser::WriteOutput(LocalLightInputs& lights)
{
    uint32_t order[kLocalLightEntries];
    uint32_t count = 0;
    for (uint32_t i = 0; i < m_count && count < kMaxLocalPointLights; ++i)
    {
        if (!m_entries[i].slotted)
            continue;
        uint32_t index = count;
        while (index > 0 && m_entries[i].smoothedPriority > m_entries[order[index - 1]].smoothedPriority)
            --index;
        for (uint32_t j = count; j > index; --j)
            order[j] = order[j - 1];
        order[index] = i;
        ++count;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        const Entry& entry = m_entries[order[i]];
        LocalPointLight light = entry.light;
        for (int channel = 0; channel < 3; ++channel)
            light.uploadedColor[channel] *= entry.weight;
        lights.pointLights[i] = light;
    }
    lights.pointLightCount = count;
    std::memcpy(m_held, lights.pointLights, sizeof(m_held));
    m_heldCount = count;
}

void LocalLightStabiliser::LogActivity(double seconds)
{
    if (seconds - m_lastLogSeconds < kLocalLightActivityLogSeconds)
        return;
    m_lastLogSeconds = seconds;
    const LocalLightActivity& now = m_activity;
    const LocalLightActivity& then = m_loggedActivity;
    if (now.admissions == then.admissions && now.evictions == then.evictions && now.fadeOuts == then.fadeOuts &&
        now.heldCaptures == then.heldCaptures)
        return;
    VF_LOG_DEBUG("local lights: %u admitted, %u evicted, %u faded out, %u captures held since the last line",
                 now.admissions - then.admissions, now.evictions - then.evictions, now.fadeOuts - then.fadeOuts,
                 now.heldCaptures - then.heldCaptures);
    m_loggedActivity = now;
}
