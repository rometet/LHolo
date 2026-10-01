#include "ui/MenuPages.h"
#include "ui/FluentTheme.h"
#include "i18n/Translator.h"
#include "i18n/LanguageStore.h"
#include "structure/StructureUiState.h"
#include "ui/MenuWidgets.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
volatile std::size_t checksum{};
template <class Work>
double medianMicros(Work work) {
    work();
    std::vector<double> samples;
    for (int repeat = 0; repeat < 31; ++repeat) {
        auto const start = Clock::now();
        work();
        samples.push_back(std::chrono::duration<double, std::micro>(Clock::now() - start).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

void materialPopupBench(std::size_t count) {
    auto* context = ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = {1920, 1080};
    io.DeltaTime = 1.f / 60.f;
    unsigned char* pixels{};
    int width{}, height{};
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    if (!pixels || width == 0 || height == 0) throw std::runtime_error("headless atlas unavailable");
    auto const metrics = lholo::ui::calculateMetrics(io.DisplaySize, 1.f);
    lholo::ui::applyFluentTheme(metrics);
    lholo::ui::MenuModel model;
    model.hasLoadedStructure = true;
    for (std::size_t i = 0; i < count; ++i) {
        auto const index = std::to_string(i);
        model.materials.push_back({"Material " + index, {}, "minecraft:material_" + index, i + 1, 64});
    }
    auto const popupName = lholo::ui::materialPopupName();
    auto frame = [&](bool open) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize({1600, 900}, ImGuiCond_Always);
        ImGui::Begin("LHoloUiBenchParent", nullptr, ImGuiWindowFlags_NoSavedSettings);
        if (open && !ImGui::IsPopupOpen(popupName.c_str())) ImGui::OpenPopup(popupName.c_str());
        lholo::ui::renderMaterialPopup(model, metrics);
        if (open && !ImGui::IsPopupOpen(popupName.c_str())) throw std::runtime_error("material popup failed to open");
        ImGui::End();
        ImGui::Render();
        checksum = static_cast<std::size_t>(ImGui::GetDrawData()->TotalVtxCount);
    };
    auto const closed = medianMicros([&] { frame(false); });
    auto const closedVertices = checksum;
    auto const opened = medianMicros([&] { frame(true); });
    auto const openVertices = checksum;
    if (openVertices <= closedVertices) throw std::runtime_error("open popup emitted no extra geometry");
    auto const copy = medianMicros([&] {
        auto copied = model;
        checksum = copied.materials.size();
    });
    std::printf("material_popup rows=%zu closed_frame_us=%.3f open_frame_us=%.3f menu_model_copy_us=%.3f closed_vertices=%zu open_vertices=%zu\n",
        count, closed, opened, copy, closedVertices, openVertices);
    lholo::ui::resetFluentTheme();
    ImGui::DestroyContext(context);
}

void materialHudSnapshotBench(std::size_t count) {
    using namespace lholo::structure::detail;
    auto& state = StructureUiState::getInstance();
    std::vector<MaterialRequirement> materials;
    std::vector<int> available;
    for (std::size_t i = 0; i < count; ++i) {
        auto const index = std::to_string(i);
        materials.push_back({"Long localized material name " + index, {},
            "minecraft:material_" + index, "minecraft:item_" + index, (i * 97) % 10000 + 1, 64});
        available.push_back(static_cast<int>((i * 47) % 1000));
    }
    state.replaceMaterialHudSnapshot(std::move(materials), std::move(available));
    struct Row { char const* name; std::uint64_t missing; int stackSize; };
    auto const prepare = medianMicros([&] {
        auto const owner = state.materialHudView();
        auto const& snapshot = *owner;
        std::vector<Row> missing;
        for (std::size_t i = 0; i < snapshot.requirements.size(); ++i) {
            auto const& material = snapshot.requirements[i];
            auto const have = static_cast<std::uint64_t>(snapshot.available[i]);
            if (have < material.count) missing.push_back({
                lholo::ui::materialDisplayName(material.displayName, material.nameKey), material.count - have,
                material.stackSize});
        }
        std::sort(missing.begin(), missing.end(), [](Row const& a, Row const& b) { return a.missing > b.missing; });
        std::size_t value = missing.size();
        for (std::size_t i = 0; i < std::min(missing.size(), std::size_t{14}); ++i) {
            value += lholo::ui::formatStackCount(missing[i].missing, missing[i].stackSize).size();
            value += std::char_traits<char>::length(missing[i].name);
        }
        checksum = value;
    });
    auto const revision = state.materialHudView()->revision;
    auto const counts = state.materialHudView()->available;
    auto const publication = medianMicros([&] {
        if (!state.setMaterialHudAvailability(revision, counts)) throw std::runtime_error("HUD publication rejected");
    });
    std::printf("material_hud rows=%zu acquire_filter_sort_format_us=%.3f checksum=%zu publication_us=%.3f\n",
        count, prepare, checksum, publication);
    state.clearMaterials();
}
} // namespace

int main() {
    lholo::i18n::initLanguageStore();
    if (!lholo::i18n::setLanguageByCode("en_US")) return 2;
    for (auto const count : {100U, 1000U, 10000U}) materialPopupBench(count);
    for (auto const count : {100U, 1000U, 10000U}) materialHudSnapshotBench(count);
    return checksum == 0;
}
