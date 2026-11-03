// Force-included into the Dear ImGui sources: a failed internal check is logged instead of aborting the game.
#pragma once
void TsImGuiAssert(const char* expr, const char* file, int line);
#define IM_ASSERT(_EXPR) ((_EXPR) ? (void)0 : TsImGuiAssert(#_EXPR, __FILE__, __LINE__))
