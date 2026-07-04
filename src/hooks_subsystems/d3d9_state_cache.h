#pragma once

#ifndef D3D9_STATE_CACHE_H
#define D3D9_STATE_CACHE_H

namespace D3D9StateCache {

// Initialize the D3D9 state cache, hook SetTexture, SetRenderState, etc.
bool Init();

// Shut down the hooks and release resources
void Shutdown();

} // namespace D3D9StateCache

#endif // D3D9_STATE_CACHE_H
