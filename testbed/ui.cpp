#include "app.hpp"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <string>

namespace testbed
{

namespace
{

void text(const std::string& line)
{
   ImGui::TextUnformatted(line.c_str());
}

}  // namespace

void draw_ui(App& app)
{
   ImGui::SetNextWindowPos({12.0f, 12.0f}, ImGuiCond_FirstUseEver);
   ImGui::SetNextWindowBgAlpha(0.85f);
   ImGui::Begin("tinyverlet", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

   // scene
   const auto names = scene_names();
   const std::string current{names[app.scene_index]};
   if (ImGui::BeginCombo("scene", current.c_str()))
   {
      for (std::size_t i = 0; i < names.size(); ++i)
      {
         const std::string name{names[i]};
         if (ImGui::Selectable(name.c_str(), i == app.scene_index))
         {
            app.load_scene(i);
         }
      }
      ImGui::EndCombo();
   }
   if (ImGui::Button("reset"))
   {
      app.load_scene(app.scene_index);
   }
   ImGui::SameLine();
   if (ImGui::Button("throw ball"))
   {
      app.throw_ball();
   }
   ImGui::SameLine();
   ImGui::Checkbox("pause", &app.paused);
   if (app.paused)
   {
      ImGui::SameLine();
      if (ImGui::Button("step"))
      {
         app.step();
      }
   }

   // physics
   auto& settings = app.scene.world.settings;
   ImGui::SeparatorText("physics");
   ImGui::SliderFloat("gravity", &settings.gravity.y, -20.0f, 0.0f, "%.1f");
   ImGui::SliderFloat("damping", &settings.damping, 0.95f, 1.0f, "%.3f");
   ImGui::SliderInt("iterations", &settings.iterations, 1, 32);
   ImGui::SliderFloat("restitution", &settings.restitution, 0.0f, 1.0f, "%.2f");
   ImGui::SliderFloat("friction", &settings.friction, 0.0f, 1.0f, "%.2f");

   bool tearing = settings.tear_ratio > 1.0f;
   if (ImGui::Checkbox("tearing", &tearing))
   {
      settings.tear_ratio = tearing ? 1.6f : 0.0f;
   }
   if (tearing)
   {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
      ImGui::SliderFloat("##tear", &settings.tear_ratio, 1.1f, 4.0f, "at %.1fx");
   }

   bool self_collision = settings.particle_radius > 0.0f;
   if (ImGui::Checkbox("self-collision", &self_collision))
   {
      settings.particle_radius = self_collision ? app.collision_radius : 0.0f;
   }
   if (self_collision)
   {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
      if (ImGui::SliderFloat("##radius", &app.collision_radius, 0.005f, 0.08f, "r %.3f"))
      {
         settings.particle_radius = app.collision_radius;
      }
   }

   if (!app.scene.cloths.empty())
   {
      ImGui::Checkbox("wind", &app.wind_enabled);
      if (app.wind_enabled)
      {
         ImGui::SameLine();
         ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
         ImGui::SliderFloat("##wind", &app.wind_strength, 0.0f, 12.0f, "%.1f");
      }
   }

   // display
   ImGui::SeparatorText("display");
   ImGui::Checkbox("surfaces", &app.display.surfaces);
   ImGui::SameLine();
   ImGui::Checkbox("constraints", &app.display.constraints);
   ImGui::SameLine();
   ImGui::Checkbox("particles", &app.display.particles);
   ImGui::ColorEdit3("cloth", app.display.cloth_color.data(), ImGuiColorEditFlags_NoInputs);

#ifndef __EMSCRIPTEN__
   ImGui::SameLine();
   if (ImGui::Button("screenshot"))
   {
      int width = 0;
      int height = 0;
      glfwGetFramebufferSize(app.window, &width, &height);
      auto file = std::format("tinyverlet_{}.png", names[app.scene_index]);
      std::ranges::replace(file, ' ', '_');
      app.status = app.renderer->screenshot(file, width, height, app.camera, app.scene, app.display)
                       ? "saved " + file
                       : "could not save " + file;
   }
#endif

   // numbers
   ImGui::SeparatorText("stats");
   const auto& world = app.scene.world;
   text(std::format("particles    {:>6}", world.particles.size()));
   text(std::format("constraints  {:>6}   torn {}", world.constraints.size(), app.torn));
   text(std::format("step()       {:.3f} ms at {:.0f} Hz", app.step_milliseconds, 1.0f / step_dt));
   if (app.show_fps)
   {
      text(std::format("frame        {:.1f} fps", ImGui::GetIO().Framerate));
   }
   if (!app.status.empty())
   {
      ImGui::TextDisabled("%s", app.status.c_str());
   }

   if (ImGui::CollapsingHeader("controls"))
   {
      text("drag a particle   pull it (and tear)");
      text("drag elsewhere    orbit");
      text("wheel             zoom");
      text("b                 throw a ball");
      text("space             pause");
      text("r                 reset scene");
      text("tab               next scene");
   }

   ImGui::End();
}

}  // namespace testbed
