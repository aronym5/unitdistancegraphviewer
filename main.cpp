
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include <stdio.h>          // printf, fprintf
#include <stdlib.h>         // abort
#include <SDL3/SDL.h>

#include <cmath>
#include <vector>

#define IM_MIN(A, B)            (((A) < (B)) ? (A) : (B))
#define IM_MAX(A, B)            (((A) >= (B)) ? (A) : (B))
#define IM_CLAMP(V, MN, MX)     ((V) < (MN) ? (MN) : (V) > (MX) ? (MX) : (V))

struct ExampleImageViewerData
{
    ImU32   ImageBgColor = IM_COL32(100, 100, 100, 255);
    ImVec4  GridColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
    ImVec4  GridColor100 = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);
    bool    ViewReset = true;
    bool    ShowLines = true;
    bool    ShowUnitCircle = true;
    int     ConnectivityType = 0;
    int     GridDepth = 5;
    ImVec2  ViewOffset; // in image space
    float   Zoom = 100.0f;
    float   ZoomMin = 1.0f;
    float   ZoomMax = 10000.0f;
    std::vector<int> NumPointsPerDepth;
    std::vector<int> NumLinesPerDepth;
};

static void ExampleImageViewer_DrawOptions(ExampleImageViewerData* data)
{
    ImGui::Checkbox("Show Edges", &data->ShowLines);
    ImGui::SameLine();
    ImGui::Checkbox("Show Unit Circle", &data->ShowUnitCircle);
    ImGui::SameLine();
    ImGui::Text("Zoom:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
    float zoom_100 = data->Zoom * 100.0f;
    if (ImGui::DragFloat("##Zoom", &zoom_100, 5.0f, data->ZoomMin * 100.0f, data->ZoomMax * 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
        data->Zoom = zoom_100 / 100.0f;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    ImGui::Combo("##Connectivity Type", &data->ConnectivityType, "Quad\0Hexa\0Octa\0Deka\0Dodeka\0");
    ImGui::SameLine();
    ImGui::Text("Depth:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 3.0f);
    ImGui::DragInt("##Depth", &data->GridDepth, 0.5f, 0, 50, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SameLine();
    ImGui::ColorEdit3("Origin Color", (float*)&data->GridColor, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    ImGui::ColorEdit3("Boundary Color", (float*)&data->GridColor100, ImGuiColorEditFlags_NoInputs);
    if (data->NumPointsPerDepth.size() > 0 && data->NumLinesPerDepth.size() > 0) {
      float linesPerPoints[data->NumPointsPerDepth.size()];
      int summedPoints = 0;
      int summedLines = 0;
      for (unsigned i = 0; i < data->NumPointsPerDepth.size(); ++i) {
        summedPoints += data->NumPointsPerDepth[i];
        summedLines += data->NumLinesPerDepth[i];
        linesPerPoints[i] = summedLines * 1.0f / summedPoints;
      }
      ImGui::PlotLines("##Edges per Node over Depth, ", linesPerPoints, IM_COUNTOF(linesPerPoints), 0, 0, 0.0f, FLT_MAX, ImVec2(100, 50));
      ImGui::SameLine();
      ImGui::Text("%.1f Edges per Node, %d Nodes, %d Edges", linesPerPoints[data->NumPointsPerDepth.size() - 1], summedPoints, summedLines);
    }
}

static void ExampleImageViewer_DrawCanvas(ExampleImageViewerData* data, ImVec2 canvas_size)
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    IM_ASSERT(canvas_size.x >= 0.0f && canvas_size.y >= 0.0f);

    // Layout canvas
    ImGui::InvisibleButton("##Canvas", canvas_size);
    ImVec2 canvas_min = ImGui::GetItemRectMin();
    ImVec2 canvas_max = ImGui::GetItemRectMax();

    if (data->ViewReset)
        data->ViewOffset = ImVec2(0, 0);
    data->ViewReset = false;

    // Handle inputs
    if (ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY))
        if (io.MouseWheel != 0.0f)
            data->Zoom = IM_CLAMP(data->Zoom * (1.0f + io.MouseWheel * 0.10f), data->ZoomMin, data->ZoomMax);
    float zoom = data->Zoom; // (float)(int)ViewZoom;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
    {
        data->ViewOffset.x -= io.MouseDelta.x / zoom;
        data->ViewOffset.y -= io.MouseDelta.y / zoom;
    }

    // Display image
    ImVec2 origin;
    origin.x = (float)(int)((canvas_min.x - (data->ViewOffset.x * zoom)) + (canvas_size.x * 0.5f));
    origin.y = (float)(int)((canvas_min.y - (data->ViewOffset.y * zoom)) + (canvas_size.y * 0.5f));
    draw_list->AddRect(ImVec2(canvas_min.x - 1.0f, canvas_min.y - 1.0f), ImVec2(canvas_max.x + 1.0f, canvas_max.y + 1.0f), IM_COL32(255, 255, 255, 255));
    draw_list->PushClipRect(canvas_min, canvas_max, true);

    // Display grid lines for visible pixels
    const float step = (float)zoom;
    const float step2 = (float)step / 2.0;
    const float stepsqrt32 = (float)step * sqrt(3) / 2.0;
    const float stepsqrt22 = (float)step * sqrt(2) / 2.0;
    const float step51 = (float)step * (sqrt(5) + 1) / 4.0;
    const float step52 = (float)step * sqrt(10 - 2*sqrt(5)) / 4.0;
    const float step53 = (float)step * (sqrt(5) - 1) / 4.0;
    const float step54 = (float)step * sqrt(10 + 2*sqrt(5)) / 4.0;
    std::vector<std::vector<float> > base_vectors;
    std::vector<std::vector<int> > neighbors;
    if (data->ConnectivityType == 0) {
      base_vectors = {{step, 0}, {0, step}};
      neighbors = {{1, 0}, {0, 1}};
    } else if (data->ConnectivityType == 1) {
      base_vectors = {{step, 0}, {step2, stepsqrt32}};
      neighbors = {{1, 0}, {0, 1}, {-1, 1}};
    } else if (data->ConnectivityType == 2) {
      base_vectors = {{step, 0}, {0, step}, {stepsqrt22, stepsqrt22}, {stepsqrt22, -stepsqrt22}};
      neighbors = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    } else if (data->ConnectivityType == 3) {
      base_vectors = {{step, 0}, {step51, step52}, {step53, step54}, {-step53, step54}, {-step51, step52}};
      neighbors = {{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, 1, 0}, {0, 0, 0, 0, 1}};
    } else if (data->ConnectivityType == 4) {
      base_vectors = {{step, 0}, {0, step}, {stepsqrt32, -step2}, {step2, stepsqrt32}};
      neighbors = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}, {0, 1, 1, 0}, {1, 0, 0, -1}};
    }
    if( data->ShowUnitCircle) {
      draw_list->AddCircle(origin, step, IM_COL32(255, 255, 0, 255), 128, 2.0f);
    }
      int GridDepth = data->GridDepth;
      data->NumPointsPerDepth.clear();
      data->NumPointsPerDepth.resize(GridDepth + 1, 0);
      data->NumLinesPerDepth.clear();
      data->NumLinesPerDepth.resize(GridDepth + 1, 0);
      ImVec2 point_min, point_max;
      std::vector<int> pos;
      pos.resize(base_vectors.size(), -GridDepth);
      while (true) {
        // check position
        int links = 0;
        for (unsigned i = 0; i < pos.size(); ++i) {
          links += abs(pos[i]);
        }
        for (unsigned i = 0; i < neighbors.size(); ++i) {
          int num = 0;
          for (unsigned j = 0; j < neighbors[i].size(); ++j) {
            if (neighbors[i][j] == 0 || pos[j] == 0) continue;
            if (num != 0) {
              if (num * pos[j] * neighbors[i][j] > 0) {
                links -= IM_MIN(abs(num), abs(pos[j] * neighbors[i][j]));
              }
            } else {
              num = pos[j] * neighbors[i][j];
            }
          }
        }
        if (links <= GridDepth) {
          // and draw
          point_min.x = origin.x - 1;
          point_min.y = origin.y - 1;
          for (unsigned i = 0; i < pos.size(); ++i) {
            point_min.x += pos[i] * base_vectors[i][0];
            point_min.y += pos[i] * base_vectors[i][1];
          }
          point_max.x = point_min.x + 3;
          point_max.y = point_min.y + 3;
          draw_list->AddRectFilled(point_min, point_max, ImColor(ImLerp(data->GridColor, data->GridColor100, GridDepth > 0 ? links * 1.0f / GridDepth : 0.0f)));
          ++data->NumPointsPerDepth[links];

          if (data->ShowLines) {
            point_min.x += 1;
            point_min.y += 1;
            for (unsigned i = 0; i < neighbors.size(); ++i) {
              std::vector<int> pos2(pos.size());
              int links2 = 0;
              for (unsigned j = 0; j < pos2.size(); ++j) {
                pos2[j] = pos[j] + neighbors[i][j];
                links2 += abs(pos2[j]);
              }
              for (unsigned j = 0; j < neighbors.size(); ++j) {
                int num = 0;
                for (unsigned k = 0; k < neighbors[j].size(); ++k) {
                  if (neighbors[j][k] == 0 || pos2[k] == 0) continue;
                  if (num != 0) {
                    if (num * pos2[k] * neighbors[j][k] > 0) {
                      links2 -= IM_MIN(abs(num), abs(pos2[k] * neighbors[j][k]));
                    }
                  } else {
                    num = pos2[k] * neighbors[j][k];
                  }
                }
              }
              if (links2 > GridDepth) continue;

              point_max.x = origin.x;
              point_max.y = origin.y;
              for (unsigned j = 0; j < pos2.size(); ++j) {
                point_max.x += pos2[j] * base_vectors[j][0];
                point_max.y += pos2[j] * base_vectors[j][1];
              }

              draw_list->AddLine(point_min, point_max, ImColor(ImLerp(data->GridColor, data->GridColor100, (links + links2) * 0.5f / GridDepth)));
              ++data->NumLinesPerDepth[IM_MAX(links, links2)];
            }
          }
        }

        // increment to next possible position
        int i = pos.size() - 1;
        while (i >= 0 && pos[i] == GridDepth) --i;
        if (i < 0) break; // done
        ++pos[i];
        for (++i; i < (signed)pos.size(); ++i) {
          pos[i] = -GridDepth;
        }
      }
    draw_list->PopClipRect();
}

static void ShowExampleAppSimpleOverlay(bool* p_open)
{
    const float DISTANCE = 10.0f;
    static int corner = 1;
    ImGuiIO& io = ImGui::GetIO();
    if (corner != -1)
    {
        ImVec2 window_pos = ImVec2((corner & 1) ? io.DisplaySize.x - DISTANCE : DISTANCE, (corner & 2) ? io.DisplaySize.y - DISTANCE : DISTANCE);
        ImVec2 window_pos_pivot = ImVec2((corner & 1) ? 1.0f : 0.0f, (corner & 2) ? 1.0f : 0.0f);
        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
    }
    ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (corner != -1)
        window_flags |= ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("Simple overlay", p_open, window_flags))
    {
        if (ImGui::IsMousePosValid())
            ImGui::Text("Mouse Position: (%.1f,%.1f)", io.MousePos.x, io.MousePos.y);
        else
            ImGui::Text("Mouse Position: <invalid>");

        ImGui::Separator();
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

        if (ImGui::BeginPopupContextWindow())
        {
            if (ImGui::MenuItem("Custom",       NULL, corner == -1)) corner = -1;
            if (ImGui::MenuItem("Top-left",     NULL, corner == 0)) corner = 0;
            if (ImGui::MenuItem("Top-right",    NULL, corner == 1)) corner = 1;
            if (ImGui::MenuItem("Bottom-left",  NULL, corner == 2)) corner = 2;
            if (ImGui::MenuItem("Bottom-right", NULL, corner == 3)) corner = 3;
            if (p_open && ImGui::MenuItem("Close")) *p_open = false;
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

// Main code
int main(int, char**)
{
    // Setup SDL
    // [If using SDL_MAIN_USE_CALLBACKS: all code below until the main loop starts would likely be your SDL_AppInit() function]
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return 1;
    }

    // Create SDL window graphics context
    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window = SDL_CreateWindow("Unit Distance Graph Viewer", (int)(1280 * main_scale), (int)(800 * main_scale), window_flags);
    if (window == nullptr)
    {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    // Create GPU Device
    SDL_GPUDevice* gpu_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB, true, nullptr);
    if (gpu_device == nullptr)
    {
        printf("Error: SDL_CreateGPUDevice(): %s\n", SDL_GetError());
        return 1;
    }

    // Claim window for GPU Device
    if (!SDL_ClaimWindowForGPUDevice(gpu_device, window))
    {
        printf("Error: SDL_ClaimWindowForGPUDevice(): %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetGPUSwapchainParameters(gpu_device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLGPU(window);
    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.Device = gpu_device;
    init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(gpu_device, window);
    init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;                      // Only used in multi-viewports mode.
    init_info.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;  // Only used in multi-viewports mode.
    init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    ImGui_ImplSDLGPU3_Init(&init_info);

    // Our state
    bool show_image_viewer = true;
    bool show_overlay = true;
    ImVec4 clear_color = ImVec4(0.1f, 0.1, 0.1, 1.00f);

    // Main loop
    bool done = false;
    while (!done)
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        // [If using SDL_MAIN_USE_CALLBACKS: call ImGui_ImplSDL3_ProcessEvent() from your SDL_AppEvent() function]
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppIterate() function]
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // 1. Viewport-Größe abrufen
        ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
        ImVec2 viewportPos = ImGui::GetMainViewport()->Pos;

        // 2. Nächstes Fenster auf Viewport-Größe setzen
        ImGui::SetNextWindowPos(viewportPos);
        ImGui::SetNextWindowSize(viewportSize);

        ImGui::Begin("Plane Viewer", &show_image_viewer, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        static ExampleImageViewerData image_viewer;
        ExampleImageViewer_DrawOptions(&image_viewer);
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        ExampleImageViewer_DrawCanvas(&image_viewer, canvas_size);
        ImGui::End();
        
        //ImGui::ShowDemoWindow();

        ShowExampleAppSimpleOverlay(&show_overlay);
        
        // Rendering
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);

        SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(gpu_device); // Acquire a GPU command buffer

        SDL_GPUTexture* swapchain_texture;
        SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, nullptr, nullptr); // Acquire a swapchain texture

        if (swapchain_texture != nullptr && !is_minimized)
        {
            // This is mandatory: call ImGui_ImplSDLGPU3_PrepareDrawData() to upload the vertex/index buffer!
            ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);

            // Setup and start a render pass
            SDL_GPUColorTargetInfo target_info = {};
            target_info.texture = swapchain_texture;
            target_info.clear_color = SDL_FColor { clear_color.x, clear_color.y, clear_color.z, clear_color.w };
            target_info.load_op = SDL_GPU_LOADOP_CLEAR;
            target_info.store_op = SDL_GPU_STOREOP_STORE;
            target_info.mip_level = 0;
            target_info.layer_or_depth_plane = 0;
            target_info.cycle = false;
            SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(command_buffer, &target_info, 1, nullptr);

            // Render ImGui
            ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, render_pass);

            SDL_EndGPURenderPass(render_pass);
        }

        // Submit the command buffer
        SDL_SubmitGPUCommandBuffer(command_buffer);
    }

    // Cleanup
    // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppQuit() function]
    SDL_WaitForGPUIdle(gpu_device);
    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();

    SDL_ReleaseWindowFromGPUDevice(gpu_device, window);
    SDL_DestroyGPUDevice(gpu_device);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
