// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Pure view frustum culling mathematics. Does not depend on external game symbols
// or stateful game resources.

#pragma once

#include "mc/deps/core/math/Vec3.h"
#include <array>
#include <cmath>
#include <glm/glm.hpp>

namespace lholo::projection::detail {

struct FrustumPlane {
    float a{0.0f};
    float b{0.0f};
    float c{0.0f};
    float d{0.0f};

    bool normalize() {
        float const lenSq = a * a + b * b + c * c;
        if (std::isfinite(lenSq) && std::isfinite(d) && lenSq > 1e-8f) {
            float const invLen = 1.0f / std::sqrt(lenSq);
            a *= invLen;
            b *= invLen;
            c *= invLen;
            d *= invLen;
            return true;
        }
        return false;
    }

    [[nodiscard]] float distanceTo(Vec3 const &point) const {
        return a * point.x + b * point.y + c * point.z + d;
    }
};

class ProjectionFrustum {
  public:
    ProjectionFrustum() = default;

    // Gribb-Hartmann fast frustum extraction from camera-relative View-Projection matrix.
    // Only side planes: do not assume GL/D3D depth, reverse-Z or far distance.
    static ProjectionFrustum fromViewProjection(glm::mat4x4 const &vp) {
        ProjectionFrustum frustum;
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (!std::isfinite(vp[column][row]))
                    return frustum;
            }
        }
        // In GLM, vp is column-major: vp[col][row].
        auto const getRow = [&](int row) {
            return glm::vec4(vp[0][row], vp[1][row], vp[2][row], vp[3][row]);
        };

        glm::vec4 const r0 = getRow(0);
        glm::vec4 const r1 = getRow(1);
        glm::vec4 const r3 = getRow(3);

        // Left plane:   row3 + row0
        frustum.mPlanes[0] = {r3.x + r0.x, r3.y + r0.y, r3.z + r0.z, r3.w + r0.w};
        // Right plane:  row3 - row0
        frustum.mPlanes[1] = {r3.x - r0.x, r3.y - r0.y, r3.z - r0.z, r3.w - r0.w};
        // Bottom plane: row3 + row1
        frustum.mPlanes[2] = {r3.x + r1.x, r3.y + r1.y, r3.z + r1.z, r3.w + r1.w};
        // Top plane:    row3 - r1
        frustum.mPlanes[3] = {r3.x - r1.x, r3.y - r1.y, r3.z - r1.z, r3.w - r1.w};
        for (auto &plane : frustum.mPlanes) {
            if (!plane.normalize())
                return ProjectionFrustum{};
        }
        frustum.mValid = true;
        return frustum;
    }

    [[nodiscard]] bool isValid() const { return mValid; }

    // Conservative AABB intersection against the four side planes.
    // Points must be in the same coordinate space as the VP matrix (camera-relative).
    [[nodiscard]] bool isBoxVisible(Vec3 const &boxMin, Vec3 const &boxMax) const {
        if (!mValid)
            return true;
        if (!std::isfinite(boxMin.x) || !std::isfinite(boxMin.y) || !std::isfinite(boxMin.z) ||
            !std::isfinite(boxMax.x) || !std::isfinite(boxMax.y) || !std::isfinite(boxMax.z) ||
            boxMin.x > boxMax.x || boxMin.y > boxMax.y || boxMin.z > boxMax.z)
            return true;

        for (auto const &plane : mPlanes) {
            // Find p-vertex: the box corner most in the direction of the plane normal
            Vec3 const p{plane.a > 0.0f ? boxMax.x : boxMin.x, plane.b > 0.0f ? boxMax.y : boxMin.y,
                         plane.c > 0.0f ? boxMax.z : boxMin.z};
            if (plane.distanceTo(p) < -0.01f) {
                // If the p-vertex is outside the plane, the entire box is culled
                return false;
            }
        }
        return true;
    }

  private:
    std::array<FrustumPlane, 4> mPlanes{};
    bool mValid{false};
};

} // namespace lholo::projection::detail
