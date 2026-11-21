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
    ImGui::Begin("Rendimiento");

    if (ImGui::BeginTable("##rendimiento", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV))
    {
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::SeparatorText("Fotograma");
        const float fps = frameTimeMs > 0.f ? 1000.f / frameTimeMs : 0.f;
        ImGui::Text("%.2f ms", frameTimeMs);
        ImGui::Text("%.1f FPS", fps);
        PROCESS_MEMORY_COUNTERS pmc{};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
            ImGui::Text("%.1f MB en uso", static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0));
        else
            ImGui::TextDisabled("Memoria no disponible");

        ImGui::TableNextColumn();
        ImGui::SeparatorText("Dibujo 2D");
        const auto stats = zio::renderer::BatchRenderer::instance().lastFrameStats();
        ImGui::Text("%u llamadas de dibujo", stats.drawCalls);
        ImGui::Text("%u rectángulos", stats.quadCount);

        ImGui::TableNextColumn();
        ImGui::SeparatorText("Tareas en segundo plano");
        auto& jobs = zio::jobs::JobSystem::instance();
        ImGui::Text("%u hilos de trabajo", jobs.workerCount());
        ImGui::Text("%zu tareas en cola", jobs.queuedTasks());
        ImGui::Text("%zu finalizaciones pendientes", jobs.pendingCompletions());

        ImGui::EndTable();
    }

    ImGui::End();
}

}
