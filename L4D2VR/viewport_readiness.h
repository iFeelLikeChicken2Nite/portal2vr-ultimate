#pragma once

namespace Portal2VRViewport
{
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
