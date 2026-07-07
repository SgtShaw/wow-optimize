#pragma once

namespace WorldToScreenSse {
    struct Vector3 {
        float x, y, z;
    };
    struct Vector2 {
        float x, y;
    };

    bool Init();
    void Shutdown();
    bool ProjectCoordinates(const Vector3& worldPos, const float* viewProjectionMatrix, int screenWidth, int screenHeight, Vector2* outScreenPos);
}
