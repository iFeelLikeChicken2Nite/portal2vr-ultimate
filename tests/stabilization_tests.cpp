#include "../L4D2VR/digital_input.h"
#include "../L4D2VR/tracked_device.h"
#include "../L4D2VR/sigscanner.h"
#include "../L4D2VR/config.h"
#include "../L4D2VR/tracking_space.h"
#include "../L4D2VR/roomscale_motion.h"
#include "../L4D2VR/portal_orientation.h"
#include "../L4D2VR/ui_input.h"
#include "../L4D2VR/haptics.h"
#include "../L4D2VR/viewport_readiness.h"
#include "../L4D2VR/render_target_readiness.h"
#include "../L4D2VR/runtime_publication.h"
#include "../L4D2VR/render_diagnostics.h"
#include "../L4D2VR/menu_overlay_placement.h"
#include "../L4D2VR/hud_capture.h"
#include "../L4D2VR/aim_feedback.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>

static int failures = 0;

struct TestViewportEngine
{
    bool inGame = false;
    int calls = 0;
    bool IsInGame() { ++calls; return inGame; }
};

struct TestViewportGame
{
    TestViewportEngine *m_EngineClient = nullptr;
    struct TestViewportVr *m_VR = nullptr;
};

struct TestViewportVr
{
    bool m_IsInitialized = false;
};

struct TestPublishedGame
{
    bool m_Initialized = false;
};

static void expectCommand(const char* actual, const char* expected, const char* caseName)
{
    if ((actual == nullptr) != (expected == nullptr) ||
        (actual && std::strcmp(actual, expected) != 0)) {
        std::cerr << caseName << " failed\n";
        ++failures;
    }
}

static void expect(bool actual, bool expected, const char* caseName)
{
    if (actual != expected) {
        std::cerr << caseName << " failed\n";
        ++failures;
    }
}

static void expectNear(float actual, float expected, const char* caseName)
{
    if (!std::isfinite(actual) || std::fabs(actual - expected) > 0.0001f) {
        std::cerr << caseName << " failed: " << actual << " != " << expected << "\n";
        ++failures;
    }
}

static void expectVectorNear(const Vector &actual, const Vector &expected, const char* caseName)
{
    expectNear(actual.x, expected.x, caseName);
    expectNear(actual.y, expected.y, caseName);
    expectNear(actual.z, expected.z, caseName);
}

int main()
{
    std::atomic<TestPublishedGame*> publishedGame{nullptr};
    TestPublishedGame initializingGame;
    expect(Portal2VRRuntime::IsPublished(publishedGame, &initializingGame), false,
           "render hook bypasses VR before game publication");
    expect(Portal2VRRuntime::PublishInitialized(publishedGame, &initializingGame), false,
           "partially initialized game is not published to renderer");
    expect(publishedGame.load() == nullptr, true,
           "renderer sees no game after initialization failure");
    initializingGame.m_Initialized = true;
    expect(Portal2VRRuntime::PublishInitialized(publishedGame, &initializingGame), true,
           "initialized game is published to renderer");
    expect(publishedGame.load() == &initializingGame, true,
           "renderer sees the initialized game");
    expect(Portal2VRRuntime::IsPublished(publishedGame, &initializingGame), true,
           "render hook may use VR after game publication");

    RenderTargetReadiness targets{{true, true, true}, {true, true, true},
                                  {true, true, true}};
    expect(targets.Ready(), true, "complete stereo and menu targets are ready");
    targets.left.shared = false;
    expect(targets.Ready(), false, "missing left Vulkan share blocks stereo");
    targets.left.shared = true;
    targets.right.surface = false;
    expect(targets.Ready(), false, "missing right D3D surface blocks stereo");
    targets.right.surface = true;
    targets.blank.texture = false;
    expect(targets.Ready(), false, "missing menu texture blocks target readiness");

    expect(Portal2VRViewport::CanUseRecommendedVRSize(false, 1920, 1080), false,
           "failed OpenVR initialization preserves game backbuffer size");
    expect(Portal2VRViewport::CanUseRecommendedVRSize(true, 0, 1080), false,
           "zero OpenVR width preserves game backbuffer size");
    expect(Portal2VRViewport::CanUseRecommendedVRSize(true, 1920, 0), false,
           "zero OpenVR height preserves game backbuffer size");
    expect(Portal2VRViewport::CanUseRecommendedVRSize(true, 1920, 1080), true,
           "valid OpenVR size may override game backbuffer size");

    RenderDiagnosticGate renderDiagnostics;
    expect(renderDiagnostics.First(RenderDiagnosticEvent::MenuSubmission), true,
           "first menu submission is logged");
    expect(renderDiagnostics.First(RenderDiagnosticEvent::MenuSubmission), false,
           "repeated menu submissions are not logged");
    expect(renderDiagnostics.First(RenderDiagnosticEvent::StereoSubmission), true,
           "first stereo submission is logged independently");
    expect(renderDiagnostics.First(RenderDiagnosticEvent::StereoSubmission), false,
           "repeated stereo submissions are not logged");

    MenuOverlayPlacement menuPlacement;
    expect(menuPlacement.ShouldAttempt(false, false), false,
           "menu cannot be positioned before the first valid HMD pose");
    expect(menuPlacement.ShouldAttempt(true, true), true,
           "visible menu retries placement after HMD pose becomes valid");
    menuPlacement.RecordResult(false);
    expect(menuPlacement.ShouldAttempt(true, true), true,
           "failed menu placement is retried while visible");
    menuPlacement.RecordResult(true);
    expect(menuPlacement.ShouldAttempt(true, true), false,
           "positioned visible menu does not move on every frame");
    expect(menuPlacement.ShouldAttempt(false, true), true,
           "hidden menu is positioned again before returning");

    TestViewportEngine viewportEngine;
    TestViewportGame viewportGame;
    TestViewportVr viewportVr;
    expect(Portal2VRViewport::CanOverrideMenuViewport<TestViewportGame>(nullptr), false,
           "viewport override skips absent game");
    expect(Portal2VRViewport::CanOverrideMenuViewport(&viewportGame), false,
           "viewport override skips game before engine interface resolves");
    viewportGame.m_EngineClient = &viewportEngine;
    expect(Portal2VRViewport::CanOverrideMenuViewport(&viewportGame), false,
           "viewport override skips game before VR is initialized");
    expect(viewportEngine.calls == 0, true,
           "viewport override never queries engine during partial initialization");
    viewportGame.m_VR = &viewportVr;
    expect(Portal2VRViewport::HasInitializedVR(&viewportGame), false,
           "DXVK reset skips an incomplete VR object");
    expect(Portal2VRViewport::CanOverrideMenuViewport(&viewportGame), false,
           "viewport override skips VR object before initialization completes");
    viewportVr.m_IsInitialized = true;
    expect(Portal2VRViewport::HasInitializedVR(&viewportGame), true,
           "DXVK reset may use dimensions after VR initialization completes");
    expect(Portal2VRViewport::CanOverrideMenuViewport(&viewportGame), true,
           "viewport override remains active in menu once dependencies are ready");
    viewportEngine.inGame = true;
    expect(Portal2VRViewport::CanOverrideMenuViewport(&viewportGame), false,
           "viewport override stays off during gameplay");

    const VMatrix quarterTurn{0,-1,0,12, 1,0,0,-5, 0,0,1,3, 0,0,0,1};
    const auto turn = PortalOrientation::Rotation::FromVMatrix(quarterTurn);
    expect(turn.has_value(), true, "rigid translated portal matrix accepted");
    if (turn) {
        expectVectorNear(turn->Rotate({1,0,0}), {0,1,0}, "90 degree portal rotation uses Source axes");
        expectVectorNear(turn->Inverse().Rotate(turn->Rotate({2,3,4})), {2,3,4},
                         "portal transform followed by inverse restores direction");
        const auto twice = turn->Compose(*turn);
        expectVectorNear(twice.Rotate({1,0,0}), {-1,0,0}, "two 90 degree crossings compose to 180");
        expectVectorNear(turn->Rotate({0.3f,0.1f,1.6f}) - turn->Rotate({0.1f,0.1f,1.6f}),
                         {0,0.2f,0}, "head and controller share relative portal rotation");
        expectVectorNear(turn->YawOnly().Rotate({1,0,0}), {0,1,0},
                         "yaw-only wall portal preserves horizontal heading");
    }
    const VMatrix floorExit{0,0,-1,0, 0,1,0,0, 1,0,0,0, 0,0,0,1};
    const auto floorTurn = PortalOrientation::Rotation::FromVMatrix(floorExit);
    expect(floorTurn.has_value(), true, "floor portal pitch matrix accepted");
    if (floorTurn) {
        expectVectorNear(floorTurn->Rotate({1,0,0}), {0,0,1},
                         "wall-to-floor portal can point forward vertically");
        expectVectorNear(floorTurn->YawOnly().Rotate({1,0,0}), {1,0,0},
                         "vertical-forward yaw fallback uses portal left axis");
        expectVectorNear(floorTurn->PreserveHorizon().Rotate({1,0,0}), {0,0,1},
                         "horizon mode retains floor-portal pitch");
    }
    const VMatrix rollPortal{1,0,0,0, 0,0,-1,0, 0,1,0,0, 0,0,0,1};
    const auto rollTurn = PortalOrientation::Rotation::FromVMatrix(rollPortal);
    expect(rollTurn.has_value(), true, "rolled portal matrix accepted");
    if (rollTurn)
        expectVectorNear(rollTurn->PreserveHorizon().Rotate({0,0,1}), {0,0,1},
                         "horizon mode removes portal-induced roll without head tracking input");
    const VMatrix reflected{-1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    expect(PortalOrientation::Rotation::FromVMatrix(reflected).has_value(), false,
           "reflected portal matrix is not a rigid rotation");
    const VMatrix scaled{2,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    expect(PortalOrientation::Rotation::FromVMatrix(scaled).has_value(), false,
           "scaled portal matrix is rejected");
    PortalOrientation::Coordinator portalEvents;
    expect(portalEvents.Queue(1, 10, *turn) == PortalOrientation::QueueResult::Disabled,
           true, "legacy default does not consume experimental portal events");
    portalEvents.SetMode(PortalOrientation::Mode::FullRotation);
    expect(portalEvents.Queue(1, 10, *turn) == PortalOrientation::QueueResult::Queued,
           true, "first local portal event queued");
    expect(portalEvents.Queue(1, 10, *turn) == PortalOrientation::QueueResult::Duplicate,
           true, "duplicate callback in one render interval is ignored");
    const auto firstPortalFrame = portalEvents.Drain(1);
    expect(firstPortalFrame.applied == 1, true, "one portal event applied at frame boundary");
    expectVectorNear(firstPortalFrame.effective.Rotate({1,0,0}), {0,1,0},
                     "full rotation uses linked portal orientation");
    expect(portalEvents.Queue(1, 10, *turn) == PortalOrientation::QueueResult::Duplicate,
           true, "same crossing prediction replay in next frame is ignored");
    portalEvents.Queue(1, 20, turn->Inverse());
    const auto returnPortalFrame = portalEvents.Drain(1);
    expect(returnPortalFrame.applied == 1, true, "reverse crossing applies once");
    expectVectorNear(returnPortalFrame.effective.Rotate({1,0,0}), {1,0,0},
                     "crossing linked portal back restores heading");
    portalEvents.Queue(1, 10, *turn);
    portalEvents.Queue(1, 20, *turn);
    const auto consecutivePortalFrame = portalEvents.Drain(1);
    expect(consecutivePortalFrame.applied == 2, true,
           "two distinct portal crossings before one frame compose in arrival order");
    expectVectorNear(consecutivePortalFrame.effective.Rotate({1,0,0}), {-1,0,0},
                     "two queued quarter turns compose to 180 degrees");
    PortalOrientation::Coordinator rapidReturns;
    rapidReturns.SetMode(PortalOrientation::Mode::FullRotation);
    rapidReturns.Queue(1, 10, *turn);
    rapidReturns.Drain(1);
    rapidReturns.Queue(1, 20, *turn);
    rapidReturns.Queue(1, 10, *turn);
    expect(rapidReturns.Drain(1).applied == 2, true,
           "distinct intervening crossing permits quick return through prior portal");
    portalEvents.Queue(1, 30, *turn);
    const auto otherPlayerFrame = portalEvents.Drain(2);
    expect(otherPlayerFrame.applied == 0, true,
           "player replacement discards pending portal event");
    expect(otherPlayerFrame.playerChanged, true,
           "player replacement explicitly reports rig reset even without applied event");
    expectVectorNear(otherPlayerFrame.effective.Rotate({1,0,0}), {1,0,0},
                     "player replacement resets old world alignment");
    portalEvents.SetMode(PortalOrientation::Mode::YawOnly);
    portalEvents.Queue(2, 40, *floorTurn);
    const auto yawPortalFrame = portalEvents.Drain(2);
    expectVectorNear(yawPortalFrame.effective.Rotate({1,0,0}), {1,0,0},
                     "yaw-only floor crossing uses stable left-axis fallback");
    portalEvents.SetMode(PortalOrientation::Mode::PreserveHorizon);
    portalEvents.Queue(2, 50, *rollTurn);
    const auto horizonPortalFrame = portalEvents.Drain(2);
    expectVectorNear(horizonPortalFrame.effective.Rotate({0,0,1}), {0,0,1},
                     "horizon mode removes portal roll for entire rig");
    portalEvents.Queue(2, 60, *turn);
    portalEvents.CancelPending();
    expect(portalEvents.Drain(2).applied == 0, true,
           "recenter or tracking loss cancels a pending portal event");
    PortalOrientation::Coordinator resumedEvents;
    resumedEvents.SetMode(PortalOrientation::Mode::FullRotation);
    resumedEvents.Queue(1, 30, *turn);
    resumedEvents.Drain(1);
    resumedEvents.CancelPending();
    expect(resumedEvents.Queue(1, 30, *turn) == PortalOrientation::QueueResult::Queued,
           true, "focus loss clears old portal duplicate window");
    PortalOrientation::RigAnchor portalRig;
    portalRig.Reanchor({10,20,30}, {10,20,30});
    const Vector afterPortalHead = portalRig.MapHmd({11,20,30}, *turn);
    expectVectorNear(afterPortalHead, {10,21,30},
                     "head displacement after portal follows linked world heading");
    expectVectorNear(portalRig.MapRelative({0.2f,0,0}, *turn), {0,0.2f,0},
                     "hand displacement uses same portal mapping as head");
    portalRig.Reanchor({0,0,0}, {0,0,0});
    expectVectorNear(portalRig.MapHmd({0,0,0}, *turn), {0,0,0},
                     "explicit recenter resets the virtual head offset");
    expectVectorNear(portalRig.MapHmd({1,0,0}, *turn), {0,1,0},
                     "post-recenter physical delta retains portal orientation");

    UiInput::MenuPointerState menuMouse;
    expect(menuMouse.Press() == UiInput::MouseTransition::Press, true,
           "menu press creates one synthetic down event");
    menuMouse.ConfirmSent(UiInput::MouseTransition::Press, true);
    expect(menuMouse.Press() == UiInput::MouseTransition::None, true,
           "held menu selection does not repeat mouse down");
    expect(menuMouse.LoseFocus() == UiInput::MouseTransition::Release, true,
           "lost hover or menu focus releases a held synthetic click");
    menuMouse.ConfirmSent(UiInput::MouseTransition::Release, true);
    expect(menuMouse.Release() == UiInput::MouseTransition::None, true,
           "late mouse up after focus loss is ignored");
    expect(menuMouse.Press() == UiInput::MouseTransition::Press, true,
           "next menu click still works after focus recovery");
    menuMouse.ConfirmSent(UiInput::MouseTransition::Press, true);
    expect(menuMouse.Release() == UiInput::MouseTransition::Release, true,
           "ordinary release emits one up event");
    menuMouse.ConfirmSent(UiInput::MouseTransition::Release, true);
    expect(menuMouse.PendingRelease() == UiInput::MouseTransition::None, true,
           "successful mouse up leaves no pending release");

    UiInput::MenuPointerState failedMenuMouse;
    expect(failedMenuMouse.Press() == UiInput::MouseTransition::Press, true,
           "failed mouse down is attempted once");
    failedMenuMouse.ConfirmSent(UiInput::MouseTransition::Press, false);
    expect(failedMenuMouse.Release() == UiInput::MouseTransition::None, true,
           "failed mouse down does not create a phantom held click");
    expect(failedMenuMouse.Press() == UiInput::MouseTransition::Press, true,
           "a later click may retry after failed mouse down");
    failedMenuMouse.ConfirmSent(UiInput::MouseTransition::Press, true);
    expect(failedMenuMouse.Release() == UiInput::MouseTransition::Release, true,
           "mouse up is requested after a delivered down");
    failedMenuMouse.ConfirmSent(UiInput::MouseTransition::Release, false);
    expect(failedMenuMouse.PendingRelease() == UiInput::MouseTransition::Release, true,
           "failed mouse up remains pending for next frame");
    expect(failedMenuMouse.Press() == UiInput::MouseTransition::None, true,
           "new down is blocked until pending mouse up succeeds");
    failedMenuMouse.ConfirmSent(failedMenuMouse.PendingRelease(), true);
    expect(failedMenuMouse.PendingRelease() == UiInput::MouseTransition::None, true,
           "successful retry clears pending mouse up");
    expect(failedMenuMouse.Press() == UiInput::MouseTransition::Press, true,
           "normal clicks resume after mouse up retry");
    const auto mainTopLeft = UiInput::MapMenuPointer(0, 1080, 1920, 1080, 1280, 720, false);
    expect(mainTopLeft && mainTopLeft->x == 0 && mainTopLeft->y == 0, true,
           "main-menu overlay top-left maps to desktop top-left");
    const auto mainBottomRight = UiInput::MapMenuPointer(1920, 0, 1920, 1080, 1280, 720, false);
    expect(mainBottomRight && mainBottomRight->x == 1279 && mainBottomRight->y == 719, true,
           "main-menu overlay edges clamp to desktop bounds");
    const auto pausePoint = UiInput::MapMenuPointer(100, 1000, 1920, 1200, 800, 600, true);
    expect(pausePoint && pausePoint->x == 100 && pausePoint->y == 200, true,
           "pause-menu crop retains existing render-texture pointer convention");
    expect(UiInput::MapMenuPointer(std::numeric_limits<float>::quiet_NaN(), 0,
                                  1920, 1080, 800, 600, false).has_value(), false,
           "nonfinite overlay coordinates cannot reach VGUI");

    RoomscaleMotion::StepAccumulator roomscale;
    roomscale.Observe(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f);
    expect(roomscale.Consume(100).has_value(), false, "first HMD sample establishes baseline");
    roomscale.Observe(true, {0.1f, 0.0f, 1.7f}, 2, 0.0f, 43.2f);
    const auto roomscaleStep = roomscale.Consume(101);
    expect(roomscaleStep.has_value(), true, "small fresh HMD step becomes one move intent");
    if (roomscaleStep)
        expectVectorNear(*roomscaleStep, {4.32f, 0.0f, 0.0f}, "physical step is horizontal Source units");
    expect(roomscale.Consume(101).has_value(), false, "same command does not repeat physical step");
    expect(roomscale.Consume(102).has_value(), false, "new command without pose does not repeat step");
    roomscale.Reset();
    roomscale.Observe(true, {1.0f, 0.0f, 1.6f}, 3, 0.0f, 43.2f);
    roomscale.Observe(true, {1.1f, 0.0f, 1.6f}, 4, 0.0f, 43.2f);
    expect(roomscale.Consume(102).has_value(), false,
           "recenter cannot replay an already consumed command number");

    RoomscaleMotion::StepAccumulator recoveredRoomscale;
    recoveredRoomscale.Observe(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f);
    recoveredRoomscale.Observe(true, {0.1f, 0.0f, 1.6f}, 2, 0.0f, 43.2f);
    expect(recoveredRoomscale.Observe(false, {}, 3, 0.0f, 43.2f) ==
           RoomscaleMotion::Observation::TrackingLost, true, "tracking loss is reported once");
    expect(recoveredRoomscale.Consume(103).has_value(), false, "tracking loss drops unconsumed step");
    expect(recoveredRoomscale.Observe(false, {}, 4, 0.0f, 43.2f) ==
           RoomscaleMotion::Observation::NoStep, true, "repeated invalid pose is quiet");
    expect(recoveredRoomscale.Observe(true, {2.0f, 0.0f, 1.6f}, 5, 0.0f, 43.2f) ==
           RoomscaleMotion::Observation::TrackingRecovered, true, "recovery rebases without teleporting");
    expect(recoveredRoomscale.Consume(104).has_value(), false, "recovery sample has no movement");

    RoomscaleMotion::StepAccumulator discontinuousRoomscale;
    discontinuousRoomscale.Observe(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f);
    discontinuousRoomscale.Observe(true, {0.1f, 0.0f, 1.6f}, 2, 0.0f, 43.2f);
    expect(discontinuousRoomscale.Observe(true, {1.1f, 0.0f, 1.6f}, 3, 0.0f, 43.2f) ==
           RoomscaleMotion::Observation::Discontinuity, true, "large tracking jump is rejected");
    expect(discontinuousRoomscale.Consume(1).has_value(), false, "jump clears earlier queued step");
    discontinuousRoomscale.Observe(true, {1.2f, 0.0f, 1.6f}, 4, 0.0f, 43.2f);
    const auto postJumpStep = discontinuousRoomscale.Consume(2);
    expect(postJumpStep.has_value(), true, "fresh step after discontinuity is accepted");
    if (postJumpStep)
        expectVectorNear(*postJumpStep, {4.32f, 0.0f, 0.0f}, "post-jump baseline is fresh");

    RoomscaleMotion::StepAccumulator changedMapping;
    changedMapping.Observe(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f);
    changedMapping.Observe(true, {0.1f, 0.0f, 1.6f}, 2, 0.0f, 43.2f);
    expect(changedMapping.Observe(true, {0.1f, 0.0f, 1.6f}, 3, 90.0f, 43.2f) ==
           RoomscaleMotion::Observation::MappingChanged, true, "artificial turn rebases pending movement");
    expect(changedMapping.Consume(1).has_value(), false, "turn is not a physical step");
    changedMapping.Observe(true, {0.1f, 0.1f, 1.6f}, 4, 90.0f, 43.2f);
    const auto turnedStep = changedMapping.Consume(2);
    expect(turnedStep.has_value(), true, "physical step after turn remains available");
    if (turnedStep)
        expectVectorNear(*turnedStep, {-4.32f, 0.0f, 0.0f}, "post-turn step uses new mapping");
    changedMapping.Reset();
    changedMapping.Observe(true, {9.0f, 9.0f, 1.6f}, 5, 90.0f, 43.2f);
    expect(changedMapping.Consume(3).has_value(), false, "recenter never generates catch-up step");

    RoomscaleMotion::StepAccumulator invalidRoomscale;
    invalidRoomscale.Observe(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f);
    expect(invalidRoomscale.Observe(true, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.6f},
                                    2, 0.0f, 43.2f) ==
           RoomscaleMotion::Observation::InvalidSample, true, "non-finite pose is rejected");
    expect(invalidRoomscale.Consume(1).has_value(), false, "invalid pose has no movement");

    RoomscaleMotion::StepAccumulator accumulatedRoomscale;
    accumulatedRoomscale.Observe(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f);
    accumulatedRoomscale.Observe(true, {0.05f, 0.0f, 1.65f}, 2, 0.0f, 43.2f);
    accumulatedRoomscale.Observe(true, {0.1f, 0.2f, 1.7f}, 3, 0.0f, 43.2f);
    accumulatedRoomscale.Observe(true, {0.2f, 0.2f, 1.7f}, 3, 0.0f, 43.2f);
    expect(accumulatedRoomscale.Consume(0).has_value(), false, "zero command does not consume physical step");
    const auto accumulatedStep = accumulatedRoomscale.Consume(1);
    expect(accumulatedStep.has_value(), true, "pose samples accumulate before command");
    if (accumulatedStep)
        expectVectorNear(*accumulatedStep, {4.32f, 8.64f, 0.0f},
                         "stale pose sequence ignored and vertical motion excluded");
    accumulatedRoomscale.Observe(true, {0.0f, 0.0f, 1.6f}, 4, 0.0f, 43.2f);
    const auto returnStep = accumulatedRoomscale.Consume(2);
    expect(returnStep.has_value(), true, "return walk produces opposite physical intent");
    if (returnStep)
        expectVectorNear(*returnStep, {-4.32f, -8.64f, 0.0f},
                         "return walk cancels open-space intent mathematically");

    const Vector openVrPosition{1.0f, 2.0f, 3.0f};
    const Vector sourceMeters = TrackingSpace::OpenVrToSourceMeters(openVrPosition);
    expectVectorNear(sourceMeters, {-3.0f, -1.0f, 2.0f}, "OpenVR to Source axes");
    expectVectorNear(TrackingSpace::SourceToOpenVrMeters(sourceMeters), openVrPosition,
                     "Source to OpenVR inverse axes");
    expectVectorNear(TrackingSpace::RotateYawDegrees({1.0f, 0.0f, 2.0f}, 90.0f),
                     {0.0f, 1.0f, 2.0f}, "yaw 90 degrees");
    expectVectorNear(TrackingSpace::RotateYawDegrees({1.0f, 0.0f, 2.0f}, 180.0f),
                     {-1.0f, 0.0f, 2.0f}, "yaw 180 degrees");

    TrackingSpace::PlayspaceState seated;
    seated.scale = 50.0f;
    seated.yawDegrees = 90.0f;
    seated.Recenter({2.0f, 3.0f, 1.0f});
    expectVectorNear(seated.HmdOffsetUnits({2.0f, 3.0f, 1.0f}, 70.0f),
                     {0.0f, 0.0f, 0.0f}, "seated recenter at nonzero yaw");
    const Vector seatedHmd{3.0f, 3.0f, 1.2f};
    expectVectorNear(seated.HmdOffsetUnits(seatedHmd, 70.0f),
                     {0.0f, 50.0f, 10.0f}, "seated HMD displacement at scale 50");
    expectVectorNear(seated.ControllerOffsetUnits({3.0f, 4.0f, 1.2f}, seatedHmd, 70.0f),
                     {-50.0f, 50.0f, 10.0f}, "controller uses HMD playspace transform");
    const auto leftOnly = seated.ControllerRelativeOffsetUnits(true,
        {3.0f, 4.0f, 1.2f}, seatedHmd, 70.0f);
    const auto rightLost = seated.ControllerRelativeOffsetUnits(false,
        {4.0f, 3.0f, 1.2f}, seatedHmd, 70.0f);
    expect(leftOnly.has_value(), true, "left position valid without right pose");
    if (leftOnly) expectVectorNear(*leftOnly, {-50.0f, 0.0f, 0.0f}, "left relative playspace offset");
    expect(rightLost.has_value(), false, "lost right position invalid independently");
    const auto rightReconnected = seated.ControllerRelativeOffsetUnits(true,
        {4.0f, 3.0f, 1.2f}, seatedHmd, 70.0f);
    expect(rightReconnected.has_value(), true, "right position recovers from fresh pose");
    if (rightReconnected) expectVectorNear(*rightReconnected,
        {0.0f, 50.0f, 0.0f}, "right relative offset after reconnect");

    TrackingSpace::PlayspaceState standing;
    standing.mode = TrackingSpace::TrackingMode::Standing;
    standing.scale = 50.0f;
    standing.heightOffsetMeters = 0.1f;
    standing.Recenter({2.0f, 3.0f, 1.7f});
    expectVectorNear(standing.HmdOffsetUnits({2.0f, 3.0f, 1.7f}, 70.0f),
                     {0.0f, 0.0f, 20.0f}, "standing floor height uses measured eye height");
    expectVectorNear(standing.HmdOffsetUnits({2.0f, 3.0f, 1.8f}, 70.0f),
                     {0.0f, 0.0f, 25.0f}, "standing HMD height change");
    const auto measuredEyeHeight = TrackingSpace::EyeHeightUnits(72.0f, 5.0f);
    expect(measuredEyeHeight.has_value(), true, "finite Source eye height available");
    if (measuredEyeHeight) expectNear(*measuredEyeHeight, 67.0f, "Source eye height difference");
    expect(TrackingSpace::EyeHeightUnits(std::numeric_limits<float>::quiet_NaN(), 5.0f).has_value(),
           false, "NaN eye position rejected");
    expect(TrackingSpace::EyeHeightUnits(72.0f, std::numeric_limits<float>::infinity()).has_value(),
           false, "infinite player origin rejected");
    const auto standingAnchor = TrackingSpace::StandingEyeAnchorUnits(1.7f, 43.2f);
    expect(standingAnchor.has_value(), true, "standing anchor accepts a tracked HMD height");
    if (standingAnchor)
        expectNear(*standingAnchor, 73.44f, "standing anchor uses the tracking height");
    expect(TrackingSpace::StandingEyeAnchorUnits(0.0f, 43.2f).has_value(), false,
           "standing anchor rejects an uncalibrated floor height");
    expect(TrackingSpace::StandingEyeAnchorUnits(1.7f, std::numeric_limits<float>::infinity()).has_value(),
           false, "standing anchor rejects an invalid scale");
    expectVectorNear(TrackingSpace::ControllerWorldOrigin({10.0f, 20.0f, 30.0f},
        {1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}, true),
        {15.0f, 27.0f, 39.0f}, "controller world origin shares the trace origin");
    expectVectorNear(TrackingSpace::ControllerWorldOrigin({10.0f, 20.0f, 30.0f},
        {1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}, false),
        {11.0f, 22.0f, 33.0f}, "controller world origin respects disabled 6DOF");

    expect(HudCapture::ShouldRedirectTarget(true, true, true, true, false, false), true,
           "opt-in VGUI paint redirects one render target");
    expect(HudCapture::ShouldRedirectTarget(false, true, true, true, false, false), false,
           "default VGUI paint leaves render targets alone");
    expect(HudCapture::ShouldRedirectTarget(true, true, true, false, false, false), false,
           "unrelated render-target pushes are not redirected");
    expect(HudCapture::ShouldRedirectTarget(true, true, true, true, true, false), false,
           "menu paint does not redirect the HUD");
    expect(HudCapture::ShouldRedirectTarget(true, true, true, true, false, true), false,
           "only the first target push per paint is redirected");
    expect(AimFeedback::ShouldRequestLaser(2, true, true, true, false), true,
           "laser request does not depend on Source crosshair paint");
    expect(AimFeedback::ShouldRequestLaser(1, true, true, true, false), false,
           "head-aim mode does not request controller laser");
    expect(AimFeedback::ShouldRequestLaser(2, true, false, true, false), false,
           "tracking loss stops controller laser requests");
    expect(AimFeedback::ShouldRequestLaser(2, true, true, true, true), false,
           "menus do not request controller laser");

    TrackingSpace::PlayspaceState turning;
    turning.scale = 50.0f;
    const Vector turnHmd{0.4f, 0.2f, 1.2f};
    expectVectorNear(turning.HmdOffsetUnits(turnHmd, 70.0f),
                     {20.0f, 10.0f, 60.0f}, "turn baseline head offset");
    turning.TurnAboutHmd(90.0f, turnHmd, 70.0f);
    expectVectorNear(turning.HmdOffsetUnits(turnHmd, 70.0f),
                     {20.0f, 10.0f, 60.0f}, "first turn holds head pivot");
    expectVectorNear(turning.ControllerOffsetUnits({0.6f, 0.2f, 1.2f}, turnHmd, 70.0f),
                     {20.0f, 20.0f, 60.0f}, "controller rotates around head");
    turning.TurnAboutHmd(90.0f, turnHmd, 70.0f);
    expectVectorNear(turning.HmdOffsetUnits(turnHmd, 70.0f),
                     {20.0f, 10.0f, 60.0f}, "repeated turns hold head pivot");
    turning.Recenter(turnHmd);
    expectVectorNear(turning.HmdOffsetUnits(turnHmd, 70.0f),
                     {0.0f, 0.0f, 0.0f}, "recenter clears turn translation");
    const Vector lastRenderedOffset{12.0f, -8.0f, 5.0f};
    turning.PreserveOffsetOnRecovery({0.8f, -0.3f, 1.1f}, lastRenderedOffset, 70.0f);
    expectVectorNear(turning.HmdOffsetUnits({0.8f, -0.3f, 1.1f}, 70.0f),
                     lastRenderedOffset, "tracking recovery holds last rendered offset");
    turning.Recenter({0.8f, -0.3f, 1.1f});
    expectVectorNear(turning.HmdOffsetUnits({0.8f, -0.3f, 1.1f}, 70.0f),
                     {0.0f, 0.0f, 0.0f}, "recenter clears recovery compensation");
    expectNear(standing.centerMeters.z, 0.0f, "standing recenter keeps floor origin");

    const TrackingSpace::DeviceDirection hmdDirection{true, {1.0f, 0.0f, 0.0f}};
    const TrackingSpace::DeviceDirection leftDirection{true, {0.0f, 1.0f, 0.5f}};
    const TrackingSpace::DeviceDirection rightDirection{true, {-1.0f, 0.0f, 0.0f}};
    const TrackingSpace::DeviceDirection lostDirection{false, {0.0f, -1.0f, 0.0f}};
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::Hmd,
                     hmdDirection, leftDirection, rightDirection), {1.0f, 0.0f, 0.0f},
                     "default HMD movement direction");
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::LeftController,
                     hmdDirection, leftDirection, rightDirection), {0.0f, 1.0f, 0.0f},
                     "left movement ignores pitch");
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::RightController,
                     hmdDirection, leftDirection, rightDirection), {-1.0f, 0.0f, 0.0f},
                     "right controller movement direction");
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::RightController,
                     hmdDirection, lostDirection, rightDirection), {-1.0f, 0.0f, 0.0f},
                     "right direction does not require left controller");
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::LeftController,
                     hmdDirection, lostDirection, rightDirection), {1.0f, 0.0f, 0.0f},
                     "lost left controller falls back to HMD");
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::RightController,
                     hmdDirection, leftDirection, {true, {0.001f, 0.0f, 1.0f}}),
                     {1.0f, 0.0f, 0.0f}, "near vertical controller falls back");
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::LeftController,
                     hmdDirection, leftDirection, lostDirection), {0.0f, 1.0f, 0.0f},
                     "left controller reconnect uses fresh pose");
    const auto defaultAxes = TrackingSpace::RebaseAnalogToView(0.25f, 0.8f,
                                                               hmdDirection.forward, hmdDirection.forward);
    expectNear(defaultAxes.forward, 0.8f, "HMD forward magnitude unchanged");
    expectNear(defaultAxes.side, 0.25f, "HMD side magnitude unchanged");
    const auto leftAxes = TrackingSpace::RebaseAnalogToView(0.25f, 0.8f,
                                                             leftDirection.forward, hmdDirection.forward);
    expectNear(leftAxes.forward, 0.25f, "left facing stick side becomes forward");
    expectNear(leftAxes.side, -0.8f, "left facing stick forward becomes side");

    DigitalButtonState attack;
    expectCommand(attack.HeldCommand(true, false, false, "+attack", "-attack"), nullptr, "released to released");
    expectCommand(attack.HeldCommand(true, true, true, "+attack", "-attack"), "+attack", "released to pressed");
    expectCommand(attack.HeldCommand(true, false, true, "+attack", "-attack"), nullptr, "pressed to pressed");
    expectCommand(attack.HeldCommand(true, true, false, "+attack", "-attack"), "-attack", "pressed to released");
    expectCommand(attack.HeldCommand(true, false, false, "+attack", "-attack"), nullptr, "released stays released");
    expectCommand(attack.HeldCommand(true, true, true, "+attack", "-attack"), "+attack", "press before tracking loss");
    expectCommand(attack.HeldCommand(false, false, false, "+attack", "-attack"), "-attack", "inactive action releases held command");
    expectCommand(attack.HeldCommand(false, false, false, "+attack", "-attack"), nullptr, "inactive action does not repeat release");

    expect(DigitalButtonState::PressEdge(true, false, false), false, "one shot released");
    expect(DigitalButtonState::PressEdge(true, true, true), true, "one shot press");
    expect(DigitalButtonState::PressEdge(true, false, true), false, "one shot hold");
    expect(DigitalButtonState::PressEdge(true, true, false), false, "one shot release");
    expect(DigitalButtonState::PressEdge(false, true, true), false, "inactive one shot");

    expect(IsUsableTrackedDeviceIndex(0, 64, 0xffffffffu), true, "HMD index is in range");
    expect(IsUsableTrackedDeviceIndex(63, 64, 0xffffffffu), true, "last tracked index is in range");
    expect(IsUsableTrackedDeviceIndex(64, 64, 0xffffffffu), false, "out of range tracked index");
    expect(IsUsableTrackedDeviceIndex(0xffffffffu, 64, 0xffffffffu), false, "invalid controller role index");

    const uint8_t image[] = { 0x11, 0x22, 0x33, 0x44 };
    expect(SigScanner::FindPattern(image, sizeof(image), { 0x33, 0x44 }) == 2, true, "signature at image end");
    expect(SigScanner::FindPattern(image, sizeof(image), { 0x33, 0x55 }) == -1, true, "signature mismatch");
    expect(SigScanner::FindPattern(image, sizeof(image), { 0x44, 0x55 }) == -1, true, "no scan beyond image");

    ConfigSnapshot previous;
    previous.turnSpeed = 0.3f;
    previous.antiAliasing = 4;
    std::istringstream malformed("TurnSpeed=bogus\nVRScale=0\nIPDScale=nan\nAimMode=9\nAntiAliasing=3\nSnapTurning=maybe\nLeftHanded=true\n");
    const auto parsed = ParseConfig(malformed, previous);
    expect(parsed.value.turnSpeed == 0.3f, true, "bad turn speed preserves previous");
    expect(parsed.value.vrScale == 43.2f, true, "out of range scale preserves previous");
    expect(parsed.value.ipdScale == 1.0f, true, "nonfinite IPD preserves previous");
    expect(parsed.value.aimMode == 2, true, "bad aim mode preserves previous");
    expect(parsed.value.antiAliasing == 4, true, "unsupported AA preserves previous");
    expect(parsed.value.snapTurning == false, true, "bad boolean preserves previous");
    expect(parsed.value.leftHanded == true, true, "other valid field accepted");
    expect(parsed.errors.size() == 6, true, "all malformed config fields reported");
    std::istringstream good("TurnSpeed = 0.5 # comment\nSnapTurning=true\nAntiAliasing=8\nViewmodelPosCustomOffsetX=-2.5\n");
    const auto valid = ParseConfig(good, previous);
    expect(valid.errors.empty(), true, "valid config has no errors");
    expect(valid.value.turnSpeed == 0.5f, true, "trimmed float accepted");
    expect(valid.value.antiAliasing == 8, true, "valid AA accepted");
    expect(valid.value.viewmodelPosOffset[0] == -2.5f, true, "viewmodel offset accepted");
    std::istringstream trailing("TurnSpeed=0.5junk\nRenderWindow=2\n");
    const auto rejected = ParseConfig(trailing, previous);
    expect(rejected.value.turnSpeed == previous.turnSpeed, true, "trailing numeric garbage rejected");
    expect(rejected.value.renderWindow == previous.renderWindow, true, "invalid render toggle rejected");

    ConfigSnapshot m2Defaults;
    expect(m2Defaults.trackingMode == TrackingSpace::TrackingMode::Seated, true,
           "legacy config defaults to seated tracking");
    expect(m2Defaults.movementDirection == TrackingSpace::MovementDirection::Hmd, true,
           "legacy config defaults to HMD locomotion");
    expectNear(m2Defaults.heightOffsetMeters, 0.0f, "default height offset");
    expectNear(m2Defaults.controllerPitchDegrees, -30.0f, "default controller pitch");
    expect(m2Defaults.roomscaleMode == RoomscaleMotion::Mode::Off, true,
           "roomscale observation defaults off");
    expect(m2Defaults.portalOrientationMode == PortalOrientation::Mode::LegacyYaw, true,
           "portal orientation keeps legacy yaw by default");
    expect(m2Defaults.experimentalHudOverlay, false,
           "unverified HUD overlay is disabled by default");
    std::istringstream hudOptions("ExperimentalHUDOverlay=true\nHUDDistanceMeters=1.6\n"
                                  "HUDWidthMeters=1.4\nHUDVerticalOffsetMeters=-0.25\n");
    const auto hudConfig = ParseConfig(hudOptions, m2Defaults);
    expect(hudConfig.errors.empty(), true, "bounded experimental HUD settings parse");
    expect(hudConfig.value.experimentalHudOverlay, true, "HUD overlay requires explicit opt-in");
    expectNear(hudConfig.value.hudDistanceMeters, 1.6f, "HUD distance parsed");
    expectNear(hudConfig.value.hudWidthMeters, 1.4f, "HUD width parsed");
    expectNear(hudConfig.value.hudVerticalOffsetMeters, -0.25f, "HUD vertical offset parsed");
    std::istringstream hudBad("ExperimentalHUDOverlay=maybe\nHUDDistanceMeters=0\n"
                              "HUDWidthMeters=10\nHUDVerticalOffsetMeters=nan\n");
    const auto hudFallback = ParseConfig(hudBad, hudConfig.value);
    expect(hudFallback.errors.size() == 4, true, "invalid HUD fields are all diagnosed");
    expect(hudFallback.value.experimentalHudOverlay, true,
           "invalid HUD toggle retains prior valid value");
    expectNear(hudFallback.value.hudDistanceMeters, 1.6f, "invalid HUD distance retains prior");
    expectNear(hudFallback.value.hudWidthMeters, 1.4f, "invalid HUD width retains prior");
    expectNear(hudFallback.value.hudVerticalOffsetMeters, -0.25f,
               "invalid HUD vertical offset retains prior");
    expect(m2Defaults.experimentalPortalShotHaptics, false,
           "unverified portal-shot haptics default off");
    std::istringstream hapticOptions("ExperimentalPortalShotHaptics=true\n"
                                    "PortalShotHapticAmplitude=0.6\n"
                                    "PortalShotHapticDurationSeconds=0.08\n");
    const auto hapticConfig = ParseConfig(hapticOptions, m2Defaults);
    expect(hapticConfig.errors.empty(), true, "bounded portal-shot haptic settings parse");
    expect(hapticConfig.value.experimentalPortalShotHaptics, true,
           "haptic output requires explicit opt-in");
    expectNear(hapticConfig.value.portalShotHapticAmplitude, 0.6f, "haptic amplitude parsed");
    expectNear(hapticConfig.value.portalShotHapticDurationSeconds, 0.08f,
               "haptic duration parsed");
    std::istringstream hapticBad("ExperimentalPortalShotHaptics=maybe\n"
                                "PortalShotHapticAmplitude=1.2\n"
                                "PortalShotHapticDurationSeconds=nan\n");
    const auto hapticFallback = ParseConfig(hapticBad, hapticConfig.value);
    expect(hapticFallback.errors.size() == 3, true, "invalid haptic fields are diagnosed");
    expect(hapticFallback.value.experimentalPortalShotHaptics, true,
           "invalid haptic toggle retains prior value");
    expectNear(hapticFallback.value.portalShotHapticAmplitude, 0.6f,
               "out-of-range amplitude retains prior value");
    expectNear(hapticFallback.value.portalShotHapticDurationSeconds, 0.08f,
               "nonfinite duration retains prior value");

    Haptics::ShotGate shotGate;
    const auto shotStart = std::chrono::steady_clock::time_point{} + std::chrono::seconds(1);
    expect(shotGate.Queue(shotStart), true, "first shot queues one pulse");
    expect(shotGate.Queue(shotStart + std::chrono::milliseconds(20)), false,
           "duplicate callback cannot queue a second pending pulse");
    expect(shotGate.Consume(), true, "queued shot is delivered once");
    expect(shotGate.Consume(), false, "consumed shot cannot be delivered twice");
    expect(shotGate.Queue(shotStart + std::chrono::milliseconds(50)), false,
           "repeat shot inside 100 ms cooldown is suppressed");
    expect(shotGate.Queue(shotStart + std::chrono::milliseconds(100)), true,
           "shot at cooldown boundary is accepted");
    shotGate.Clear();
    expect(shotGate.Consume(), false, "menu or tracking loss discards pending shot");
    expect(shotGate.Queue(shotStart + std::chrono::milliseconds(101)), true,
           "discarded shot cannot suppress the first valid shot after recovery");
    shotGate.Clear();
    expect(shotGate.Queue(shotStart + std::chrono::milliseconds(200)), true,
           "new shot after discarded event and cooldown is accepted");
    Haptics::ShotGate delayedShots;
    delayedShots.Queue(shotStart);
    expect(delayedShots.Consume(shotStart + std::chrono::milliseconds(500)), true,
           "delayed first shot may deliver after a stalled frame");
    delayedShots.Queue(shotStart + std::chrono::milliseconds(501));
    expect(delayedShots.Consume(shotStart + std::chrono::milliseconds(501)), false,
           "a second pulse cannot follow immediately after a delayed pulse");
    expect(Haptics::OutputHand(false) == Haptics::Hand::Right, true,
           "right-handed aiming vibrates physical right hand");
    expect(Haptics::OutputHand(true) == Haptics::Hand::Left, true,
           "left-handed aiming vibrates physical left hand");
    expect(Haptics::CanDeliverShot(true, true, true, true, true, true), true,
           "ready local gameplay delivers a queued haptic shot");
    expect(Haptics::CanDeliverShot(false, true, true, true, true, true), false,
           "disabled haptics never reach OpenVR");
    expect(Haptics::CanDeliverShot(true, false, true, true, true, true), false,
           "failed action update drops pending haptics");
    expect(Haptics::CanDeliverShot(true, true, false, true, true, true), false,
           "menu focus drops pending haptics");
    expect(Haptics::CanDeliverShot(true, true, true, false, true, true), false,
           "invalid tracking drops pending haptics");
    expect(Haptics::CanDeliverShot(true, true, true, true, false, true), false,
           "disconnected firing controller drops pending haptics");
    expect(Haptics::CanDeliverShot(true, true, true, true, true, false), false,
           "missing output action drops pending haptics");
    expect(Haptics::IsLocalShot(1, 1), true,
           "local portal-gun owner qualifies as shot source");
    expect(Haptics::IsLocalShot(1, 2), false,
           "another player's portal gun cannot vibrate the local hand");
    expect(Haptics::IsLocalShot(0, 0), false,
           "missing local player cannot qualify as shot source");
    expect(Haptics::IsLocalShot(1, -1), false,
           "weapon without a resolved owner cannot qualify as shot source");

    float poseMatrix[3][4] = {{1,0,0,0}, {0,1,0,0}, {0,0,1,0}};
    float poseVelocity[3] = {0,0,0};
    float poseAngularVelocity[3] = {0,0,0};
    expect(IsUsableTrackedPose(poseMatrix, poseVelocity, poseAngularVelocity), true,
           "finite tracked pose is usable");
    poseMatrix[0][3] = std::numeric_limits<float>::quiet_NaN();
    expect(IsUsableTrackedPose(poseMatrix, poseVelocity, poseAngularVelocity), false,
           "nonfinite tracking position is rejected");
    poseMatrix[0][3] = 0;
    poseMatrix[1][2] = 1.01f;
    expect(IsUsableTrackedPose(poseMatrix, poseVelocity, poseAngularVelocity), false,
           "out-of-domain tracking rotation is rejected");
    poseMatrix[1][2] = 1.00001f;
    expect(IsUsableTrackedPose(poseMatrix, poseVelocity, poseAngularVelocity), true,
           "small matrix rounding error is accepted for clamped asin");
    poseMatrix[1][2] = 0;
    poseAngularVelocity[2] = std::numeric_limits<float>::infinity();
    expect(IsUsableTrackedPose(poseMatrix, poseVelocity, poseAngularVelocity), false,
           "nonfinite angular velocity is rejected");

    std::istringstream portalModeFull("PortalOrientationMode=FullRotation\n");
    const auto fullModeConfig = ParseConfig(portalModeFull, m2Defaults);
    expect(fullModeConfig.value.portalOrientationMode == PortalOrientation::Mode::FullRotation,
           true, "full portal rotation requires explicit config");
    std::istringstream portalModeHorizon("PortalOrientationMode=PreserveHorizon\n");
    const auto horizonModeConfig = ParseConfig(portalModeHorizon, fullModeConfig.value);
    expect(horizonModeConfig.value.portalOrientationMode == PortalOrientation::Mode::PreserveHorizon,
           true, "horizon-preserving mode parses");
    std::istringstream portalModeBad("PortalOrientationMode=Magic\n");
    const auto badPortalMode = ParseConfig(portalModeBad, horizonModeConfig.value);
    expect(badPortalMode.value.portalOrientationMode == PortalOrientation::Mode::PreserveHorizon,
           true, "invalid portal mode retains prior valid mode");
    expect(badPortalMode.errors.empty(), false, "invalid portal mode is diagnosed");
    std::istringstream roomscaleObserve("RoomscaleMode=Observe\n");
    const auto observedConfig = ParseConfig(roomscaleObserve, m2Defaults);
    expect(observedConfig.errors.empty(), true, "roomscale observe config is valid");
    expect(observedConfig.value.roomscaleMode == RoomscaleMotion::Mode::Observe, true,
           "roomscale observe config selects no-motion diagnostics");
    std::istringstream roomscaleBad("RoomscaleMode=Active\n");
    const auto badRoomscaleConfig = ParseConfig(roomscaleBad, observedConfig.value);
    expect(badRoomscaleConfig.value.roomscaleMode == RoomscaleMotion::Mode::Observe, true,
           "invalid roomscale config retains previous mode");
    expect(badRoomscaleConfig.errors.size() == 1, true,
           "unsupported roomscale activation is diagnosed");
    RoomscaleMotion::Observer diagnostic;
    diagnostic.OnPose(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f, true);
    diagnostic.OnPose(true, {0.1f, 0.0f, 1.6f}, 2, 0.0f, 43.2f, true);
    expect(diagnostic.OnCommand(1, true).has_value(), false, "off roomscale mode ignores poses and commands");
    expect(diagnostic.SetMode(RoomscaleMotion::Mode::Observe), true, "observe mode transition reported");
    diagnostic.OnPose(true, {5.0f, 0.0f, 1.6f}, 3, 0.0f, 43.2f, true);
    diagnostic.OnPose(true, {5.1f, 0.0f, 1.6f}, 4, 0.0f, 43.2f, true);
    diagnostic.OnPose(true, {9.0f, 0.0f, 1.6f}, 4, 0.0f, 43.2f, true);
    const auto observedStep = diagnostic.OnCommand(2, true);
    expect(observedStep.has_value(), true, "observe mode computes diagnostic intent");
    if (observedStep)
        expectVectorNear(*observedStep, {4.32f, 0.0f, 0.0f}, "observe mode reports unmoved intent");
    const auto observedSummary = diagnostic.TakeSummary();
    expect(observedSummary.steps == 1, true, "diagnostic summary counts consumed steps");
    expectNear(observedSummary.distanceUnits, 4.32f, "diagnostic summary measures intended distance");
    expect(diagnostic.TakeSummary().steps == 0, true, "diagnostic summary drains once");
    diagnostic.OnPose(true, {5.2f, 0.0f, 1.6f}, 5, 0.0f, 43.2f, true);
    expect(diagnostic.OnCommand(3, false).has_value(), false,
           "menu or missing gameplay gate discards pending intent");
    diagnostic.OnPose(true, {8.0f, 0.0f, 1.6f}, 6, 0.0f, 43.2f, true);
    expect(diagnostic.OnCommand(4, true).has_value(), false,
           "return from menu establishes a fresh baseline");
    diagnostic.OnPose(true, {8.1f, 0.0f, 1.6f}, 7, 0.0f, 43.2f, false);
    expect(diagnostic.OnCommand(5, true).has_value(), false,
           "poses observed in menus do not queue movement");
    diagnostic.OnPose(false, {}, 8, 0.0f, 43.2f, true);
    expect(diagnostic.OnCommand(6, true).has_value(), false,
           "invalid HMD pose blocks command consumption");
    expect(diagnostic.SetMode(RoomscaleMotion::Mode::Off), true, "disabling observation reported");
    diagnostic.OnPose(true, {5.2f, 0.0f, 1.6f}, 9, 0.0f, 43.2f, true);
    expect(diagnostic.OnCommand(7, true).has_value(), false, "disabled observer emits no movement intent");
    std::istringstream m2Options("TrackingMode=Standing\nMovementDirection=LeftController\n"
                                 "HeightOffsetMeters=0.25\nControllerPitchDegrees=15\n");
    const auto m2Valid = ParseConfig(m2Options, m2Defaults);
    expect(m2Valid.errors.empty(), true, "valid M2 config has no errors");
    expect(m2Valid.value.trackingMode == TrackingSpace::TrackingMode::Standing, true,
           "standing mode parsed");
    expect(m2Valid.value.movementDirection == TrackingSpace::MovementDirection::LeftController,
           true, "left controller movement parsed");
    expectNear(m2Valid.value.heightOffsetMeters, 0.25f, "height offset parsed");
    expectNear(m2Valid.value.controllerPitchDegrees, 15.0f, "controller pitch parsed");
    std::istringstream m2Right("MovementDirection=RightController\nHeightOffsetMeters=-0.5\n"
                               "ControllerPitchDegrees=-60\n");
    const auto m2Boundary = ParseConfig(m2Right, m2Defaults);
    expect(m2Boundary.errors.empty(), true, "M2 lower bounds valid");
    expect(m2Boundary.value.movementDirection == TrackingSpace::MovementDirection::RightController,
           true, "right controller movement parsed");
    std::istringstream m2Bad("TrackingMode=Floor\nMovementDirection=Neither\n"
                             "HeightOffsetMeters=nan\nControllerPitchDegrees=61\nSeatedMode=true\n");
    const auto m2Rejected = ParseConfig(m2Bad, m2Valid.value);
    expect(m2Rejected.value.trackingMode == m2Valid.value.trackingMode, true,
           "invalid tracking mode retains previous");
    expect(m2Rejected.value.movementDirection == m2Valid.value.movementDirection, true,
           "invalid movement direction retains previous");
    expectNear(m2Rejected.value.heightOffsetMeters, 0.25f,
               "invalid height offset retains previous");
    expectNear(m2Rejected.value.controllerPitchDegrees, 15.0f,
               "invalid controller pitch retains previous");
    expect(m2Rejected.errors.size() == 5, true, "invalid M2 entries and legacy key reported");

    if (failures) return 1;
    std::cout << "stabilization tests passed\n";
    return 0;
}
