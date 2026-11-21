#include "EditorApp.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_win32.h>

#include <vulkan/vulkan.h>

#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "EditorWindow.h"
#include "core/Icons.h"
#include "core/LayoutSettings.h"
#include "core/Picking.h"
#include "core/RecentProject.h"
#include "core/SelectionGizmo.h"
#include "core/Theme.h"
#include "core/ViewportGrid.h"
#include "panels/AddObjectMenu.h"
#include "zio/render/Device.h"
#include "zio/scene3d/Renderer3D.h"

namespace zio_editor
{

namespace
{
constexpr std::uint32_t kInitialWidth  = 1600;
constexpr std::uint32_t kInitialHeight = 900;

constexpr float kSidePanelWidth    = 330.f;
constexpr float kBottomPanelHeight = 250.f;

constexpr ImVec4 kErrorColor  = ImVec4(0.937f, 0.267f, 0.267f, 1.f);
constexpr ImVec4 kUpdateColor = ImVec4(0.506f, 0.549f, 0.973f, 1.f);

constexpr float kSnapStep       = 0.25f;
constexpr float kFastMultiplier = 3.f;
constexpr float kMinFlySpeed    = 0.5f;
constexpr float kFlySpeedPerDistance = 0.6f;
constexpr float kMaxFrameTime   = 0.1f;
constexpr float kFocusDistance  = 2.5f;
constexpr float kMinFocus       = 1.5f;
constexpr float kMenuClickRadius = 4.f;
constexpr float kRotateSnapRadians = 0.2617994f;
constexpr float kScaleSnapStep     = 0.1f;
constexpr float kMinScaleFactor    = 0.01f;
constexpr float kRadiansToDegrees  = 57.2957795f;

constexpr std::uint32_t kImGuiDescriptorPoolSize = 64;

void checkVkResult(VkResult err)
{
    if (err != VK_SUCCESS)
        std::fprintf(stderr, "[zio_editor] Vulkan error in Dear ImGui backend: %d\n", static_cast<int>(err));
}

void copyText(char* dest, std::size_t capacity, const std::string& text)
{
    std::snprintf(dest, capacity, "%s", text.c_str());
}

std::string describe(const Action& action)
{
    switch (action.kind)
    {
    case ActionKind::OpenScene:
        return "Se abrirá la escena " + action.text + ".";
    case ActionKind::OpenProject:
        return "Se abrirá el proyecto " + action.path.string() + ".";
    case ActionKind::CreateProject:
        return "Se creará el proyecto " + action.text + ".";
    case ActionKind::CloseWindow:
        return "Se cerrará el editor.";
    }
    return {};
}
}

EditorApp::EditorApp() : window_(std::make_unique<EditorWindow>())
{
    const std::filesystem::path launchDir = std::filesystem::current_path();
    imguiIniPath_                         = (launchDir / "imgui.ini").string();
    copyText(newProjectParent_, sizeof(newProjectParent_), launchDir.string());

    recentFile_ = recentProjectFile();
    layoutFile_ = layoutSettingsFile();
    layout_     = readLayoutSettings(layoutFile_);
    if (const auto recent = readRecentProject(recentFile_))
        copyText(openProjectPath_, sizeof(openProjectPath_), recent->string());
}

EditorApp::~EditorApp()
{
    if (ImGui::GetCurrentContext())
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    zio::render::Device::instance().shutdown();
    window_->destroy();
}

void EditorApp::openProjectAtStartup(const std::filesystem::path& path)
{
    std::string error;
    if (!context_.openProject(path, &error))
    {
        statusError_ = error;
        std::fprintf(stderr, "[zio_editor] %s\n", error.c_str());
    }
}

void EditorApp::openRecentProject()
{
    const auto recent = readRecentProject(recentFile_);
    if (!recent)
        return;

    std::string error;
    if (!context_.openProject(*recent, &error))
        statusError_ = "No se pudo reabrir el último proyecto: " + error;
}

void EditorApp::rememberProject()
{
    if (!context_.project().isOpen() || context_.project().rootDir() == rememberedProject_)
        return;
    rememberedProject_ = context_.project().rootDir();
    writeRecentProject(recentFile_, rememberedProject_);
}

void EditorApp::run()
{
    if (!window_->create("Editor de Zio", kInitialWidth, kInitialHeight))
        return;

    auto& device = zio::render::Device::instance();
    if (!device.init(window_->nativeHandle(), kInitialWidth, kInitialHeight))
        return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = imguiIniPath_.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    applyZioTheme();
    loadEditorFonts();

    ImGui_ImplWin32_Init(window_->nativeHandle());
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion            = VK_API_VERSION_1_1;
    initInfo.Instance              = static_cast<VkInstance>(device.nativeInstance());
    initInfo.PhysicalDevice        = static_cast<VkPhysicalDevice>(device.nativePhysicalDevice());
    initInfo.Device                = static_cast<VkDevice>(device.nativeDevice());
    initInfo.QueueFamily           = device.nativeQueueFamily();
    initInfo.Queue                 = static_cast<VkQueue>(device.nativeQueue());
    initInfo.DescriptorPoolSize    = kImGuiDescriptorPoolSize;
    initInfo.MinImageCount         = device.swapchainImageCount();
    initInfo.ImageCount            = device.swapchainImageCount();
    initInfo.PipelineInfoMain.RenderPass = static_cast<VkRenderPass>(device.nativeRenderPass());
    initInfo.CheckVkResultFn       = &checkVkResult;
    ImGui_ImplVulkan_Init(&initInfo);

    if (Updater::enabled())
    {
        Updater::cleanupLeftovers();
        updater_.start();
    }

    auto lastFrameStart = std::chrono::steady_clock::now();
    float frameTimeMs    = 0.f;

    while (!context_.closeApproved())
    {
        window_->pumpMessages();
        if (window_->closeRequested())
        {
            window_->cancelClose();
            if (!context_.hasPending())
                context_.request(Action{ActionKind::CloseWindow, {}, {}});
            if (context_.closeApproved())
                break;
        }

        if (window_->resized())
        {
            device.resize(window_->width(), window_->height());
            window_->consumeResized();
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        drawUi(frameTimeMs);
        rememberProject();
        updateViewportCamera();
        handleViewportInput();
        drawViewportContextMenu();
        if (!ImGui::IsAnyItemActive() && !dragging())
            context_.commitHistory();

        ImGui::Render();

        device.beginFrame(zio::render::ClearColor{0.059f, 0.067f, 0.090f, 1.f});

        renderScene();

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(device.nativeContext()));

        device.endFrame();

        const auto frameEnd = std::chrono::steady_clock::now();
        frameTimeMs          = std::chrono::duration<float, std::milli>(frameEnd - lastFrameStart).count();
        lastFrameStart        = frameEnd;
    }

    updater_.finish(restartAfterClose_);
}

void EditorApp::renderScene()
{
    auto& scene    = context_.scene();
    auto& renderer = zio::scene3d::Renderer3D::instance();

    if (context_.playing())
    {
        scene.update(std::min(ImGui::GetIO().DeltaTime, kMaxFrameTime));
        if (viewRect_.w < 1.f || viewRect_.h < 1.f)
            return;
        renderer.begin(scene.camera(), scene.light(), static_cast<std::uint32_t>(viewRect_.w),
                       static_cast<std::uint32_t>(viewRect_.h), static_cast<std::uint32_t>(viewRect_.x),
                       static_cast<std::uint32_t>(viewRect_.y));
        scene.root().visit(renderer);
        renderer.end();
        return;
    }

    scene.update(0.f);

    if (viewRect_.w < 1.f || viewRect_.h < 1.f)
        return;

    const zio::scene3d::Camera camera = editorCamera_.toCamera(scene.camera());
    renderer.begin(camera, scene.light(), static_cast<std::uint32_t>(viewRect_.w),
                   static_cast<std::uint32_t>(viewRect_.h), static_cast<std::uint32_t>(viewRect_.x),
                   static_cast<std::uint32_t>(viewRect_.y));
    drawGrid(renderer, editorCamera_.target(), editorCamera_.distance());
    scene.root().visit(renderer);
    for (const zio::scene3d::Node3D* node : context_.selection())
        if (node != context_.selected())
            drawSelectionBox(renderer, *node);
    if (const zio::scene3d::Node3D* selected = context_.selected())
        drawSelection(renderer, *selected, camera.position, hoveredAxis_, activeAxis(), gizmoMode_);
    renderer.end();
}

bool EditorApp::beginGizmoDrag(zio::scene3d::Node3D& node, const GizmoAxes& axes, int axis, const Ray& ray)
{
    const zio::math::Vec3 direction = axes.direction[axis];
    bool started                    = false;
    if (gizmoMode_ == GizmoMode::Rotate)
    {
        started        = rotateDrag_.begin(axis, axes.origin, direction, ray);
        startRotation_ = node.rotation();
    }
    else
    {
        started        = gizmoDrag_.begin(axis, axes.origin, direction, ray);
        startScale_    = node.scale();
        startLength_   = axes.length;
        dragOrigin_    = axes.origin;
        dragDirection_ = direction;
    }
    groupStart_.clear();
    if (started && gizmoMode_ == GizmoMode::Move)
    {
        const auto roots = context_.selectionRoots();
        if (std::find(roots.begin(), roots.end(), &node) != roots.end())
            for (zio::scene3d::Node3D* other : roots)
                if (other != &node)
                    groupStart_.emplace_back(other, other->worldMatrix().transformPoint(zio::math::Vec3::zero()));
    }
    if (started)
        dragNode_ = &node;
    return started;
}

void EditorApp::endGizmoDrag()
{
    gizmoDrag_.end();
    rotateDrag_.end();
    dragNode_ = nullptr;
    groupStart_.clear();
}

void EditorApp::applyGizmoDrag(const Ray& ray, bool snap)
{
    if (!dragNode_)
        return;

    if (gizmoMode_ == GizmoMode::Rotate)
    {
        if (const auto angle = rotateDrag_.update(ray, snap ? kRotateSnapRadians : 0.f))
        {
            zio::math::Vec3 unit = zio::math::Vec3::unitX();
            if (rotateDrag_.axis() == 1)
                unit = zio::math::Vec3::unitY();
            else if (rotateDrag_.axis() == 2)
                unit = zio::math::Vec3::unitZ();
            dragNode_->setRotation(
                (startRotation_ * zio::math::Quaternion::fromAxisAngle(unit, *angle * kRadiansToDegrees)).normalized());
            context_.markDirty();
        }
        return;
    }

    const auto position = gizmoDrag_.update(ray, gizmoMode_ == GizmoMode::Move && snap ? kSnapStep : 0.f);
    if (!position)
        return;

    if (gizmoMode_ == GizmoMode::Move)
    {
        dragNode_->setPosition(localPositionFor(*dragNode_, *position));
        const zio::math::Vec3 delta = *position - dragOrigin_;
        for (const auto& [other, start] : groupStart_)
            other->setPosition(localPositionFor(*other, start + delta));
    }
    else
    {
        float factor = 1.f + (*position - dragOrigin_).dot(dragDirection_) / startLength_;
        if (snap)
            factor = std::round(factor / kScaleSnapStep) * kScaleSnapStep;
        factor = std::max(factor, kMinScaleFactor);

        zio::math::Vec3 scale = startScale_;
        switch (gizmoDrag_.axis())
        {
        case 0:
            scale.x *= factor;
            break;
        case 1:
            scale.y *= factor;
            break;
        default:
            scale.z *= factor;
            break;
        }
        dragNode_->setScale(scale);
    }
    context_.markDirty();
}

void EditorApp::handleViewportInput()
{
    ImGuiIO& io        = ImGui::GetIO();
    const float width  = viewRect_.w;
    const float height = viewRect_.h;
    if (!(width > 1.f) || !(height > 1.f))
        return;
    const float mouseX = io.MousePos.x - viewRect_.x;
    const float mouseY = io.MousePos.y - viewRect_.y;

    if (context_.playing())
    {
        if (dragging())
            endGizmoDrag();
        hoveredAxis_ = -1;
        return;
    }

    zio::scene3d::Node3D* selected    = context_.selected();
    const zio::scene3d::Camera camera = editorCamera_.toCamera(context_.scene().camera());
    const Ray ray = makeRay(camera, width / height, 2.f * mouseX / width - 1.f, 1.f - 2.f * mouseY / height);

    if (dragging() && (!selected || selected != dragNode_))
        endGizmoDrag();

    if (dragging())
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            applyGizmoDrag(ray, io.KeyCtrl);
        else
            endGizmoDrag();
        hoveredAxis_ = activeAxis();
        return;
    }

    const bool insideView   = viewRect_.contains(io.MousePos.x, io.MousePos.y);
    const bool overViewport = !io.WantCaptureMouse && insideView && cameraDrag_ == CameraDrag::None;
    hoveredAxis_            = -1;
    GizmoAxes axes;
    if (selected && overViewport)
    {
        axes         = computeGizmoAxes(*selected, camera.position);
        hoveredAxis_ = gizmoMode_ == GizmoMode::Rotate
                           ? hitTestRings(camera, width, height, axes, mouseX, mouseY)
                           : hitTestGizmo(camera, width, height, axes, mouseX, mouseY);
    }

    if (!io.WantCaptureMouse && insideView && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (!(hoveredAxis_ >= 0 && beginGizmoDrag(*selected, axes, hoveredAxis_, ray)))
        {
            zio::scene3d::Node3D* hit = pickNode(context_.scene().root(), ray);
            if (io.KeyCtrl)
                context_.toggleSelect(hit);
            else
                context_.select(hit);
        }
    }

    handleViewportKeys();
}

void EditorApp::focusSelection()
{
    const zio::scene3d::Node3D* selected = context_.selected();
    if (!selected)
        return;
    zio::math::Vec3 center;
    float radius = 1.f;
    selectionFocus(*selected, center, radius);
    editorCamera_.focus(center, std::max(radius * kFocusDistance, kMinFocus));
}

void EditorApp::drawViewportContextMenu()
{
    constexpr const char* kPopupId = "##menu_visor";
    if (openViewportMenu_)
    {
        openViewportMenu_ = false;
        ImGuiIO& io       = ImGui::GetIO();
        const float width  = viewRect_.w;
        const float height = viewRect_.h;
        if (width > 1.f && height > 1.f && !context_.hasPending() &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        {
            const zio::scene3d::Camera camera = editorCamera_.toCamera(context_.scene().camera());
            const Ray ray = makeRay(camera, width / height, 2.f * (io.MousePos.x - viewRect_.x) / width - 1.f,
                                    1.f - 2.f * (io.MousePos.y - viewRect_.y) / height);
            if (zio::scene3d::Node3D* hit = pickNode(context_.scene().root(), ray))
                context_.select(hit);
            ImGui::OpenPopup(kPopupId);
        }
    }

    if (!ImGui::BeginPopup(kPopupId))
        return;

    if (ImGui::BeginMenu("Añadir objeto"))
    {
        drawAddObjectItems(context_);
        ImGui::EndMenu();
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Mover", "1", gizmoMode_ == GizmoMode::Move))
        gizmoMode_ = GizmoMode::Move;
    if (ImGui::MenuItem("Rotar", "2", gizmoMode_ == GizmoMode::Rotate))
        gizmoMode_ = GizmoMode::Rotate;
    if (ImGui::MenuItem("Escalar", "3", gizmoMode_ == GizmoMode::Scale))
        gizmoMode_ = GizmoMode::Scale;

    const zio::scene3d::Node3D* selected = context_.selected();
    if (selected)
    {
        ImGui::Separator();
        if (ImGui::MenuItem("Enfocar", "F"))
            focusSelection();
        if (ImGui::MenuItem("Deseleccionar", "Esc"))
            context_.clearSelection();
        if (ImGui::MenuItem("Eliminar", "Supr", false, selected != &context_.scene().root()))
            context_.removeSelected();
    }

    ImGui::Separator();
    drawEditItems();
    ImGui::EndPopup();
}

void EditorApp::drawEditItems()
{
    const bool hasSelection = !context_.selectionRoots().empty();
    if (ImGui::MenuItem("Copiar", "Ctrl+C", false, hasSelection))
        context_.copySelection();
    if (ImGui::MenuItem("Pegar", "Ctrl+V", false, context_.canPaste()))
        context_.paste();
    if (ImGui::MenuItem("Duplicar", "Ctrl+D", false, hasSelection))
        context_.duplicateSelection();
    ImGui::Separator();
    if (ImGui::MenuItem("Deshacer", "Ctrl+Z", false, context_.canUndo()))
        context_.undo();
    if (ImGui::MenuItem("Rehacer", "Ctrl+Y", false, context_.canRedo()))
        context_.redo();
}

void EditorApp::togglePlay()
{
    if (context_.playing())
        context_.stopPlay();
    else if (context_.project().isOpen())
        context_.startPlay();
}

void EditorApp::handleViewportKeys()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || io.WantCaptureMouse || context_.hasPending() ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return;

    if (!io.KeyCtrl && !io.KeyAlt)
    {
        float forward = 0.f, right = 0.f, up = 0.f;
        if (ImGui::IsKeyDown(ImGuiKey_W))
            forward += 1.f;
        if (ImGui::IsKeyDown(ImGuiKey_S))
            forward -= 1.f;
        if (ImGui::IsKeyDown(ImGuiKey_D))
            right += 1.f;
        if (ImGui::IsKeyDown(ImGuiKey_A))
            right -= 1.f;
        if (ImGui::IsKeyDown(ImGuiKey_E))
            up += 1.f;
        if (ImGui::IsKeyDown(ImGuiKey_Q))
            up -= 1.f;

        if (forward != 0.f || right != 0.f || up != 0.f)
        {
            const float speed = std::max(kMinFlySpeed, editorCamera_.distance() * kFlySpeedPerDistance) *
                                (io.KeyShift ? kFastMultiplier : 1.f) *
                                std::min(io.DeltaTime, kMaxFrameTime);
            editorCamera_.fly(forward * speed, right * speed, up * speed);
        }
    }

    if (io.KeyCtrl && !io.KeyAlt)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, true))
        {
            if (io.KeyShift)
                context_.redo();
            else
                context_.undo();
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Y, true))
            context_.redo();
        else if (ImGui::IsKeyPressed(ImGuiKey_C, false))
            context_.copySelection();
        else if (ImGui::IsKeyPressed(ImGuiKey_V, false))
            context_.paste();
        else if (ImGui::IsKeyPressed(ImGuiKey_D, false))
            context_.duplicateSelection();
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_1, false))
        gizmoMode_ = GizmoMode::Move;
    if (ImGui::IsKeyPressed(ImGuiKey_2, false))
        gizmoMode_ = GizmoMode::Rotate;
    if (ImGui::IsKeyPressed(ImGuiKey_3, false))
        gizmoMode_ = GizmoMode::Scale;
    if (ImGui::IsKeyPressed(ImGuiKey_F, false))
        focusSelection();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        context_.clearSelection();
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        context_.removeSelected();
}

void EditorApp::updateViewportCamera()
{
    if (context_.sceneRevision() != seenRevision_)
    {
        editorCamera_.resetFrom(context_.scene().camera());
        seenRevision_ = context_.sceneRevision();
        cameraDrag_   = CameraDrag::None;
    }

    if (context_.playing())
    {
        cameraDrag_ = CameraDrag::None;
        return;
    }

    ImGuiIO& io       = ImGui::GetIO();
    const bool overUi = io.WantCaptureMouse || !viewRect_.contains(io.MousePos.x, io.MousePos.y);

    if (!overUi && cameraDrag_ == CameraDrag::None)
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            cameraDrag_  = CameraDrag::Orbit;
            rightPressX_ = io.MousePos.x;
            rightPressY_ = io.MousePos.y;
        }
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            cameraDrag_ = CameraDrag::Pan;
    }

    if (cameraDrag_ != CameraDrag::None)
    {
        const ImGuiMouseButton button =
            cameraDrag_ == CameraDrag::Orbit ? ImGuiMouseButton_Right : ImGuiMouseButton_Middle;
        if (ImGui::IsMouseDown(button))
        {
            if (cameraDrag_ == CameraDrag::Orbit)
                editorCamera_.orbit(io.MouseDelta.x, io.MouseDelta.y);
            else
                editorCamera_.pan(io.MouseDelta.x, io.MouseDelta.y);
        }
        else
        {
            if (cameraDrag_ == CameraDrag::Orbit)
            {
                const float dx = io.MousePos.x - rightPressX_;
                const float dy = io.MousePos.y - rightPressY_;
                if (dx * dx + dy * dy < kMenuClickRadius * kMenuClickRadius)
                    openViewportMenu_ = true;
            }
            cameraDrag_ = CameraDrag::None;
        }
    }

    if (!overUi && io.MouseWheel != 0.f)
        editorCamera_.zoom(io.MouseWheel);
}

void EditorApp::saveCurrentScene()
{
    if (!context_.project().isOpen() || context_.playing())
        return;
    if (context_.canSave())
        context_.saveScene();
    else
        wantSaveAsPopup_ = true;
}

void EditorApp::drawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;

    const bool projectOpen = context_.project().isOpen();

    if (ImGui::BeginMenu("Proyecto"))
    {
        if (ImGui::MenuItem("Nuevo proyecto..."))
            wantNewProjectPopup_ = true;
        if (ImGui::MenuItem("Abrir proyecto..."))
            wantOpenProjectPopup_ = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Escena"))
    {
        if (ImGui::MenuItem("Guardar", "Ctrl+S", false, projectOpen && !context_.playing()))
            saveCurrentScene();
        if (ImGui::MenuItem("Guardar como...", nullptr, false, projectOpen && !context_.playing()))
            wantSaveAsPopup_ = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Editar"))
    {
        drawEditItems();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Objeto"))
    {
        drawAddObjectItems(context_);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Ver"))
    {
        if (ImGui::BeginMenu("Posición de los paneles"))
        {
            for (int panel = 0; panel < PanelCount; ++panel)
            {
                if (!ImGui::BeginMenu(panelTitle(panel)))
                    continue;
                for (PanelSide side : {PanelSide::Left, PanelSide::Right, PanelSide::Bottom})
                {
                    if (ImGui::MenuItem(sideLabel(side), nullptr, layout_.side[panel] == side) &&
                        layout_.side[panel] != side)
                    {
                        layout_.side[panel] = side;
                        changeLayout();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Interfaz fija", nullptr, layout_.locked))
        {
            layout_.locked = !layout_.locked;
            writeLayoutSettings(layoutFile_, layout_);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Restablecer disposición"))
        {
            const bool locked = layout_.locked;
            layout_           = LayoutSettings{};
            layout_.locked    = locked;
            changeLayout();
        }
        ImGui::EndMenu();
    }

    const float size       = ImGui::GetFrameHeight() - 4.f;
    const float groupWidth = size * 2.f + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - groupWidth) * 0.5f);

    ImGui::BeginDisabled(!projectOpen);
    if (iconButton("##jugar", Icon::Play, context_.playing(), "Jugar (F5)", size) && !context_.playing())
        togglePlay();
    ImGui::SameLine();
    if (iconButton("##detener", Icon::Stop, context_.playing(), "Detener (F5)", size) && context_.playing())
        togglePlay();
    ImGui::EndDisabled();

    ImGui::EndMainMenuBar();
}

void EditorApp::drawStatusBar(float frameTimeMs)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 4.f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    const bool open = ImGui::BeginViewportSideBar("##barra_estado", viewport, ImGuiDir_Down,
                                                  ImGui::GetFrameHeight() - 2.f, kFlags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!open)
    {
        ImGui::End();
        return;
    }

    ImGui::AlignTextToFramePadding();
    if (context_.project().isOpen())
    {
        const std::string& scene = context_.currentScene();
        ImGui::TextDisabled("%s", context_.project().name().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("/");
        ImGui::SameLine();
        ImGui::TextUnformatted(scene.empty() ? "(sin escena)" : scene.c_str());
        if (context_.isDirty())
        {
            ImGui::SameLine(0.f, 4.f);
            ImGui::TextColored(kUpdateColor, "*");
        }
        if (context_.selection().size() > 1)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("  %d seleccionados", static_cast<int>(context_.selection().size()));
        }
    }
    else
    {
        ImGui::TextDisabled("Sin proyecto: crea o abre uno desde el menú Proyecto");
    }

    const std::string& error = context_.lastError().empty() ? statusError_ : context_.lastError();
    if (!error.empty())
    {
        ImGui::SameLine();
        ImGui::TextColored(kErrorColor, "   %s", error.c_str());
    }
    drawUpdateStatus();

    char right[96];
    const float fps = frameTimeMs > 0.f ? 1000.f / frameTimeMs : 0.f;
    std::snprintf(right, sizeof(right), "%.0f FPS    Zio v%s", fps, ZIO_VERSION);
    const float rightWidth = ImGui::CalcTextSize(right).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - rightWidth - ImGui::GetStyle().WindowPadding.x);
    ImGui::TextDisabled("%s", right);

    ImGui::End();
}

void EditorApp::drawViewportToolbar()
{
    if (context_.playing() || viewRect_.w < 1.f || viewRect_.h < 1.f)
        return;

    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking |
                                        ImGuiWindowFlags_NoMove;

    ImGui::SetNextWindowPos(ImVec2(viewRect_.x + 12.f, viewRect_.y + 12.f));
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 6.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 9.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f, 4.f));
    const bool open = ImGui::Begin("##herramientas_visor", nullptr, kFlags);
    ImGui::PopStyleVar(3);
    if (open)
    {
        const float size = ImGui::GetFrameHeight() + 4.f;
        if (iconButton("##mover", Icon::Move, gizmoMode_ == GizmoMode::Move, "Mover (1)", size))
            gizmoMode_ = GizmoMode::Move;
        ImGui::SameLine();
        if (iconButton("##rotar", Icon::Rotate, gizmoMode_ == GizmoMode::Rotate, "Rotar (2)", size))
            gizmoMode_ = GizmoMode::Rotate;
        ImGui::SameLine();
        if (iconButton("##escalar", Icon::Scale, gizmoMode_ == GizmoMode::Scale, "Escalar (3)", size))
            gizmoMode_ = GizmoMode::Scale;
        ImGui::SameLine(0.f, 10.f);
        ImGui::BeginDisabled(context_.selected() == nullptr);
        if (iconButton("##enfocar", Icon::Focus, false, "Enfocar la selección (F)", size))
            focusSelection();
        ImGui::EndDisabled();
    }
    ImGui::End();
}

void EditorApp::changeLayout()
{
    writeLayoutSettings(layoutFile_, layout_);
    resetLayout_ = true;
}

void EditorApp::buildDefaultLayout(std::uint32_t dockspaceId, float width, float height)
{
    const ImGuiID root = dockspaceId;
    ImGui::DockBuilderRemoveNode(root);
    ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(root, ImVec2(width, height));

    std::vector<int> panels[3];
    for (int panel = 0; panel < PanelCount; ++panel)
        panels[static_cast<int>(layout_.side[panel])].push_back(panel);
    const std::vector<int>& left   = panels[static_cast<int>(PanelSide::Left)];
    const std::vector<int>& right  = panels[static_cast<int>(PanelSide::Right)];
    const std::vector<int>& bottom = panels[static_cast<int>(PanelSide::Bottom)];

    ImGuiID center  = root;
    ImGuiID leftId  = 0;
    ImGuiID rightId = 0;
    ImGuiID bottomId = 0;
    if (!right.empty())
        rightId = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
    if (!left.empty())
        leftId = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, left.size() == 1 && right.empty() ? 0.22f : 0.26f,
                                             nullptr, &center);
    if (!bottom.empty())
        bottomId = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, nullptr, &center);

    auto stack = [](ImGuiID node, const std::vector<int>& list, ImGuiDir direction) {
        ImGuiID rest = node;
        for (std::size_t i = 0; i + 1 < list.size(); ++i)
        {
            const float remaining = static_cast<float>(list.size() - i);
            const float ratio     = list.size() == 2 ? 0.58f : 1.f / remaining;
            ImGuiID first         = 0;
            ImGuiID next          = ImGui::DockBuilderSplitNode(rest, direction, ratio, &first, &rest);
            (void)next;
            ImGui::DockBuilderDockWindow(panelTitle(list[i]), first);
        }
        if (!list.empty())
            ImGui::DockBuilderDockWindow(panelTitle(list.back()), rest);
    };
    stack(leftId, left, ImGuiDir_Up);
    stack(rightId, right, ImGuiDir_Up);
    for (auto it = bottom.rbegin(); it != bottom.rend(); ++it)
        ImGui::DockBuilderDockWindow(panelTitle(*it), bottomId);

    ImGui::DockBuilderFinish(root);
}

void EditorApp::drawUpdateStatus()
{
    if (!Updater::enabled())
        return;

    const UpdateStatus update = updater_.status();
    switch (update.state)
    {
    case UpdateState::Available:
        ImGui::SameLine();
        ImGui::TextColored(kUpdateColor, "  Actualización disponible: v%s", update.version.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Descargar"))
            updater_.download();
        break;
    case UpdateState::Downloading:
    {
        ImGui::SameLine();
        char label[64];
        std::snprintf(label, sizeof(label), "Descargando v%s", update.version.c_str());
        ImGui::ProgressBar(update.progress, ImVec2(180.f, 0.f), label);
        break;
    }
    case UpdateState::Downloaded:
        ImGui::SameLine();
        ImGui::TextColored(kUpdateColor, "  Zio v%s lista para instalar", update.version.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Reiniciar y actualizar") && !context_.hasPending())
        {
            restartAfterClose_ = true;
            context_.request(Action{ActionKind::CloseWindow, {}, {}});
        }
        break;
    case UpdateState::Error:
        ImGui::SameLine();
        ImGui::TextColored(kErrorColor, "  %s", update.error.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Reintentar"))
            updater_.retry();
        break;
    default:
        break;
    }
}

void EditorApp::drawProjectPopups()
{
    if (wantNewProjectPopup_)
    {
        popupError_.clear();
        ImGui::OpenPopup("Nuevo proyecto");
        wantNewProjectPopup_ = false;
    }
    if (wantOpenProjectPopup_)
    {
        popupError_.clear();
        ImGui::OpenPopup("Abrir proyecto");
        wantOpenProjectPopup_ = false;
    }

    if (ImGui::BeginPopupModal("Nuevo proyecto", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::SetNextItemWidth(520.f);
        ImGui::InputText("Carpeta padre", newProjectParent_, sizeof(newProjectParent_));
        ImGui::SetNextItemWidth(520.f);
        ImGui::InputText("Nombre", newProjectName_, sizeof(newProjectName_));
        if (!popupError_.empty())
            ImGui::TextColored(kErrorColor, "%s", popupError_.c_str());
        if (ImGui::Button("Crear"))
        {
            std::string error;
            if (!ProjectManager::checkNewProject(newProjectParent_, newProjectName_, &error))
            {
                popupError_ = error;
            }
            else if (context_.request(Action{ActionKind::CreateProject, newProjectName_, newProjectParent_}))
            {
                statusError_.clear();
                ImGui::CloseCurrentPopup();
            }
            else
            {
                popupError_ = context_.lastError();
                context_.clearError();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Abrir proyecto", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::SetNextItemWidth(520.f);
        ImGui::InputText("project.zio o su carpeta", openProjectPath_, sizeof(openProjectPath_));
        if (!popupError_.empty())
            ImGui::TextColored(kErrorColor, "%s", popupError_.c_str());
        if (ImGui::Button("Abrir"))
        {
            std::string error;
            ProjectManager probe;
            if (!probe.loadProject(openProjectPath_, &error))
            {
                popupError_ = error;
            }
            else if (context_.request(Action{ActionKind::OpenProject, {}, openProjectPath_}))
            {
                statusError_.clear();
                ImGui::CloseCurrentPopup();
            }
            else
            {
                popupError_ = context_.lastError();
                context_.clearError();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void EditorApp::drawSceneSavePopups()
{
    if (wantSaveAsPopup_)
    {
        popupError_.clear();
        ImGui::OpenPopup("Guardar escena como");
        wantSaveAsPopup_ = false;
    }
    if (context_.hasPending() && !ImGui::IsPopupOpen("Cambios sin guardar"))
    {
        modalError_.clear();
        ImGui::OpenPopup("Cambios sin guardar");
    }

    if (ImGui::BeginPopupModal("Guardar escena como", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::SetNextItemWidth(360.f);
        ImGui::InputText("Nombre (en assets/scenes)", saveAsName_, sizeof(saveAsName_));
        if (!popupError_.empty())
            ImGui::TextColored(kErrorColor, "%s", popupError_.c_str());
        if (ImGui::Button("Guardar"))
        {
            std::string error;
            if (context_.saveSceneAs(saveAsName_, &error))
            {
                popupError_.clear();
                ImGui::CloseCurrentPopup();
            }
            else
            {
                popupError_ = error;
                context_.clearError();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Cambios sin guardar", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (!context_.hasPending())
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        const std::string& scene = context_.currentScene();
        ImGui::Text("La escena %s tiene cambios sin guardar.", scene.empty() ? "(sin archivo)" : scene.c_str());
        ImGui::TextDisabled("%s", describe(context_.pending()).c_str());
        if (!modalError_.empty())
            ImGui::TextColored(kErrorColor, "%s", modalError_.c_str());

        const bool canSave = context_.canSave();
        if (!canSave)
            ImGui::BeginDisabled();
        if (ImGui::Button("Guardar"))
        {
            if (!context_.resolvePending(Decision::Save))
            {
                modalError_ = context_.lastError();
                context_.clearError();
            }
        }
        if (!canSave)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Descartar"))
            context_.resolvePending(Decision::Discard);
        ImGui::SameLine();
        if (ImGui::Button("Cancelar"))
            context_.resolvePending(Decision::Cancel);
        if (!canSave)
            ImGui::TextDisabled("La escena no tiene archivo: usa Escena > Guardar como antes de continuar.");

        if (!context_.hasPending())
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void EditorApp::drawUi(float frameTimeMs)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S) && !context_.hasPending())
        saveCurrentScene();
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false) && !context_.hasPending() && !ImGui::GetIO().WantTextInput)
        togglePlay();

    drawMenuBar();
    drawStatusBar(frameTimeMs);

    const ImGuiID dockspaceId = viewport->ID;
    if (resetLayout_ || ImGui::DockBuilderGetNode(dockspaceId) == nullptr)
    {
        buildDefaultLayout(dockspaceId, viewport->WorkSize.x, viewport->WorkSize.y);
        resetLayout_   = false;
        focusFilesTab_ = true;
    }
    ImGui::DockSpaceOverViewport(dockspaceId, viewport,
                                 ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingOverCentralNode);

    {
        constexpr ImGuiDockNodeFlags kLockFlags = ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoDockingSplit |
                                                  ImGuiDockNodeFlags_NoDockingOverMe |
                                                  ImGuiDockNodeFlags_NoDockingOverOther |
                                                  ImGuiDockNodeFlags_NoDockingOverEmpty;
        std::vector<ImGuiDockNode*> pending;
        if (ImGuiDockNode* rootNode = ImGui::DockBuilderGetNode(dockspaceId))
            pending.push_back(rootNode);
        while (!pending.empty())
        {
            ImGuiDockNode* node = pending.back();
            pending.pop_back();
            if (layout_.locked)
                node->LocalFlags |= kLockFlags;
            else
                node->LocalFlags &= ~kLockFlags;
            node->UpdateMergedFlags();
            for (ImGuiDockNode* child : node->ChildNodes)
                if (child)
                    pending.push_back(child);
        }
    }

    viewRect_ = ViewRect{viewport->WorkPos.x, viewport->WorkPos.y, viewport->WorkSize.x, viewport->WorkSize.y};
    if (const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockspaceId))
        viewRect_ = ViewRect{central->Pos.x, central->Pos.y, central->Size.x, central->Size.y};

    drawProjectPopups();
    drawSceneSavePopups();

    hierarchyPanel_.draw(context_);
    inspectorPanel_.draw(context_);
    contentBrowserPanel_.draw(context_);
    fileExplorerPanel_.draw(context_);
    diagnosticsPanel_.draw(frameTimeMs);

    if (focusFilesTab_)
    {
        ImGui::SetWindowFocus(panelTitle(PanelFiles));
        focusFilesTab_ = false;
    }

    drawViewportToolbar();
}

}
