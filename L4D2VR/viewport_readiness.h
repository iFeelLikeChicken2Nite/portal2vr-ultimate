#pragma once
#include <cstdint>

namespace Portal2VRViewport
{
    inline bool CanUseRecommendedVRSize(bool vrReady, std::uint32_t width,
                                        std::uint32_t height)
    {
        return vrReady && width != 0 && height != 0;
    }

    template <typename GameType>
    bool HasInitializedVR(GameType *game)
    {
        return game && game->m_VR && game->m_VR->m_IsInitialized;
    }

    // DXVK can call SetViewport while Game::Initialize is still resolving Source interfaces.
    template <typename GameType>
    bool CanOverrideMenuViewport(GameType *game)
    {
        return HasInitializedVR(game) && game->m_EngineClient &&
               !game->m_EngineClient->IsInGame();
    }
}
