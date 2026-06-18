#pragma once

// Raises WoW's resident-texture cache budget so an area's working set of textures
// stays resident instead of being evicted and reloaded constantly. See the .cpp
// for the full binary rationale. Tick() re-asserts after device resets.
void InitTexCacheTuning();
void TexCacheTuning_Tick();
