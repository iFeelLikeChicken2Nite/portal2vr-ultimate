#pragma once

namespace Portal2VRViewport
{
    // DXVK can call SetViewport while Game::Initialize is still resolving Source interfaces.
    template <typename GameType>
    bool CanOverrideMenuViewport(GameType *game)
    {
        return game && game->m_EngineClient && game->m_VR &&
               !game->m_EngineClient->IsInGame();
    }
}
