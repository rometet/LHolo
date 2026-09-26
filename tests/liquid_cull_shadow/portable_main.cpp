#include "projection/core/ProjectionLiquidCullShadow.h"

#include <cstdio>
#include <span>
#include <vector>

using namespace lholo::projection::detail;

struct Point { float x, y, z; };
int checks{};
int failures{};

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
    } \
} while (false)

void addFace(std::vector<Point>& vertices, float x, bool positive,
             float y = 0.0F, float z = 0.0F,
             float height = 1.0F, float depth = 1.0F,
             bool sloped = false) {
    Point const a{x, y, z};
    Point const b{x, y + height, z};
    Point const c{x, y + height, z + depth};
    Point const d{sloped ? x + 0.1F : x, y, z + depth};
    if (positive) {
        vertices.insert(vertices.end(), {a, b, c, d});
    } else {
        vertices.insert(vertices.end(), {a, d, c, b});
    }
}

LiquidCullShadowReport inspect(std::vector<Point> const& vertices) {
    return analyzeLiquidCullShadow(
        std::span<Point const>{vertices.data(), vertices.size()}
    );
}

int main() {
    // 1. One opposite-winding pair.
    std::vector<Point> faces;
    addFace(faces, 1.0F, true);
    addFace(faces, 1.0F, false);
    auto result = inspect(faces);
    CHECK(result.valid);
    CHECK(result.currentPairs == 1U);
    CHECK(result.oldPraxisPairs == 1U);
    CHECK(result.currentRemovedVertices == 8U);
    CHECK(result.fullUnitIntegerFaces == 2U);
    CHECK(result.keysWithOppositeWindings == 1U);

    // 2. Same-winding positive duplicate blocks the current mask only.
    faces.clear();
    addFace(faces, 1.0F, true);
    addFace(faces, 1.0F, true);
    addFace(faces, 1.0F, false);
    result = inspect(faces);
    CHECK(result.currentPairs == 0U);
    CHECK(result.oldPraxisPairs == 1U);
    CHECK(result.keysBlockedOnlyByDuplicatePolicy == 1U);
    CHECK(result.duplicatePositiveKeys == 1U);
    CHECK(result.multiplePositiveOneNegativeKeys == 1U);
    CHECK(result.differences.size() == 1U);
    CHECK(result.differences[0].duplicatePositive);

    // 3. Same-winding negative duplicate.
    faces.clear();
    addFace(faces, 1.0F, true);
    addFace(faces, 1.0F, false);
    addFace(faces, 1.0F, false);
    result = inspect(faces);
    CHECK(result.currentPairs == 0U);
    CHECK(result.oldPraxisPairs == 1U);
    CHECK(result.duplicateNegativeKeys == 1U);
    CHECK(result.onePositiveMultipleNegativeKeys == 1U);

    // 4-5. Unpaired positive / negative only.
    faces.clear();
    addFace(faces, 1.0F, true);
    result = inspect(faces);
    CHECK(result.currentPairs == 0U && result.oldPraxisPairs == 0U);
    CHECK(result.onePositiveOnlyKeys == 1U);
    faces.clear();
    addFace(faces, 1.0F, false);
    result = inspect(faces);
    CHECK(result.currentPairs == 0U && result.oldPraxisPairs == 0U);
    CHECK(result.oneNegativeOnlyKeys == 1U);

    // 6. A 0.888889-wide liquid face is not a full unit face.
    faces.clear();
    addFace(faces, 1.0F, true, 0.0F, 0.0F, 1.0F, 0.888889F);
    addFace(faces, 1.0F, false, 0.0F, 0.0F, 1.0F, 0.888889F);
    result = inspect(faces);
    CHECK(result.currentPairs == 0U && result.oldPraxisPairs == 0U);
    CHECK(result.partialAxisAlignedFaces == 2U);

    // 7. Full-size non-integer plane.
    faces.clear();
    addFace(faces, 1.25F, true);
    addFace(faces, 1.25F, false);
    result = inspect(faces);
    CHECK(result.currentPairs == 0U && result.oldPraxisPairs == 0U);
    CHECK(result.nonIntegerPlaneFaces == 2U);

    // 8. Sloped geometry has no flat axis.
    faces.clear();
    addFace(faces, 1.0F, true, 0.0F, 0.0F, 1.0F, 1.0F, true);
    addFace(faces, 1.0F, false, 0.0F, 0.0F, 1.0F, 1.0F, true);
    result = inspect(faces);
    CHECK(result.currentPairs == 0U && result.oldPraxisPairs == 0U);
    CHECK(result.slopedFaces == 2U);

    // 9. Adjacent full water cubes meet at x=2.
    faces.clear();
    addFace(faces, 2.0F, true, 4.0F, 7.0F);
    addFace(faces, 2.0F, false, 4.0F, 7.0F);
    result = inspect(faces);
    CHECK(result.currentPairs == 1U && result.oldPraxisPairs == 1U);
    CHECK(result.oldPairs[0].key.plane == 2);
    CHECK(result.oldPairs[0].key.first == 4);
    CHECK(result.oldPairs[0].key.second == 7);

    // 10. Overlay-like duplicate plus opposite; the shadow never edits input.
    faces.clear();
    addFace(faces, 3.0F, true);
    addFace(faces, 3.0F, true);
    addFace(faces, 3.0F, false);
    auto const original = faces;
    result = inspect(faces);
    CHECK(result.currentPairs == 0U && result.oldPraxisPairs == 1U);
    CHECK(result.keysBlockedOnlyByDuplicatePolicy == 1U);
    CHECK(faces.size() == original.size());
    for (std::size_t index = 0; index < faces.size(); ++index) {
        CHECK(faces[index].x == original[index].x);
        CHECK(faces[index].y == original[index].y);
        CHECK(faces[index].z == original[index].z);
    }

    // Diagnostics are bounded even if a large schematic reaches this path.
    faces.clear();
    for (std::size_t index = 0; index < MaxLiquidCullDetailQuads + 1U; ++index) {
        addFace(faces, static_cast<float>(index), true);
    }
    result = inspect(faces);
    CHECK(result.detailQuads.size() == MaxLiquidCullDetailQuads);
    faces.resize((MaxLiquidCullDiagnosticQuads + 1U) * 4U);
    result = inspect(faces);
    CHECK(!result.valid && result.tooLarge);

    std::printf("LiquidCullShadowTests: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
