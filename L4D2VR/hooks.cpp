#include "hooks.h"
#include "game.h"
#include "texture.h"
#include "sdk.h"
#include "sdk_server.h"
#include "vr.h"
#include "offsets.h"
#include "logger.h"
#include "runtime_publication.h"
#include "aim_feedback.h"
#include <Windows.h>
#include <cstdint>
#include <iostream>
#include <optional>

static std::optional<PortalOrientation::Rotation> ReadPortalRotation(const void* portal)
{
    if (!portal)
        return std::nullopt;
    // The existing CPortal_Base2D accessor uses this ABI-specific field.
    constexpr std::uintptr_t kMatrixOffset = 0x4C4;
    const auto base = reinterpret_cast<std::uintptr_t>(portal);
    if (base > UINTPTR_MAX - kMatrixOffset - sizeof(VMatrix))
        return std::nullopt;
    const auto matrixAddress = base + kMatrixOffset;
    VMatrix matrix;
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(matrixAddress),
                           &matrix, sizeof(matrix), &bytesRead) || bytesRead != sizeof(matrix))
        return std::nullopt;
    return PortalOrientation::Rotation::FromVMatrix(matrix);
}

Hooks::Hooks(Game *game)
{
	if (MH_Initialize() != MH_OK)
	{
		Game::errorMsg("Failed to init MinHook");
		return;
	}
	m_MinHookInitialized = true;

	m_Game = game;
	m_VR = m_Game->m_VR;

	m_PushHUDStep = -999;
	m_PushedHud = true;
	m_HudCaptureRoute = HudCapture::RouteState{};

	if (initSourceHooks() != 0)
		return;

#define ENABLE_REQUIRED(hook) do { if (hook.enableHook()) { Logger::Write("Failed to enable " #hook); return; } } while (false)

	//hkGetRenderTarget.enableHook();
	ENABLE_REQUIRED(hkCalcViewModelView);

	ENABLE_REQUIRED(hkProcessUsercmds);
	ENABLE_REQUIRED(hkReadUsercmd);

	//hkWriteUsercmdDeltaToBuffer.enableHook();
	ENABLE_REQUIRED(hkWriteUsercmd);

	ENABLE_REQUIRED(hkCreateMove);
	ENABLE_REQUIRED(hkEyePosition);
	ENABLE_REQUIRED(hkRenderView);


	ENABLE_REQUIRED(hkWeapon_ShootPosition);
	ENABLE_REQUIRED(hkTraceFirePortal);
	ENABLE_REQUIRED(hkCWeaponPortalgun_FirePortal);

	ENABLE_REQUIRED(hkDrawSelf);
	ENABLE_REQUIRED(hkPlayerPortalled);

	//hkComputeError.enableHook();
	ENABLE_REQUIRED(hkUpdateObject);
	ENABLE_REQUIRED(hkUpdateObjectVM);
	//hkRotateObject.enableHook();
	ENABLE_REQUIRED(hkEyeAngles);

	ENABLE_REQUIRED(hkGetDefaultFOV);
	ENABLE_REQUIRED(hkGetFOV);
	ENABLE_REQUIRED(hkGetViewModelFOV);

	ENABLE_REQUIRED(hkSetDrawOnlyForSplitScreenUser);
	//kClientThink.enableHook();
	if (m_Game->m_Offsets->m_LaserAvailable && hkPrecache.enableHook()) {
		m_Game->m_Offsets->m_LaserAvailable = false;
		Logger::Write("Laser pointer disabled: Precache hook enable failed.");
	}
	ENABLE_REQUIRED(hkCHudCrosshair_ShouldDraw);
#undef ENABLE_REQUIRED
	if (m_VR->m_Config.experimentalHudOverlay &&
		m_VR->m_HUDHandle != vr::k_ulOverlayHandleInvalid) {
		const auto *offsets = m_Game->m_Offsets;
		Logger::Write("Experimental HUD symbols: PushRenderTarget=" +
			std::string(offsets->PushRenderTargetAndViewport.address ? "OK" : "MISSING") +
			" PopRenderTarget=" +
			std::string(offsets->PopRenderTargetAndViewport.address ? "OK" : "MISSING") +
			" VGui_Paint=" + std::string(offsets->VGui_Paint.address ? "OK" : "MISSING"));
		if (offsets->PushRenderTargetAndViewport.address &&
			offsets->PopRenderTargetAndViewport.address && offsets->VGui_Paint.address) {
			const bool created =
				!hkPushRenderTargetAndViewport.createHook(
					(LPVOID)offsets->PushRenderTargetAndViewport.address, &dPushRenderTargetAndViewport) &&
				!hkPopRenderTargetAndViewport.createHook(
					(LPVOID)offsets->PopRenderTargetAndViewport.address, &dPopRenderTargetAndViewport) &&
				!hkVgui_Paint.createHook((LPVOID)offsets->VGui_Paint.address, &dVGui_Paint);
			if (created) {
				const bool pushEnabled = !hkPushRenderTargetAndViewport.enableHook();
				const bool popEnabled = pushEnabled && !hkPopRenderTargetAndViewport.enableHook();
				const bool paintEnabled = popEnabled && !hkVgui_Paint.enableHook();
				m_HudCaptureHooksReady = pushEnabled && popEnabled && paintEnabled;
				if (!m_HudCaptureHooksReady) {
					if (popEnabled) hkPopRenderTargetAndViewport.disableHook();
					if (pushEnabled) hkPushRenderTargetAndViewport.disableHook();
				}
			}
		}
		Logger::Write(m_HudCaptureHooksReady ?
			"Experimental HUD VGUI capture hooks enabled" :
			"Experimental HUD capture unavailable; stereo rendering remains enabled");
	}
	m_Ready = true;
}

Hooks::~Hooks()
{
	if (m_MinHookInitialized) {
		MH_DisableHook(MH_ALL_HOOKS);
		if (MH_Uninitialize() != MH_OK)
			Logger::Write("Failed to uninitialize MinHook");
	}
}


int Hooks::initSourceHooks()
{
	/*LPVOID pGetRenderTargetVFunc = (LPVOID)(m_Game->m_Offsets->GetRenderTarget.address);
	hkGetRenderTarget.createHook(pGetRenderTargetVFunc, &dGetRenderTarget);*/

	LPVOID pRenderViewVFunc = (LPVOID)(m_Game->m_Offsets->RenderView.address);
	hkRenderView.createHook(pRenderViewVFunc, &dRenderView);

	LPVOID calcViewModelViewAddr = (LPVOID)(m_Game->m_Offsets->CalcViewModelView.address);
	hkCalcViewModelView.createHook(calcViewModelViewAddr, &dCalcViewModelView);

	LPVOID ProcessUsercmdsAddr = (LPVOID)(m_Game->m_Offsets->ProcessUsercmds.address);
	hkProcessUsercmds.createHook(ProcessUsercmdsAddr, &dProcessUsercmds);

	LPVOID ReadUserCmdAddr = (LPVOID)(m_Game->m_Offsets->ReadUserCmd.address);
	hkReadUsercmd.createHook(ReadUserCmdAddr, &dReadUsercmd);

	/*LPVOID WriteUsercmdDeltaToBufferAddr = (LPVOID)(m_Game->m_Offsets->WriteUsercmdDeltaToBuffer.address);
	hkWriteUsercmdDeltaToBuffer.createHook(WriteUsercmdDeltaToBufferAddr, &dWriteUsercmdDeltaToBuffer);*/

	LPVOID WriteUsercmdAddr = (LPVOID)(m_Game->m_Offsets->WriteUsercmd.address);
	hkWriteUsercmd.createHook(WriteUsercmdAddr, &dWriteUsercmd);

	/*LPVOID AdjustEngineViewportAddr = (LPVOID)(m_Game->m_Offsets->AdjustEngineViewport.address);
	hkAdjustEngineViewport.createHook(AdjustEngineViewportAddr, &dAdjustEngineViewport);

	LPVOID ViewportAddr = (LPVOID)(m_Game->m_Offsets->Viewport.address);
	hkViewport.createHook(ViewportAddr, &dViewport);

	LPVOID GetViewportAddr = (LPVOID)(m_Game->m_Offsets->GetViewport.address);
	hkGetViewport.createHook(GetViewportAddr, &dGetViewport);*/

	LPVOID EyePositionAddr = (LPVOID)(m_Game->m_Offsets->EyePosition.address);
	hkEyePosition.createHook(EyePositionAddr, &dEyePosition);

	/*LPVOID DrawModelExecuteAddr = (LPVOID)(m_Game->m_Offsets->DrawModelExecute.address);
	hkDrawModelExecute.createHook(DrawModelExecuteAddr, &dDrawModelExecute);*/


	/*LPVOID IsSplitScreenAddr = (LPVOID)(m_Game->m_Offsets->IsSplitScreen.address);
	hkIsSplitScreen.createHook(IsSplitScreenAddr, &dIsSplitScreen);*/


	/*LPVOID GetFullScreenTextureAddr = (LPVOID)(m_Game->m_Offsets->GetFullScreenTexture.address);
	hkGetFullScreenTexture.createHook(GetFullScreenTextureAddr, &dGetFullScreenTexture);*/

	LPVOID Weapon_ShootPositionAddr = (LPVOID)(m_Game->m_Offsets->Weapon_ShootPosition.address);
	hkWeapon_ShootPosition.createHook(Weapon_ShootPositionAddr, &dWeapon_ShootPosition);
	
	LPVOID TraceFirePortalAddr = (LPVOID)(m_Game->m_Offsets->TraceFirePortalServer.address);
	hkTraceFirePortal.createHook(TraceFirePortalAddr, &dTraceFirePortal);

	hkCWeaponPortalgun_FirePortal.createHook((LPVOID)m_Game->m_Offsets->CWeaponPortalgun_FirePortal.address, &dCWeaponPortalgun_FirePortal);

	LPVOID DrawSelfAddr = (LPVOID)(m_Game->m_Offsets->DrawSelf.address);
	hkDrawSelf.createHook(DrawSelfAddr, &dDrawSelf);
	// Projection is called by our HUD hook, but does not need its own detour.
	ClipTransform = reinterpret_cast<tClipTransform>(m_Game->m_Offsets->ClipTransform.address);
	

	// Portalling
	LPVOID PlayerPortalledAddr = (LPVOID)(m_Game->m_Offsets->PlayerPortalled.address);
	hkPlayerPortalled.createHook(PlayerPortalledAddr, &dPlayerPortalled);

	UTIL_Portal_FirstAlongRay = (tUTIL_Portal_FirstAlongRay)m_Game->m_Offsets->UTIL_Portal_FirstAlongRay.address;
	UTIL_IntersectRayWithPortal = (tUTIL_IntersectRayWithPortal)m_Game->m_Offsets->UTIL_IntersectRayWithPortal.address;
	UTIL_Portal_AngleTransform = (tUTIL_Portal_AngleTransform)m_Game->m_Offsets->UTIL_Portal_AngleTransform.address;

	LPVOID CreateMoveAddr = (LPVOID)(m_Game->m_Offsets->CreateMove.address);
	hkCreateMove.createHook(CreateMoveAddr, &dCreateMove);

	// Grababbles
	hkUpdateObject.createHook((LPVOID)(m_Game->m_Offsets->UpdateObject.address), &dUpdateObject);
	hkUpdateObjectVM.createHook((LPVOID)(m_Game->m_Offsets->UpdateObjectVM.address), &dUpdateObjectVM);
	hkEyeAngles.createHook((LPVOID)(m_Game->m_Offsets->EyeAngles.address), &dEyeAngles);

	// Portal Gun VFX
	hkGetDefaultFOV.createHook((LPVOID)(m_Game->m_Offsets->GetDefaultFOV.address), &dGetDefaultFOV);
	hkGetFOV.createHook((LPVOID)(m_Game->m_Offsets->GetFOV.address), &dGetFOV);
	hkGetViewModelFOV.createHook((LPVOID)(m_Game->m_Offsets->GetViewModelFOV.address), &dGetViewModelFOV);
	
	// Laser Pointer
	GetPortalPlayer = (tGetPortalPlayer)m_Game->m_Offsets->GetPortalPlayer.address;
	CreatePingPointer = (tCreatePingPointer)m_Game->m_Offsets->CreatePingPointer.address;
	PrecacheParticleSystem = (tPrecacheParticleSystem)m_Game->m_Offsets->PrecacheParticleSystem.address;
	if (m_Game->m_Offsets->m_LaserAvailable &&
		hkPrecache.createHook((LPVOID)(m_Game->m_Offsets->Precache.address), &dPrecache)) {
		m_Game->m_Offsets->m_LaserAvailable = false;
		Logger::Write("Laser pointer disabled: Precache hook creation failed.");
	}
	hkSetDrawOnlyForSplitScreenUser.createHook((LPVOID)m_Game->m_Offsets->SetDrawOnlyForSplitScreenUser.address, &dSetDrawOnlyForSplitScreenUser);
	hkCHudCrosshair_ShouldDraw.createHook((LPVOID)m_Game->m_Offsets->CHudCrosshair_ShouldDraw.address, &dCHudCrosshair_ShouldDraw);

	//
	EntityIndex = (tEntindex)m_Game->m_Offsets->CBaseEntity_entindex.address;
	GetOwner = (tGetOwner)m_Game->m_Offsets->GetOwner.address;
	GetFullScreenTexture = (tGetFullScreenTexture)m_Game->m_Offsets->GetFullScreenTexture.address;
	return 0;
} 

bool __fastcall Hooks::dCHudCrosshair_ShouldDraw(void* ecx, void* edx) {
	bool shouldDraw = hkCHudCrosshair_ShouldDraw.fOriginal(ecx);
	if (Portal2VRRuntime::IsPublished(g_Game, m_Game) && m_VR->m_IsVREnabled &&
		m_Game->m_EngineClient->IsInGame() &&
		m_VR->m_RenderDiagnostics.First(shouldDraw ? RenderDiagnosticEvent::CrosshairShouldDrawTrue :
			RenderDiagnosticEvent::CrosshairShouldDrawFalse))
		Logger::Write(std::string("Crosshair ShouldDraw: Source returned ") +
			(shouldDraw ? "true" : "false") + " (actual pixels unverified)");

	// Keep the Source crosshair as a fallback until the optional laser is
	// confirmed visible by a real VR test. Symbol resolution alone is not proof.
	return shouldDraw;
}

void __fastcall Hooks::dPrecache(void* ecx, void* edx) {
	hkPrecache.fOriginal(ecx);
	PrecacheParticleSystem("robot_point_beam");
}

void __fastcall Hooks::dClientThink(void* ecx, void* edx) {
	hkClientThink.fOriginal(ecx);
}

void __fastcall Hooks::dSetDrawOnlyForSplitScreenUser(void* ecx, void* edx, int nSlot) {
	hkSetDrawOnlyForSplitScreenUser.fOriginal(ecx, -1);
}

ITexture *__fastcall Hooks::dGetFullScreenTexture()
{
	ITexture *result = hkGetFullScreenTexture.fOriginal();
	return result;
}

ITexture* __fastcall Hooks::dGetRenderTarget(void* ecx, void* edx)
{
	ITexture* result = hkGetRenderTarget.fOriginal(ecx);
	return result;
}

void __fastcall Hooks::dRenderView(void *ecx, void *edx, CViewSetup &setup, CViewSetup &hudViewSetup, int nClearFlags, int whatToDraw)
{
	// MinHook may dispatch this while Game::Initialize is still enabling hooks.
	if (!Portal2VRRuntime::IsPublished(g_Game, m_Game))
		return hkRenderView.fOriginal(ecx, setup, hudViewSetup, nClearFlags, whatToDraw);
	m_VR->ApplyPendingPortalOrientation(setup.origin);
    if (!m_VR->m_TrackingOutputValid) {
        if (m_VR->m_RenderDiagnostics.First(RenderDiagnosticEvent::TrackingBypass))
            Logger::Write("RenderView: stereo bypassed because tracking output is invalid");
        return hkRenderView.fOriginal(ecx, setup, hudViewSetup, nClearFlags, whatToDraw);
	}
	if (!m_VR->m_CreatedVRTextures) {
		m_VR->CreateVRTextures();
	}
	if (!m_VR->m_CreatedVRTextures) {
        if (m_VR->m_RenderDiagnostics.First(RenderDiagnosticEvent::RenderTargetBypass))
            Logger::Write("RenderView: stereo bypassed because VR render targets are unavailable");
        return hkRenderView.fOriginal(ecx, setup, hudViewSetup, nClearFlags, whatToDraw);
    }

	if (m_Game->m_VguiSurface->IsCursorVisible()) {
        if (m_VR->m_RenderDiagnostics.First(RenderDiagnosticEvent::CursorBypass))
            Logger::Write("RenderView: stereo bypassed while VGUI cursor is visible");
		return hkRenderView.fOriginal(ecx, setup, hudViewSetup, nClearFlags, whatToDraw);
	}

	//VPanel* g_pFullscreenRootPanel = *(VPanel**)(m_Game->m_Offsets->g_pFullscreenRootPanel.address);

	IMaterialSystem* matSystem = m_Game->m_MaterialSystem;

	hudViewSetup.width = m_VR->m_RenderWidth;
	hudViewSetup.height = m_VR->m_RenderHeight;
	hudViewSetup.fov = m_VR->m_Fov;
	//hudViewSetup.fovViewmodel = m_VR->m_Fov;
	hudViewSetup.m_flAspectRatio = m_VR->m_Aspect;

	hudViewSetup.m_nUnscaledWidth = m_VR->m_RenderWidth;
	hudViewSetup.m_nUnscaledHeight = m_VR->m_RenderHeight;

	Vector position = setup.origin;

    if (!m_VR->ExperimentalPortalOrientation() && m_VR->m_ApplyPortalRotationOffset) {
		Vector vec = position - m_VR->m_SetupOrigin;
		float distance = sqrt(vec.x * vec.x + vec.y * vec.y + vec.z * vec.z);

		// Rudimentary portalling detection
		if (distance > 35) {
			//m_VR->m_RotationOffset.x += m_VR->m_PortalRotationOffset.x;
			m_VR->m_RotationOffset.y += m_VR->m_PortalRotationOffset.y;
			//m_VR->m_RotationOffset.z += m_VR->m_PortalRotationOffset.z;

			m_VR->UpdateHMDAngles();

			m_VR->m_ApplyPortalRotationOffset = false;
		}
	}

	m_VR->m_SetupOrigin = position;

	Vector hmdAngle = m_VR->GetViewAngle();
	QAngle inGameAngle(hmdAngle.x, hmdAngle.y, hmdAngle.z);
	m_Game->m_EngineClient->SetViewAngles(inGameAngle);

	float aspect = setup.m_flAspectRatio;

	setup.x = 0;
	setup.y = 0;
	setup.width = m_VR->m_RenderWidth;
	setup.height = m_VR->m_RenderHeight;
	setup.m_nUnscaledWidth = m_VR->m_RenderWidth;
	setup.m_nUnscaledHeight = m_VR->m_RenderHeight;
	setup.fov = m_VR->m_Fov;
	setup.fovViewmodel = m_VR->m_Fov;
	setup.m_flAspectRatio = m_VR->m_Aspect;
	setup.zNear = 6;
	setup.zNearViewmodel = 2;
	setup.angles = hmdAngle;

	CViewSetup leftEyeView = setup;
	CViewSetup rightEyeView = setup;

	int playerIndex = m_Game->m_EngineClient->GetLocalPlayer();
	C_BasePlayer* localPlayer = (C_BasePlayer*)m_Game->GetClientEntity(playerIndex);

	// Left eye CViewSetup
	QAngle tempAngle = QAngle(setup.angles.x, setup.angles.y, setup.angles.z);
	leftEyeView.origin = m_VR->TraceEye((uint32_t*)localPlayer, position, m_VR->GetViewOriginLeft(position), tempAngle);
	if (m_VR->ExperimentalPortalOrientation())
		leftEyeView.angles = Vector(tempAngle.x, tempAngle.y, tempAngle.z);
	else
		leftEyeView.angles.y = tempAngle.y;

	//std::cout << "dRenderView - Left Start\n";
	IMatRenderContext* rndrContext = matSystem->GetRenderContext();
	rndrContext->SetRenderTarget(m_VR->m_LeftEyeTexture);
	rndrContext->Release();
	hkRenderView.fOriginal(ecx, leftEyeView, hudViewSetup, nClearFlags, whatToDraw);
	
	// Right eye CViewSetup
	tempAngle = QAngle(setup.angles.x, setup.angles.y, setup.angles.z);
	rightEyeView.origin = m_VR->TraceEye((uint32_t*)localPlayer, position, m_VR->GetViewOriginRight(position), tempAngle);
	if (m_VR->ExperimentalPortalOrientation())
		rightEyeView.angles = Vector(tempAngle.x, tempAngle.y, tempAngle.z);
	else
		rightEyeView.angles.y = tempAngle.y;

	//std::cout << "dRenderView - Right Start\n";
	rndrContext = matSystem->GetRenderContext();
	rndrContext->SetRenderTarget(m_VR->m_RightEyeTexture);
	rndrContext->Release();
	hkRenderView.fOriginal(ecx, rightEyeView, hudViewSetup, nClearFlags, whatToDraw);

	m_PushedHud = false;



	rndrContext = matSystem->GetRenderContext();
	rndrContext->SetRenderTarget(NULL);
	rndrContext->Release();

	/*rndrContext = matSystem->GetRenderContext();

	ITexture* fullscreenTxt = rndrContext->GetRenderTarget();

	Rect_t srcRect;
	srcRect.x = setup.x;
	srcRect.y = setup.y;
	srcRect.width = 1920;
	srcRect.height = 1080;

	rndrContext->SetRenderTarget(m_VR->m_RightEyeTexture);
	rndrContext->CopyRenderTargetToTextureEx(fullscreenTxt, 0, &srcRect, &srcRect);

	rndrContext->SetRenderTarget(NULL);
	rndrContext->Release();*/

	if (m_VR->m_RenderWindow) {
		setup.m_flAspectRatio = aspect;

		//setup.width, setup.height
		hkRenderView.fOriginal(ecx, setup, hudViewSetup, nClearFlags, whatToDraw);
	}


	m_VR->m_RenderedNewFrame = true;
    if (m_VR->m_RenderDiagnostics.First(RenderDiagnosticEvent::StereoRendered))
        Logger::Write("RenderView: first stereo pair rendered");
}

bool __fastcall Hooks::dCreateMove(void *ecx, void *edx, float flInputSampleTime, CUserCmd *cmd)
{
	if (!cmd->command_number)
		return hkCreateMove.fOriginal(ecx, flInputSampleTime, cmd);

	if (m_VR->m_IsVREnabled && m_VR->m_TrackingOutputValid)
	{
		cmd->viewangles = m_VR->m_HmdAngAbs;

		vr::InputAnalogActionData_t analogActionData;
		if (m_VR->GetAnalogActionData(m_VR->m_ActionWalk, analogActionData)) {
			// Run toward other guy
			cmd->buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);

			const auto movement = TrackingSpace::RebaseAnalogToView(
				analogActionData.x, analogActionData.y, m_VR->GetMovementForward(), m_VR->m_HmdForward);
			cmd->forwardmove += movement.forward * MAX_LINEAR_SPEED;
			cmd->sidemove += movement.side * MAX_LINEAR_SPEED;

			// We'll only be moving fwd or sideways
			cmd->upmove = 0.0f;

			if (cmd->forwardmove > 0.0f)
			{
				cmd->buttons |= IN_FORWARD;
			}
			else if (cmd->forwardmove < 0.0f)
			{
				cmd->buttons |= IN_BACK;
			}

			if (cmd->sidemove > 0.0f)
			{
				cmd->buttons |= IN_MOVELEFT;
			}
			else if (cmd->sidemove < 0.0f)
			{
				cmd->buttons |= IN_MOVERIGHT;
			}

		}

	}
	m_VR->ObserveRoomscaleCommand(cmd->command_number); // diagnostic only; never changes CUserCmd

	return false;
}

void __fastcall Hooks::dEndFrame(void *ecx, void *edx)
{
	return hkEndFrame.fOriginal(ecx);
}

void __fastcall Hooks::dCalcViewModelView(void *ecx, void *edx, const Vector &eyePosition, const QAngle &eyeAngles)
{
	Vector vecNewOrigin = eyePosition;
	QAngle vecNewAngles = eyeAngles;

	//std::cout << "dCalcViewModelView: (" << m_VR->m_IsVREnabled << ")\n";

	if (m_VR->m_IsVREnabled && m_VR->m_TrackingOutputValid && m_VR->m_RightControllerPose.valid)
	{
		vecNewOrigin = m_VR->GetRecommendedViewmodelAbsPos();
		vecNewAngles = m_VR->GetRecommendedViewmodelAbsAngle();
	}


	return hkCalcViewModelView.fOriginal(ecx, vecNewOrigin, vecNewAngles);
}

float __fastcall Hooks::dProcessUsercmds(void *ecx, void *edx, edict_t *player, void *buf, int numcmds, int totalcmds, int dropped_packets, bool ignore, bool paused)
{
	Server_BaseEntity *pPlayer = (Server_BaseEntity*)player->m_pUnk->GetBaseEntity();

	int index = EntityIndex(pPlayer);
	m_Game->m_CurrentUsercmdID = index;

	return hkProcessUsercmds.fOriginal(ecx, player, buf, numcmds, totalcmds, dropped_packets, ignore, paused);
}

int Hooks::dWriteUsercmd(bf_write *buf, CUserCmd *to, CUserCmd *from)
{
	auto result =  hkWriteUsercmd.fOriginal(buf, to, from);

	// Let's write our stuff into the buffer
	if (m_VR->m_IsVREnabled && m_VR->m_TrackingOutputValid && m_VR->m_RightControllerPose.valid)
	{
		Vector controllerPos = m_VR->GetRightControllerAbsPos();
		QAngle controllerAngles = m_VR->GetRightControllerAbsAngle();

		buf->WriteChar(-2);
		buf->WriteBitVec3Coord(controllerPos);
		buf->WriteBitAngles(controllerAngles);
	}

	return result;
}

int Hooks::dReadUsercmd(bf_read *buf, CUserCmd* move, CUserCmd* from)
{
	auto result = hkReadUsercmd.fOriginal(buf, move, from);

	int i = m_Game->m_CurrentUsercmdID;
	auto& vrPlayer = m_Game->m_PlayersVRInfo[i];

	auto pos = buf->Tell();
	int res = buf->ReadChar();

	// This means we got a VR player on the other side
	if (res == -2)
	{
		vrPlayer.isUsingVR = true;
		buf->ReadBitVec3Coord(vrPlayer.controllerPos);
		buf->ReadBitAngles(vrPlayer.controllerAngle);
	}
	else {
		vrPlayer.isUsingVR = false;
		buf->Seek(pos);
	}

	return result;
}


void Hooks::dAdjustEngineViewport(int &x, int &y, int &width, int &height)
{
	width = m_VR->m_RenderWidth;
	height = m_VR->m_RenderHeight;

	hkAdjustEngineViewport.fOriginal(x, y, width, height);
}

void Hooks::dGetViewport(void *ecx, void *edx, int &x, int &y, int &width, int &height)
{
	hkGetViewport.fOriginal(ecx, x, y, width, height);

	width = m_VR->m_RenderWidth;
	height = m_VR->m_RenderHeight;
}

int Hooks::dGetPrimaryAttackActivity(void *ecx, void *edx, void *meleeInfo)
{
	return hkGetPrimaryAttackActivity.fOriginal(ecx, meleeInfo);
}

Vector *Hooks::dEyePosition(void *ecx, void *edx, Vector *eyePos)
{
	Vector *result = hkEyePosition.fOriginal(ecx, eyePos);
	return result;
}

// We'll keep this for... future reference!
void Hooks::dDrawModelExecute(void *ecx, void *edx, void *state, const ModelRenderInfo_t &info, void *pCustomBoneToWorld)
{
	if (info.pModel)
	{
		std::string modelName = m_Game->m_ModelInfo->GetModelName(info.pModel);
		if (modelName.find("/arms/") != std::string::npos)
		{
			m_Game->m_ArmsMaterial = m_Game->m_MaterialSystem->FindMaterial(modelName.c_str(), "Model textures");
			m_Game->m_ArmsModel = info.pModel;
			m_Game->m_CachedArmsModel = true;
		}
	}

	if (info.pModel && info.pModel == m_Game->m_ArmsModel)
	{
		m_Game->m_ArmsMaterial->SetMaterialVarFlag(MATERIAL_VAR_NO_DRAW, true);
		m_Game->m_ModelRender->ForcedMaterialOverride(m_Game->m_ArmsMaterial);
		hkDrawModelExecute.fOriginal(ecx, state, info, pCustomBoneToWorld);
		m_Game->m_ModelRender->ForcedMaterialOverride(NULL);
		return;
	}

	hkDrawModelExecute.fOriginal(ecx, state, info, pCustomBoneToWorld);
}

void Hooks::dPushRenderTargetAndViewport(void *ecx, void *edx, ITexture *pTexture, ITexture *pDepthTexture, int nViewX, int nViewY, int nViewW, int nViewH)
{
	const bool inPaint = m_VguiPaintActive;
	const bool published = Portal2VRRuntime::IsPublished(g_Game, m_Game);
	const bool redirect = published && m_HudCaptureRoute.AllowsRedirect() &&
		HudCapture::ShouldRedirectTarget(m_VR->m_Config.experimentalHudOverlay,
			m_Game->m_Hooks->m_HudCaptureHooksReady, m_VR->m_CreatedVRTextures,
			inPaint, m_Game->m_VguiSurface->IsCursorVisible(), m_PushedHud) &&
		m_VR->m_HUDTexture && m_VR->m_VKHUD.m_VRTexture.handle;
	if (inPaint) {
		++m_HudPushDepth;
		m_HudPushSeenDuringPaint = true;
	}
	if (published && m_VR->m_Config.experimentalHudOverlay &&
		m_Game->m_EngineClient->IsInGame()) {
		const auto event = inPaint ? RenderDiagnosticEvent::HudPushInPaint :
			RenderDiagnosticEvent::HudPushOutsidePaint;
		if (m_VR->m_RenderDiagnostics.First(event))
			Logger::Write(std::string("Experimental HUD render-target push: inEligiblePaint=") +
				std::to_string(inPaint) + " redirect=" + std::to_string(redirect) +
				" alreadyRedirected=" + std::to_string(m_PushedHud) +
				" hudTargetReady=" + std::to_string(m_VR->m_HUDTexture != nullptr &&
					m_VR->m_VKHUD.m_VRTexture.handle != nullptr) +
				" sourceTarget=" + std::to_string(pTexture != nullptr) +
				" viewport=" + std::to_string(nViewW) + "x" + std::to_string(nViewH));
	}
	if (redirect)
	{
		m_HudRedirectSeenDuringPaint = true;
		if (m_VR->m_RenderDiagnostics.First(RenderDiagnosticEvent::HudRedirected))
			Logger::Write("Experimental HUD render-target redirect reached (painted pixels unverified)");
		pTexture = m_VR->m_HUDTexture;
		hkPushRenderTargetAndViewport.fOriginal(ecx, pTexture, pDepthTexture, nViewX, nViewY, nViewW, nViewH);
		IMatRenderContext *renderContext = m_Game->m_MaterialSystem->GetRenderContext();
		renderContext->OverrideAlphaWriteEnable(true, true);
		renderContext->ClearColor4ub(0, 0, 0, 0);
		renderContext->ClearBuffers(true, false);
		renderContext->Release();

		m_VR->m_RenderedHud = true;
		m_PushedHud = true;
		m_HudTargetActive = true;
	}
	else
	{
		hkPushRenderTargetAndViewport.fOriginal(ecx, pTexture, pDepthTexture, nViewX, nViewY, nViewW, nViewH);
	}
}

void Hooks::dPopRenderTargetAndViewport(void *ecx, void *edx)
{
	if (m_VguiPaintActive && !HudCapture::ShouldForwardPaintPop(
		m_ExplicitHudCaptureActive, m_HudPushDepth)) {
		m_HudUnexpectedPopDuringPaint = true;
		return;
	}
	if (m_VguiPaintActive && m_HudPushDepth > 0)
		--m_HudPushDepth;
	if (m_HudTargetActive && m_HudPushDepth == 0)
	{
		IMatRenderContext* renderContext = m_Game->m_MaterialSystem->GetRenderContext();
		renderContext->OverrideAlphaWriteEnable(false, false);
		renderContext->ClearColor4ub(0, 0, 0, 255);
		renderContext->Release();
		m_HudTargetActive = false;
	}

	hkPopRenderTargetAndViewport.fOriginal(ecx);
}

void Hooks::dVGui_Paint(void *ecx, void *edx, int mode)
{
	if (!Portal2VRRuntime::IsPublished(g_Game, m_Game) ||
		!m_VR->m_Config.experimentalHudOverlay)
		return hkVgui_Paint.fOriginal(ecx, mode);

	const bool inGame = m_Game->m_EngineClient->IsInGame();
	const bool cursorVisible = m_Game->m_VguiSurface->IsCursorVisible();
	const bool targetReady = m_VR->m_HUDTexture && m_VR->m_VKHUD.m_VRTexture.handle;
	const bool capture = HudCapture::CanCapturePaint(true, m_Game->m_Hooks->m_HudCaptureHooksReady,
		m_VR->m_CreatedVRTextures, targetReady, m_VR->m_RenderedNewFrame,
		inGame, cursorVisible);
	auto logPaintState = [&](const char *phase) {
		Logger::Write(std::string("Experimental HUD VGui_Paint ") + phase +
			": mode=" + std::to_string(mode) + " inGame=" + std::to_string(inGame) +
			" cursor=" + std::to_string(cursorVisible) +
			" hooks=" + std::to_string(m_Game->m_Hooks->m_HudCaptureHooksReady) +
			" textures=" + std::to_string(m_VR->m_CreatedVRTextures) +
			" target=" + std::to_string(targetReady) +
			" stereoFrame=" + std::to_string(m_VR->m_RenderedNewFrame));
	};
	if (m_VR->m_RenderDiagnostics.First(RenderDiagnosticEvent::HudPaintEntered))
		logPaintState("first call");
	if (inGame && m_VR->m_RenderDiagnostics.First(RenderDiagnosticEvent::HudPaintInGame))
		logPaintState("first in-game call");
	if (!capture)
		return hkVgui_Paint.fOriginal(ecx, mode);
	if (m_VR->m_RenderDiagnostics.First(RenderDiagnosticEvent::HudPaintEligible))
		logPaintState("first eligible call");
	const bool explicitCapture = m_HudCaptureRoute.ShouldCaptureExplicitly(
		capture, (mode & PAINT_UIPANELS) != 0, m_VR->m_RenderedHud);
	IMatRenderContext *captureContext = nullptr;
	if (explicitCapture) {
		captureContext = m_Game->m_MaterialSystem->GetRenderContext();
		if (!captureContext) {
			if (m_VR->m_RenderDiagnostics.First(RenderDiagnosticEvent::HudExplicitContextUnavailable))
				Logger::Write("Experimental HUD: explicit capture skipped; render context unavailable");
			return hkVgui_Paint.fOriginal(ecx, mode | PAINT_UIPANELS | PAINT_INGAMEPANELS);
		}
	}

	m_HudPushSeenDuringPaint = false;
	m_HudRedirectSeenDuringPaint = false;
	m_HudUnexpectedPopDuringPaint = false;
	m_ExplicitHudCaptureActive = captureContext != nullptr;
	m_VguiPaintActive = true;
	if (captureContext) {
		// The observed post-stereo UI paint has no nested target push. Bracket
		// that paint using the already-resolved six-argument Source ABI.
		hkPushRenderTargetAndViewport.fOriginal(captureContext, m_VR->m_HUDTexture,
			nullptr, 0, 0, m_VR->m_RenderWidth, m_VR->m_RenderHeight);
		captureContext->OverrideAlphaWriteEnable(true, true);
		captureContext->ClearColor4ub(0, 0, 0, 0);
		captureContext->ClearBuffers(true, false);
	}
	mode |= PAINT_UIPANELS | PAINT_INGAMEPANELS;
	hkVgui_Paint.fOriginal(ecx, mode);
	if (captureContext) {
		const unsigned unmatchedNestedPushes = m_HudPushDepth;
		HudCapture::UnwindNestedTargets(m_HudPushDepth, [&] {
			hkPopRenderTargetAndViewport.fOriginal(captureContext);
		});
		captureContext->OverrideAlphaWriteEnable(false, false);
		captureContext->ClearColor4ub(0, 0, 0, 255);
		hkPopRenderTargetAndViewport.fOriginal(captureContext);
		m_ExplicitHudCaptureActive = false;
		captureContext->Release();
		if (m_HudCaptureRoute.ObserveExplicitPaint(
			m_HudPushSeenDuringPaint || m_HudUnexpectedPopDuringPaint)) {
			m_VR->m_RenderedHud = false;
			if (m_VR->m_RenderDiagnostics.First(RenderDiagnosticEvent::HudExplicitUnexpectedPush))
				Logger::Write("Experimental HUD: unexpected target stack operation in explicit paint; capture disabled until restart (unmatched pushes unwound=" +
					std::to_string(unmatchedNestedPushes) + ", unmatched pop blocked=" +
					std::to_string(m_HudUnexpectedPopDuringPaint) + ")");
		} else {
			m_VR->m_RenderedHud = true;
			if (m_VR->m_RenderDiagnostics.First(RenderDiagnosticEvent::HudExplicitCapture))
				Logger::Write("Experimental HUD: explicit UI paint captured to vrHUD (pixels/alpha unverified)");
		}
	}
	m_VguiPaintActive = false;
	if (!captureContext && !m_HudRedirectSeenDuringPaint &&
		m_VR->m_RenderDiagnostics.First(RenderDiagnosticEvent::HudPaintNoRedirect))
		Logger::Write("Experimental HUD: eligible VGui_Paint returned without redirect; "
			"render-target push during paint=" + std::to_string(m_HudPushSeenDuringPaint));
	if (!captureContext && m_HudCaptureRoute.ObserveRedirectPaint(
		m_HudPushSeenDuringPaint, m_HudRedirectSeenDuringPaint))
		Logger::Write("Experimental HUD: no nested VGUI target push; explicit UI capture armed");
	if (m_HudPushDepth || m_HudTargetActive) {
		if (m_HudTargetActive) {
			IMatRenderContext* context = m_Game->m_MaterialSystem->GetRenderContext();
			context->OverrideAlphaWriteEnable(false, false);
			context->ClearColor4ub(0, 0, 0, 255);
			context->Release();
		}
		Logger::Write("Experimental HUD: VGUI render target stack was unbalanced; capture disabled until restart");
		m_Game->m_Hooks->m_HudCaptureHooksReady = false;
		m_HudPushDepth = 0;
		m_HudTargetActive = false;
		m_PushedHud = false;
		m_VR->m_RenderedHud = false;
	}
}

int Hooks::dIsSplitScreen()
{
	//std::cout << "dIsSplitScreen: " << m_PushHUDStep << "\n";

	if (m_PushHUDStep == 0)
		++m_PushHUDStep;
	else
		m_PushHUDStep = -999;

	return hkIsSplitScreen.fOriginal();
}

DWORD *Hooks::dPrePushRenderTarget(void *ecx, void *edx, int a2)
{
	//std::cout << "dPrePushRenderTarget: " << m_PushHUDStep << "\n";

	if (m_PushHUDStep == 1)
		++m_PushHUDStep;
	else
		m_PushHUDStep = -999;

	return hkPrePushRenderTarget.fOriginal(ecx, a2);
}

Vector* Hooks::dWeapon_ShootPosition(void* ecx, void* edx, Vector* eyePos)
{
	Vector* result = hkWeapon_ShootPosition.fOriginal(ecx, eyePos);

	int localIndex = m_Game->m_EngineClient->GetLocalPlayer();
	int index = EntityIndex(ecx);

	auto vrPlayer = m_Game->m_PlayersVRInfo[index];

	if (m_VR->m_IsVREnabled && m_VR->m_TrackingOutputValid && m_VR->m_RightControllerPose.valid && localIndex == index) {
		*result = m_VR->GetRightControllerAbsPos();	
	}
	else if (vrPlayer.isUsingVR)
	{
		*result = vrPlayer.controllerPos;
	}

	return result;
}

void* Hooks::dCWeaponPortalgun_FirePortal(void* ecx, void* edx, bool bPortal2, Vector* pVector) {
	bool wasTrue = m_VR->m_OverrideEyeAngles;
	const int localIndex = m_Game->m_EngineClient ?
		m_Game->m_EngineClient->GetLocalPlayer() : -1;
	void *owner = GetOwner(ecx);
	const bool localShot = Haptics::IsLocalShot(localIndex, owner ? EntityIndex(owner) : -1);

	m_VR->m_OverrideEyeAngles = true;

	auto result = hkCWeaponPortalgun_FirePortal.fOriginal(ecx, bPortal2, pVector);
	// This is a candidate fire event, not proof of successful portal placement.
	// Queue on the local weapon owner only; VR API calls stay on the update thread.
	if (localShot)
		m_VR->QueuePortalShotHaptic();

	if (!wasTrue)
		m_VR->m_OverrideEyeAngles = false;

	return result;
}

bool __fastcall Hooks::dTraceFirePortal(void* ecx, void* edx, const Vector& vTraceStart, const Vector& vDirection, bool bPortal2, int iPlacedBy, void* tr) //trace_tx& tr, Vector& vFinalPosition //  , Vector& vFinalPosition, QAngle& qFinalAngles, int iPlacedBy, bool bTest /*= false*/
{
	Vector vNewTraceStart = vTraceStart;
	Vector vNewDirection = vDirection;

	if (iPlacedBy == 2) {
		int localIndex = m_Game->m_EngineClient->GetLocalPlayer();

		auto owner = GetOwner(ecx);

		if (owner) {
			int index = EntityIndex(owner);

			auto vrPlayer = m_Game->m_PlayersVRInfo[index];

			if (m_VR->m_IsVREnabled && m_VR->m_TrackingOutputValid && m_VR->m_RightControllerPose.valid && localIndex == index) {
				vNewTraceStart = m_VR->GetRightControllerAbsPos();
				vNewDirection = m_VR->m_RightControllerForward;
			}
			else if (vrPlayer.isUsingVR)
			{
				vNewTraceStart = vrPlayer.controllerPos;
				Vector fwd, rt, up;
				QAngle::AngleVectors(vrPlayer.controllerAngle, &fwd, &rt, &up);
				vNewDirection = fwd;
			}
		}
	}

	return hkTraceFirePortal.fOriginal(ecx, vNewTraceStart, vNewDirection, bPortal2, iPlacedBy, tr);
}

void __fastcall Hooks::dPlayerPortalled(void* ecx, void* edx, void* a2, __int64 a3)
{
	CBaseEntity* pBaseEntity = (CBaseEntity*)ecx;
	const int playerIndex = m_Game->m_EngineClient->GetLocalPlayer();
	const bool localPlayer = playerIndex > 0 &&
		m_Game->GetClientEntity(playerIndex) == pBaseEntity;
	const bool experimental = m_VR->ExperimentalPortalOrientation();
	const auto portalRotation = experimental && localPlayer ?
		ReadPortalRotation(a2) : std::nullopt;

	QAngle angAbsRotationBefore;
	m_Game->m_EngineClient->GetViewAngles(angAbsRotationBefore);

	hkPlayerPortalled.fOriginal(ecx, a2, a3);

	QAngle angAbsRotationAfter;
	m_Game->m_EngineClient->GetViewAngles(angAbsRotationAfter);

	if (experimental && localPlayer) {
		m_VR->QueuePortalTraversal(reinterpret_cast<std::uintptr_t>(pBaseEntity),
			reinterpret_cast<std::uintptr_t>(a2), portalRotation);
	} else if (!experimental && angAbsRotationBefore != angAbsRotationAfter) {
		m_VR->m_PortalRotationOffset = angAbsRotationAfter - angAbsRotationBefore;
		m_VR->m_ApplyPortalRotationOffset = true;
	}

	return;
}

int Hooks::dGetModeHeight(void* ecx, void* edx) {
	//std::cout << "dGetModeHeight\n";
	return m_VR->m_RenderHeight;
}

bool Hooks::ScreenTransform(const Vector& point, Vector* pScreen, int width, int height)
{
	bool retval = ClipTransform(point, pScreen);

	pScreen->x = 0.5f * (pScreen->x + 1.0f) * width;
	pScreen->y = 0.5f * (-pScreen->y + 1.0f) * height;

	return retval;
}

int __fastcall Hooks::dDrawSelf(void* ecx, void* edx, int x, int y, int w, int h, const void* clr, float flApparentZ) {
	//std::cout << "dDrawSelf - X: " << x << ", Y: " << y << ", W: " << w << ", H: " << h << ", Z: " << flApparentZ << "\n";

	//int playerIndex = m_Game->m_EngineClient->GetLocalPlayer();

	//auto viewport = m_Game->m_ClientMode->GetViewport();

	int newX = x;
	int	newY = y;

	if (m_VR->m_IsVREnabled && m_VR->m_TrackingOutputValid &&
		m_VR->m_RightControllerPose.valid)
	{
		int windowWidth, windowHeight;
		IMatRenderContext* context = m_Game->m_MaterialSystem->GetRenderContext();
		context->GetWindowSize(windowWidth, windowHeight);
		context->Release();

		Vector screen = { 0, 0, 0 };

		//Vector vec = m_VR->m_AimPos - m_VR->GetRightControllerAbsPos();

		//newZ = 1.0 / sqrt(vec.x * vec.x + vec.y * vec.y + vec.z * vec.z);

		const bool clipTransformResult = ScreenTransform(m_VR->m_AimPos, &screen,
			m_VR->m_RenderWidth, m_VR->m_RenderHeight);
		const bool drawingToHud = m_ExplicitHudCaptureActive || m_HudTargetActive;
		const auto projected = AimFeedback::ProjectedCrosshairPosition(
			clipTransformResult, screen.x, screen.y, x, y, windowWidth, windowHeight,
			m_VR->m_RenderWidth, m_VR->m_RenderHeight,
			drawingToHud ? windowWidth : static_cast<int>(m_VR->m_RenderWidth),
			drawingToHud ? windowHeight : static_cast<int>(m_VR->m_RenderHeight));
		if (!projected) {
			if (Portal2VRRuntime::IsPublished(g_Game, m_Game) &&
				m_Game->m_EngineClient->IsInGame() &&
				m_VR->m_RenderDiagnostics.First(RenderDiagnosticEvent::CrosshairTransformTrue))
				Logger::Write("Crosshair DrawSelf: projection outside VR viewport; draw skipped (clip=" +
					std::to_string(clipTransformResult) + ")");
			return 0;
		}
		newX = projected->x;
		newY = projected->y;
		if (Portal2VRRuntime::IsPublished(g_Game, m_Game) &&
			m_Game->m_EngineClient->IsInGame() &&
			m_VR->m_RenderDiagnostics.First(drawingToHud ?
				RenderDiagnosticEvent::CrosshairHudDraw :
				RenderDiagnosticEvent::CrosshairTransformFalse))
			Logger::Write("Crosshair DrawSelf: ClipTransformResult=" + std::to_string(clipTransformResult) +
				" hudTarget=" + std::to_string(drawingToHud) +
				" source=" + std::to_string(x) + "," + std::to_string(y) +
				" output=" + std::to_string(newX) + "," + std::to_string(newY) +
				" target=" + std::to_string(screen.x) + "," + std::to_string(screen.y) +
				" window=" + std::to_string(windowWidth) + "x" + std::to_string(windowHeight) +
				" vr=" + std::to_string(m_VR->m_RenderWidth) + "x" +
				std::to_string(m_VR->m_RenderHeight));
	}

	return hkDrawSelf.fOriginal(ecx, newX, newY, w, h, clr, flApparentZ);
}

void __cdecl Hooks::dVGui_GetHudBounds(int slot, int& x, int& y, int& w, int& h) {
	if (m_VR->m_IsVREnabled && !m_Game->m_VguiSurface->IsCursorVisible())
	{
		x = y = 0;
		w = m_VR->m_RenderWidth;
		h = m_VR->m_RenderHeight;
	} else {
		hkVGui_GetHudBounds.fOriginal(slot, x, y, w, h);
	}

	//std::cout << "dVGui_GetHudBounds - X: " << x << ", Y: " << y << ", W: " << w << ", H: " << h << "\n";
}

void __cdecl Hooks::dVGui_GetPanelBounds(int slot, int& x, int& y, int& w, int& h) {
	if (m_VR->m_IsVREnabled && !m_Game->m_VguiSurface->IsCursorVisible())
	{
		x = y = 0;
		w = m_VR->m_RenderWidth;
		h = m_VR->m_RenderHeight;
	}
	else {
		hkVGui_GetPanelBounds.fOriginal(slot, x, y, w, h);
	}

	//std::cout << "dVGui_GetPanelBounds - X: " << x << ", Y: " << y << ", W: " << w << ", H: " << h << "\n";
}

void __cdecl Hooks::dVGUI_UpdateScreenSpaceBounds(int nNumSplits, int sx, int sy, int sw, int sh) {
	hkVGUI_UpdateScreenSpaceBounds.fOriginal(nNumSplits, sx, sy, m_VR->m_RenderWidth, m_VR->m_RenderHeight);
}

void __cdecl Hooks::dVGui_GetTrueScreenSize(int &w, int &h) {
	w = m_VR->m_RenderWidth;
	h = m_VR->m_RenderHeight;
}

void __fastcall Hooks::dGetScreenSize(void* ecx, void* edx, int& wide, int& tall) {
	//hkGetScreenSize.fOriginal(ecx, wide, tall);
	wide = m_VR->m_RenderWidth;
	tall = m_VR->m_RenderHeight;
}

void __cdecl Hooks::dGetHudSize(int& w, int& h) {
	w = m_VR->m_RenderWidth;
	h = m_VR->m_RenderHeight;
}

void __fastcall Hooks::dPush2DView(void* ecx, void* edx, IMatRenderContext* pRenderContext, const CViewSetup& view, int nFlags, ITexture* pRenderTarget, void* frustumPlanes) {
	m_PushedHud = false;

	return hkPush2DView.fOriginal(ecx, pRenderContext, view, nFlags, pRenderTarget, frustumPlanes);
}

void __fastcall Hooks::dRender(void* ecx, void* edx, vrect_t* rect) {
	//std::cout << "dRender - X: " << rect->x << ", Y: " << rect->y << ", W: " << rect->width << ", H: " << rect->height  << "\n";

	return hkRender.fOriginal(ecx, rect);
}

void __fastcall Hooks::dSetBounds(void* ecx, void* edx, int x, int y, int w, int h) {
	std::cout << "dSetBounds - X: " << x << ", Y: " << y << ", W: " << w << ", H: " << h << "\n";

	hkSetBounds.fOriginal(ecx, x, y, m_VR->m_RenderWidth, m_VR->m_RenderHeight);
}

void __fastcall Hooks::dSetSize(void* ecx, void* edx, int wide, int tall) {
	hkSetSize.fOriginal(ecx, wide, tall);

	//std::cout << "dSetSize - Wide: " << wide << ", Tall: " << tall  << "\n";
}

void __fastcall Hooks::dGetClipRect(void* ecx, void* edx, int& x0, int& y0, int& x1, int& y1) {
	hkGetClipRect.fOriginal(ecx, x0, y0, x1, y1);

	//std::cout << "dGetClipRect - X: " << x0 << ", Y: " << y0 << ", W: " << x1 << ", H: " << y1  << "\n";
}

double __fastcall Hooks::dComputeError(void* ecx, void* edx) {
	bool wasTrue = m_VR->m_OverrideEyeAngles;

	m_VR->m_OverrideEyeAngles = true;

	double computedError = hkComputeError.fOriginal(edx);

	if (!wasTrue)
		m_VR->m_OverrideEyeAngles = false;

	return computedError;
}

bool __fastcall Hooks::dUpdateObject(void* ecx, void* edx, void* pPlayer, float flError, bool bIsTeleport) {
	bool wasTrue = m_VR->m_OverrideEyeAngles;

	m_VR->m_OverrideEyeAngles = true;

	bool value = hkUpdateObject.fOriginal(ecx, pPlayer, flError, bIsTeleport);

	if (!wasTrue)
		m_VR->m_OverrideEyeAngles = false;

	return value;
}

bool __fastcall Hooks::dUpdateObjectVM(void* ecx, void* edx, void* pPlayer, float flError) {
	bool wasTrue = m_VR->m_OverrideEyeAngles;

	m_VR->m_OverrideEyeAngles = true;

	bool value = hkUpdateObjectVM.fOriginal(ecx, pPlayer, flError);

	if (!wasTrue)
		m_VR->m_OverrideEyeAngles = false;

	return value;
}

// This function is apparently not used by Portal 2, remove?
void __fastcall Hooks::dRotateObject(void* ecx, void* edx, void* pPlayer, float fRotAboutUp, float fRotAboutRight, bool bUseWorldUpInsteadOfPlayerUp) {
	bool wasTrue = m_VR->m_OverrideEyeAngles;

	m_VR->m_OverrideEyeAngles = true;

	hkRotateObject.fOriginal(ecx, pPlayer, fRotAboutUp, fRotAboutRight, bUseWorldUpInsteadOfPlayerUp);

	if (!wasTrue)
		m_VR->m_OverrideEyeAngles = false;
}

// This is CPlayerBase, do we also need to hook CPortalPlayer? can the same function be used by both?
// This works for release, but why was it crashing before??? TODO: buy a c++ book...
QAngle& __fastcall Hooks::dEyeAngles(void* ecx, void* edx) {
	if (m_VR->m_OverrideEyeAngles) {
		int localIndex = m_Game->m_EngineClient->GetLocalPlayer();
		int index = EntityIndex(ecx);

		auto& vrPlayer = m_Game->m_PlayersVRInfo[index];

		if (m_VR->m_IsVREnabled && m_VR->m_TrackingOutputValid && m_VR->m_RightControllerPose.valid && localIndex == index) {
			return m_VR->GetRightControllerAbsAngleConst();
		}
		else if (vrPlayer.isUsingVR)
		{
			return vrPlayer.controllerAngle;
		}
	}

	return hkEyeAngles.fOriginal(ecx);
}

int __fastcall Hooks::dGetDefaultFOV(void* ecx, void* edx) {
	return m_VR->m_Fov;
}

double __fastcall Hooks::dGetFOV(void* ecx, void* edx) {
	return m_VR->m_Fov;
}

double __fastcall Hooks::dGetViewModelFOV(void* ecx, void* edx) {
	return m_VR->m_Fov;
}
