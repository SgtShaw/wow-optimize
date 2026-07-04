#pragma once

#ifndef SIMD_SKINNING_H
#define SIMD_SKINNING_H

namespace SimdSkinning {

// Initialize the SIMD vertex skinning hooks and detect CPU features (SSE4.1 / AVX2)
bool Init();

// Shut down the hooks
void Shutdown();

} // namespace SimdSkinning

#endif // SIMD_SKINNING_H
