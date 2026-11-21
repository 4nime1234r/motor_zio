#include "panels/DiagnosticsPanel.h"

#include <imgui.h>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <psapi.h>

#include "zio/jobs/JobSystem.h"
#include "zio/renderer/BatchRenderer.h"

namespace zio_editor
{

void DiagnosticsPanel::draw(float frameTimeMs)
{
    ImGui::Begin("Diagnóstico", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    const float fps = frameTimeMs > 0.f ? 1000.f / frameTimeMs : 0.f;
    ImGui::Text("Tiempo por fotograma: %.2f ms (%.1f FPS)", frameTimeMs, fps);

    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        ImGui::Text("Memoria en uso: %.1f MB", static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0));
    else
        ImGui::TextDisabled("Memoria en uso: no disponible");

    ImGui::Separator();
    ImGui::TextUnformatted("Dibujo 2D (último fotograma)");
    const auto stats = zio::renderer::BatchRenderer::instance().lastFrameStats();
    ImGui::Text("Llamadas de dibujo: %u", stats.drawCalls);
    ImGui::Text("Rectángulos: %u", stats.quadCount);

    ImGui::Separator();
    ImGui::TextUnformatted("Tareas en segundo plano");
    auto& jobs = zio::jobs::JobSystem::instance();
    ImGui::Text("Hilos de trabajo: %u", jobs.workerCount());
    ImGui::Text("Tareas en cola: %zu", jobs.queuedTasks());
    ImGui::Text("Finalizaciones pendientes: %zu", jobs.pendingCompletions());

    ImGui::End();
}

}
