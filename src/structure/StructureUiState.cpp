// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "structure/StructureUiState.h"

#include "ui/HotkeyFormat.h"

#include <Windows.h>

#include <algorithm>
#include <utility>

namespace lholo::structure::detail {
namespace {

template <class T>
bool updateRelaxed(std::atomic<T>& target, T value) {
    if (target.load(std::memory_order_relaxed) == value) return false;
    target.store(value, std::memory_order_relaxed);
    return true;
}

struct DefaultHotkey {
    unsigned int key;
    unsigned int modifiers;
};

constexpr std::array<DefaultHotkey, input::kHotkeyCount> kDefaultHotkeys{{
    {VK_INSERT, 0},
    {VK_LEFT, lholo::ui::kHotkeyModifierControl},
    {VK_RIGHT,lholo::ui::kHotkeyModifierControl},
    {VK_UP,   lholo::ui::kHotkeyModifierControl},
    {VK_DOWN, lholo::ui::kHotkeyModifierControl},
    {VK_UP,   lholo::ui::kHotkeyModifierShift},
    {VK_DOWN, lholo::ui::kHotkeyModifierShift},
    {VK_UP,   lholo::ui::kHotkeyModifierAlt},
    {VK_DOWN, lholo::ui::kHotkeyModifierAlt},
    {0,       0},
    {0,       0},
    {0,       0},
    {0,       0}, // OpenPlaced: direct routes start unassigned.
    {0,       0}, // OpenFiles
    {0,       0}, // OpenVerification
    {0,       0}, // OpenMaterials
}};

// "Reset all hotkeys" restores this together with the bindings: it is an input
// preference of the same page, and the gesture it turns off cannot be reached
// through any other controls, so the page needs one way back to the default.
constexpr bool kDefaultAltWheelOffsetEnabled = true;

} // namespace

StructureUiState::StructureUiState() {
    for (std::size_t index = 0; index < mHotkeys.size(); ++index) {
        mHotkeys[index].key.store(kDefaultHotkeys[index].key, std::memory_order_relaxed);
        mHotkeys[index].modifiers.store(kDefaultHotkeys[index].modifiers, std::memory_order_relaxed);
    }
}

StructureUiState& StructureUiState::getInstance() {
    static StructureUiState instance;
    return instance;
}

StructureUiState::HotkeyStorage* StructureUiState::hotkeyStorage(std::size_t index) {
    return index < mHotkeys.size() ? &mHotkeys[index] : nullptr;
}

StructureUiState::HotkeyStorage const* StructureUiState::hotkeyStorage(std::size_t index) const {
    return index < mHotkeys.size() ? &mHotkeys[index] : nullptr;
}

bool StructureUiState::guiVisible() const {
    return mGuiVisible.load(std::memory_order_acquire);
}

bool StructureUiState::toggleGuiVisible() {
    auto const opening = !mGuiVisible.load(std::memory_order_acquire);
    setGuiVisible(opening);
    return opening;
}

void StructureUiState::setGuiVisible(bool visible) {
    std::lock_guard lock(mMenuRouteMutex);
    cancelMenuRoutesLocked();
    mGuiVisible.store(visible,std::memory_order_release);
    mUiInteractionBlocked.store(false,std::memory_order_release);
}

void StructureUiState::cancelMenuRoutesLocked() {
    mPendingMenuRoute.reset();
    mMenuRouteGeneration.fetch_add(1,std::memory_order_acq_rel);
    if(mMenuRouteOpeningGui) {
        mGuiVisible.store(false,std::memory_order_release);
        mBlockGameInputUntil.store(GetTickCount64()+180,std::memory_order_release);
    }
    mMenuRouteOpeningGui=false;
}
void StructureUiState::cancelMenuRoutes() {
    std::lock_guard lock(mMenuRouteMutex);cancelMenuRoutesLocked();
}
std::uint64_t StructureUiState::menuRouteGeneration() const { return mMenuRouteGeneration.load(std::memory_order_acquire); }
bool StructureUiState::queueMenuRoute(input::MenuRouteIntent intent) {
    std::lock_guard lock(mMenuRouteMutex);
    if(intent.route<input::MenuRoute::Placed || intent.route>input::MenuRoute::Materials
        || intent.uiGeneration!=menuRouteGeneration() || uiInteractionBlocked() || nativeTextInputBlocked())return false;
    intent.uiGeneration=mMenuRouteGeneration.fetch_add(1,std::memory_order_acq_rel)+1;
    mPendingMenuRoute=intent;
    mMenuRouteOpeningGui=mMenuRouteOpeningGui || !guiVisible();
    mGuiVisible.store(true, std::memory_order_release);
    setOpeningInputBlockFrames(3);
    return true;
}
std::optional<input::MenuRouteIntent> StructureUiState::consumeMenuRoute() {
    std::lock_guard lock(mMenuRouteMutex);auto intent=mPendingMenuRoute;mPendingMenuRoute.reset();return intent;
}
bool StructureUiState::hasPendingMenuRoute() const {
    std::lock_guard lock(mMenuRouteMutex);return mPendingMenuRoute.has_value();
}
bool StructureUiState::applyMenuRouteIfCurrent(input::MenuRouteIntent const& intent,bool currentInteractionBlocked,std::function<void()> const& apply) {
    std::lock_guard lock(mMenuRouteMutex);
    if(intent.uiGeneration!=menuRouteGeneration())return false;
    if(!guiVisible() || currentInteractionBlocked || uiInteractionBlocked() || nativeTextInputBlocked()) {
        cancelMenuRoutesLocked();return false;
    }
    apply();mMenuRouteOpeningGui=false;return true;
}
void StructureUiState::discardMenuRoute(input::MenuRouteIntent const& intent) {
    std::lock_guard lock(mMenuRouteMutex);
    if(intent.uiGeneration==menuRouteGeneration())cancelMenuRoutesLocked();
}
void StructureUiState::setUiInteractionBlocked(bool blocked) {
    std::lock_guard lock(mMenuRouteMutex);
    mUiInteractionBlocked.store(blocked, std::memory_order_release);
    if(blocked)cancelMenuRoutesLocked();
}
bool StructureUiState::uiInteractionBlocked() const {
    return mUiInteractionBlocked.load(std::memory_order_acquire);
}
void StructureUiState::setNativeTextInputFlag(input::NativeTextInputFlag flag, bool blocked) {
    if(blocked) {
        std::lock_guard lock(mMenuRouteMutex);mNativeTextInput.gain(flag);cancelMenuRoutesLocked();
    } else clearNativeTextInputFlagIfCurrent(flag,nativeTextInputToken(flag));
}
std::uint64_t StructureUiState::nativeTextInputToken(input::NativeTextInputFlag flag) const { return mNativeTextInput.token(flag); }
void StructureUiState::clearNativeTextInputFlagIfCurrent(input::NativeTextInputFlag flag,std::uint64_t token) { mNativeTextInput.clearIfCurrent(flag,token); }
void StructureUiState::setNativeTextInputHooksReady(bool ready) {
    mNativeTextInputHooksReady.store(ready, std::memory_order_release);
    if(!ready)cancelMenuRoutes();
}
bool StructureUiState::nativeTextInputBlocked() const {
    return !mNativeTextInputHooksReady.load(std::memory_order_acquire)
        || mNativeTextInput.blocked();
}
bool StructureUiState::nativeTextInputHooksReady() const {
    return mNativeTextInputHooksReady.load(std::memory_order_acquire);
}
void StructureUiState::releaseHotkey(std::size_t index) {
    if(auto* storage=hotkeyStorage(index))storage->held.store(false,std::memory_order_release);
}
std::optional<std::size_t> StructureUiState::firstHotkeyConflict(std::size_t index) const {
    auto const target=inputHotkey(index);
    if (!target.key) return std::nullopt;
    for (std::size_t candidate=0;candidate<input::kHotkeyCount;++candidate) {
        if (candidate==index) continue;
        auto const other=inputHotkey(candidate);
        if (target.key==other.key && target.modifiers==other.modifiers) return candidate;
    }
    return std::nullopt;
}

bool StructureUiState::openingInputBlocked() const {
    return mOpeningInputBlockFrames.load(std::memory_order_acquire) > 0;
}

void StructureUiState::setOpeningInputBlockFrames(int frames) {
    mOpeningInputBlockFrames.store(frames, std::memory_order_release);
}

void StructureUiState::consumeOpeningInputBlockFrame() {
    if (mOpeningInputBlockFrames.load(std::memory_order_acquire) > 0) {
        mOpeningInputBlockFrames.fetch_sub(1, std::memory_order_acq_rel);
    }
}

std::uint64_t StructureUiState::blockGameInputUntil() const {
    return mBlockGameInputUntil.load(std::memory_order_acquire);
}

void StructureUiState::setBlockGameInputUntil(std::uint64_t deadline) {
    mBlockGameInputUntil.store(deadline, std::memory_order_release);
}

HudStateSnapshot StructureUiState::hud() const {
    return {
        mHudEnabled.load(std::memory_order_relaxed),
        mHudShowFileName.load(std::memory_order_relaxed),
        mHudShowLayer.load(std::memory_order_relaxed),
        mHudShowOverallProgress.load(std::memory_order_relaxed),
        mHudShowProgress.load(std::memory_order_relaxed),
        mHudShowWrongState.load(std::memory_order_relaxed),
        mHudShowWrongType.load(std::memory_order_relaxed),
        mHudShowExtraBlocks.load(std::memory_order_relaxed),
        mHudShowProjectedBlockName.load(std::memory_order_relaxed),
        mHudPosition.load(std::memory_order_relaxed),
        mUiScale.load(std::memory_order_relaxed)
    };
}

bool StructureUiState::setUiScale(float scale) {
    return updateRelaxed(mUiScale, scale);
}

bool StructureUiState::applyHud(HudStateSnapshot const& snapshot) {
    bool changed = false;
    changed = updateRelaxed(mHudEnabled, snapshot.enabled) || changed;
    changed = updateRelaxed(mHudShowFileName, snapshot.showFileName) || changed;
    changed = updateRelaxed(mHudShowLayer, snapshot.showLayer) || changed;
    changed = updateRelaxed(mHudShowOverallProgress, snapshot.showOverallProgress) || changed;
    changed = updateRelaxed(mHudShowProgress, snapshot.showProgress) || changed;
    changed = updateRelaxed(mHudShowWrongState, snapshot.showWrongState) || changed;
    changed = updateRelaxed(mHudShowWrongType, snapshot.showWrongType) || changed;
    changed = updateRelaxed(mHudShowExtraBlocks, snapshot.showExtraBlocks) || changed;
    changed = updateRelaxed(
        mHudShowProjectedBlockName, snapshot.showProjectedBlockName
    ) || changed;
    changed = updateRelaxed(mHudPosition, snapshot.position) || changed;
    changed = updateRelaxed(mUiScale, snapshot.uiScale) || changed;
    return changed;
}

HotkeyBindingSnapshot StructureUiState::hotkey(std::size_t index) const {
    auto const* storage = hotkeyStorage(index);
    if (!storage) return {};
    return {
        storage->key.load(std::memory_order_relaxed),
        storage->modifiers.load(std::memory_order_relaxed),
        storage->capturing.load(std::memory_order_acquire)
    };
}

HotkeyBindingSnapshot StructureUiState::inputHotkey(std::size_t index) const {
    auto const* storage = hotkeyStorage(index);
    if (!storage) return {};
    return {
        storage->key.load(std::memory_order_acquire),
        storage->modifiers.load(std::memory_order_acquire),
        storage->capturing.load(std::memory_order_acquire)
    };
}

void StructureUiState::setHotkey(
    std::size_t  index,
    unsigned int key,
    unsigned int modifiers
) {
    if (auto* storage = hotkeyStorage(index)) {
        storage->key.store(key, std::memory_order_relaxed);
        storage->modifiers.store(modifiers, std::memory_order_relaxed);
    }
}

std::optional<std::size_t> StructureUiState::capturingHotkey() const {
    // Only one hotkey can be capturing at a time (beginHotkeyCapture clears the
    // rest), so a plain scan over every slot is sufficient and count-agnostic.
    for (std::size_t index = 0; index < mHotkeys.size(); ++index) {
        if (mHotkeys[index].capturing.load(std::memory_order_acquire)) return index;
    }
    return std::nullopt;
}

void StructureUiState::beginHotkeyCapture(std::size_t index) {
    stopHotkeyCapture();
    if (auto* storage = hotkeyStorage(index)) {
        storage->capturing.store(true, std::memory_order_release);
    }
}

void StructureUiState::stopHotkeyCapture() {
    for (auto& hotkey : mHotkeys) hotkey.capturing.store(false, std::memory_order_release);
}

void StructureUiState::clearHotkey(std::size_t index) {
    if (auto* storage = hotkeyStorage(index)) {
        storage->key.store(0, std::memory_order_release);
        storage->modifiers.store(0, std::memory_order_release);
        storage->capturing.store(false, std::memory_order_release);
    }
}

void StructureUiState::bindCapturedHotkey(
    std::size_t  index,
    unsigned int key,
    unsigned int modifiers
) {
    auto* target = hotkeyStorage(index);
    if (!target) return;
    for (std::size_t current = 0; current < mHotkeys.size(); ++current) {
        if (current == index) continue;
        // Direct shortcuts never silently displace a configured binding.
        if (input::isDirectMenuHotkey(index) || input::isDirectMenuHotkey(current)) continue;
        auto& candidate = mHotkeys[current];
        if (candidate.key.load(std::memory_order_relaxed) == key
            && candidate.modifiers.load(std::memory_order_relaxed) == modifiers) {
            candidate.key.store(0, std::memory_order_relaxed);
            candidate.modifiers.store(0, std::memory_order_relaxed);
        }
    }
    target->key.store(key, std::memory_order_release);
    target->modifiers.store(modifiers, std::memory_order_release);
    stopHotkeyCapture();
}

void StructureUiState::resetHotkeys() {
    for (std::size_t index = 0; index < mHotkeys.size(); ++index) {
        mHotkeys[index].key.store(kDefaultHotkeys[index].key, std::memory_order_relaxed);
        mHotkeys[index].modifiers.store(kDefaultHotkeys[index].modifiers, std::memory_order_relaxed);
    }
    mAltWheelOffsetEnabled.store(kDefaultAltWheelOffsetEnabled, std::memory_order_relaxed);
    stopHotkeyCapture();
}

void StructureUiState::resetHotkey(std::size_t index) {
    auto* storage = hotkeyStorage(index);
    if (!storage) return;
    storage->key.store(kDefaultHotkeys[index].key, std::memory_order_release);
    storage->modifiers.store(kDefaultHotkeys[index].modifiers, std::memory_order_release);
    storage->capturing.store(false, std::memory_order_release);
    storage->held.store(false, std::memory_order_release);
}

void StructureUiState::setControlHeld(bool held) {
    mControlHeld.store(held, std::memory_order_release);
}
void StructureUiState::setAltHeld(bool held) { mAltHeld.store(held, std::memory_order_release); }
void StructureUiState::setShiftHeld(bool held) { mShiftHeld.store(held, std::memory_order_release); }

unsigned int StructureUiState::currentHotkeyModifiers() const {
    unsigned int modifiers{};
    if (mControlHeld.load(std::memory_order_acquire)) modifiers |= lholo::ui::kHotkeyModifierControl;
    if (mAltHeld.load(std::memory_order_acquire)) modifiers |= lholo::ui::kHotkeyModifierAlt;
    if (mShiftHeld.load(std::memory_order_acquire)) modifiers |= lholo::ui::kHotkeyModifierShift;
    return modifiers;
}

bool StructureUiState::tryPressHotkey(std::size_t index) {
    auto* storage = hotkeyStorage(index);
    return storage && !storage->held.exchange(true, std::memory_order_acq_rel);
}

bool StructureUiState::altHeld() const {
    return mAltHeld.load(std::memory_order_acquire);
}

bool StructureUiState::altWheelOffsetEnabled() const {
    return mAltWheelOffsetEnabled.load(std::memory_order_acquire);
}

bool StructureUiState::setAltWheelOffsetEnabled(bool enabled) {
    return updateRelaxed(mAltWheelOffsetEnabled, enabled);
}

bool StructureUiState::releaseHotkeysForKey(unsigned int key, std::uint64_t now) {
    bool consumed{};
    for (auto& hotkey : mHotkeys) {
        if (key == hotkey.key.load(std::memory_order_acquire)) {
            consumed = hotkey.held.exchange(false, std::memory_order_acq_rel) || consumed;
        }
    }
    if (key < mConsumeKeyReleaseUntil.size()) {
        auto& deadline = mConsumeKeyReleaseUntil[key];
        if (consumed) {
            deadline.store(now + 100, std::memory_order_release);
            return true;
        }
        if (now <= deadline.load(std::memory_order_acquire)) return true;
    }
    return false;
}

void StructureUiState::resetHotkeyState() {
    cancelMenuRoutes();
    mControlHeld.store(false, std::memory_order_release);
    mAltHeld.store(false, std::memory_order_release);
    mShiftHeld.store(false, std::memory_order_release);
    for (auto& hotkey : mHotkeys) hotkey.held.store(false, std::memory_order_release);
    for (auto& deadline : mConsumeKeyReleaseUntil) deadline.store(0, std::memory_order_release);
}

std::uint64_t StructureUiState::ignoreHotkeyUntil() const {
    return mIgnoreHotkeyUntil.load(std::memory_order_acquire);
}

void StructureUiState::setIgnoreHotkeyUntil(std::uint64_t deadline) {
    mIgnoreHotkeyUntil.store(deadline, std::memory_order_release);
}

void StructureUiState::queueOffsetDelta(int deltaX, int deltaY, int deltaZ) {
    mPendingOffsetX.fetch_add(deltaX, std::memory_order_relaxed);
    mPendingOffsetY.fetch_add(deltaY, std::memory_order_relaxed);
    mPendingOffsetZ.fetch_add(deltaZ, std::memory_order_relaxed);
}

void StructureUiState::queueLayerDelta(int delta) {
    if (delta > 0) mPendingLayerDelta.fetch_add(1, std::memory_order_relaxed);
    else if (delta < 0) mPendingLayerDelta.fetch_sub(1, std::memory_order_relaxed);
}

void StructureUiState::queueLoadProjection() {
    mPendingLoadProjection.store(true, std::memory_order_release);
}

void StructureUiState::queueCloseProjection() {
    mPendingCloseProjection.store(true, std::memory_order_release);
}

void StructureUiState::queueToggleManualPlacement() {
    mPendingToggleManualPlacement.store(true, std::memory_order_release);
}

void StructureUiState::requestSettingsSave() {
    mPendingSettingsSave.store(true, std::memory_order_release);
}

PendingHotkeyActions StructureUiState::consumePendingHotkeyActions() {
    return {
        mPendingOffsetX.exchange(0, std::memory_order_acq_rel),
        mPendingOffsetY.exchange(0, std::memory_order_acq_rel),
        mPendingOffsetZ.exchange(0, std::memory_order_acq_rel),
        mPendingLayerDelta.exchange(0, std::memory_order_acq_rel),
        mPendingSettingsSave.exchange(false, std::memory_order_acq_rel),
        mPendingLoadProjection.exchange(false, std::memory_order_acq_rel),
        mPendingCloseProjection.exchange(false, std::memory_order_acq_rel),
        mPendingToggleManualPlacement.exchange(false, std::memory_order_acq_rel)
    };
}

bool StructureUiState::experimentalConsentGiven() const {
    return mExperimentalConsent.load(std::memory_order_acquire);
}

void StructureUiState::setExperimentalConsentGiven(bool given) {
    mExperimentalConsent.store(given, std::memory_order_release);
}

bool StructureUiState::materialHudEnabled() const {
    return mMaterialHudEnabled.load(std::memory_order_acquire);
}

void StructureUiState::setMaterialHudEnabled(bool enabled) {
    mMaterialHudEnabled.store(enabled, std::memory_order_release);
}

int StructureUiState::materialHudPosition() const {
    return mMaterialHudPosition.load(std::memory_order_acquire);
}

void StructureUiState::setMaterialHudPosition(int position) {
    mMaterialHudPosition.store(std::clamp(position, 0, 3), std::memory_order_release);
}

void StructureUiState::setActionHint(i18n::Message message, std::uint64_t expiry) {
    {
        std::lock_guard lock(mActionHintMutex);
        mActionHintMessage = std::move(message);
    }
    mActionHintExpiry.store(expiry, std::memory_order_release);
}

std::uint64_t StructureUiState::actionHintExpiry() const {
    return mActionHintExpiry.load(std::memory_order_acquire);
}

ActionHintSnapshot StructureUiState::actionHint() const {
    auto const expiry = mActionHintExpiry.load(std::memory_order_acquire);
    std::lock_guard lock(mActionHintMutex);
    // A cleared hint carries no message, so the default key must never be
    // rendered as text.
    if (expiry == 0) return {std::string{}, 0};
    return {i18n::format(mActionHintMessage), expiry};
}

void StructureUiState::requestMaterialList() {
    // A loaded structure has an immutable material bill. Reopening the popup
    // must reuse that snapshot instead of rescanning a potentially huge file.
    if (mMaterialListReady.load(std::memory_order_acquire)) return;
    mMaterialListRequested.store(true, std::memory_order_release);
}

bool StructureUiState::consumeMaterialListRequest() {
    return mMaterialListRequested.exchange(false, std::memory_order_acq_rel);
}

bool StructureUiState::materialListReady() const {
    return mMaterialListReady.load(std::memory_order_acquire);
}

void StructureUiState::replaceMaterialRequirements(std::vector<MaterialRequirement> materials) {
    {
        std::lock_guard lock(mMaterialMutex);
        mMaterialRequirements = std::move(materials);
    }
    // Publish readiness only after the complete snapshot is visible. An empty
    // list is also a valid cached result and must not trigger endless rescans.
    mMaterialListReady.store(true, std::memory_order_release);
}

std::vector<MaterialRequirement> StructureUiState::materialRequirements() const {
    std::lock_guard lock(mMaterialMutex);
    return mMaterialRequirements;
}

std::uint64_t StructureUiState::materialHudRevision() const {
    std::lock_guard lock(mMaterialMutex);
    return mMaterialHudRevision;
}

bool StructureUiState::replaceMaterialHudSnapshot(
    std::vector<MaterialRequirement> materials,
    std::vector<int>                 available,
    std::optional<std::uint64_t>     expectedRevision
) {
    auto next = std::make_shared<MaterialHudSnapshot>(
        MaterialHudSnapshot{std::move(materials), std::move(available), true, 0});
    std::shared_ptr<MaterialHudSnapshot const> retired;
    {
        std::lock_guard lock(mMaterialMutex);
        if (expectedRevision && *expectedRevision != mMaterialHudRevision) return false;
        next->revision = ++mMaterialHudRevision;
        retired = std::move(mMaterialHud);
        mMaterialHud = std::move(next);
    }
    return true;
}

bool StructureUiState::setMaterialHudAvailability(std::uint64_t revision, std::vector<int> counts) {
    auto const previous = materialHudView();
    if (!previous || revision != previous->revision
        || counts.size() != previous->requirements.size()) return false;
    // Copy on the publishing tick, outside the mutex. The revision identifies
    // immutable requirements; concurrent availability publications for those
    // same requirements may still commit in their normal mutex order.
    auto next = std::make_shared<MaterialHudSnapshot>(*previous);
    next->available = std::move(counts);
    std::shared_ptr<MaterialHudSnapshot const> retired;
    {
        std::lock_guard lock(mMaterialMutex);
        if (revision != mMaterialHudRevision || !mMaterialHud) return false;
        retired = std::move(mMaterialHud);
        mMaterialHud = std::move(next);
    }
    return true;
}

MaterialHudSnapshot StructureUiState::materialHudSnapshot() const {
    std::shared_ptr<MaterialHudSnapshot const> current;
    std::uint64_t revision;
    {
        std::lock_guard lock(mMaterialMutex);
        current = mMaterialHud;
        revision = mMaterialHudRevision;
    }
    return current ? *current : MaterialHudSnapshot{{}, {}, false, revision};
}

std::shared_ptr<MaterialHudSnapshot const> StructureUiState::materialHudView() const {
    std::lock_guard lock(mMaterialMutex);
    return mMaterialHud;
}

void StructureUiState::clearMaterialHud() {
    std::shared_ptr<MaterialHudSnapshot const> retired;
    {
        std::lock_guard lock(mMaterialMutex);
        retired = std::move(mMaterialHud);
        ++mMaterialHudRevision;
    }
}

void StructureUiState::clearMaterials() {
    mMaterialListRequested.store(false, std::memory_order_release);
    mMaterialListReady.store(false, std::memory_order_release);
    std::shared_ptr<MaterialHudSnapshot const> retired;
    {
        std::lock_guard lock(mMaterialMutex);
        mMaterialRequirements.clear();
        retired = std::move(mMaterialHud);
        ++mMaterialHudRevision;
    }
}

void StructureUiState::resetWorldSession() {
    setGuiVisible(false);
    mOpeningInputBlockFrames.store(0, std::memory_order_release);
    mBlockGameInputUntil.store(0, std::memory_order_release);
    mPendingOffsetX.store(0, std::memory_order_release);
    mPendingOffsetY.store(0, std::memory_order_release);
    mPendingOffsetZ.store(0, std::memory_order_release);
    mPendingLayerDelta.store(0, std::memory_order_release);
    mPendingLoadProjection.store(false, std::memory_order_release);
    mPendingCloseProjection.store(false, std::memory_order_release);
    mPendingToggleManualPlacement.store(false, std::memory_order_release);
    mIgnoreHotkeyUntil.store(0, std::memory_order_release);
    stopHotkeyCapture();
    resetHotkeyState();
    setActionHint(i18n::Message{}, 0);
    clearMaterials();
}

} // namespace lholo::structure::detail
