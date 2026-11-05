// tinyverlet testbed: cloth, ropes and soft bodies on Verlet integration.
//
//   tinyverlet_testbed [--scene NAME] [--size WxH]
//                      [--screenshot out.png [--time SECONDS] [--ui]]
//                      [--record DIR [--frames N] [--fps F] [--orbit RADIANS_PER_SECOND] [--throw SECONDS]]
//
// --record writes DIR/frame_0000.png ... for tools/make_gif.py; --throw throws a
// ball from the camera at that time.
//
// The ImGui panel drives everything; with the mouse, drag a particle to pull it
// and drag elsewhere to orbit; wheel zooms, Space pauses, R resets, Tab switches scene.

#include "app.hpp"
#include "gl.hpp"
#include "renderer.hpp"
#include "scenes.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace
{

using testbed::App;

App* app_from(GLFWwindow* window)
{
   return static_cast<App*>(glfwGetWindowUserPointer(window));
}

void on_key(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
   if (action != GLFW_PRESS || ImGui::GetIO().WantCaptureKeyboard)
   {
      return;
   }
   auto& app = *app_from(window);
   switch (key)
   {
      case GLFW_KEY_SPACE: app.paused = !app.paused; break;
      case GLFW_KEY_B: app.throw_ball(); break;
      case GLFW_KEY_R: app.load_scene(app.scene_index); break;
      case GLFW_KEY_TAB: app.load_scene((app.scene_index + 1) % testbed::scene_names().size()); break;
      case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(window, GLFW_TRUE); break;
      default: break;
   }
}

void on_mouse_button(GLFWwindow* window, int button, int action, int /*mods*/)
{
   auto& app = *app_from(window);
   if (button != GLFW_MOUSE_BUTTON_LEFT)
   {
      return;
   }
   glfwGetCursorPos(window, &app.last_x, &app.last_y);
   if (action == GLFW_PRESS && !ImGui::GetIO().WantCaptureMouse)
   {
      app.orbiting = !app.try_grab(app.last_x, app.last_y);
   }
   else if (action == GLFW_RELEASE)
   {
      app.orbiting = false;
      app.release();
   }
}

void on_cursor(GLFWwindow* window, double x, double y)
{
   auto& app = *app_from(window);
   if (app.grab)
   {
      app.drag(x, y);
   }
   else if (app.orbiting)
   {
      app.camera.yaw += static_cast<float>(x - app.last_x) * 0.01f;
      app.camera.pitch = std::clamp(app.camera.pitch + static_cast<float>(y - app.last_y) * 0.01f, -1.5f, 1.5f);
   }
   app.last_x = x;
   app.last_y = y;
}

void on_scroll(GLFWwindow* window, double /*x*/, double y)
{
   if (ImGui::GetIO().WantCaptureMouse)
   {
      return;
   }
   auto& app = *app_from(window);
   app.camera.distance = std::clamp(app.camera.distance * (y > 0 ? 0.9f : 1.1f), 0.5f, 30.0f);
}

void init_imgui(GLFWwindow* window, bool install_callbacks)
{
   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGui::GetIO().IniFilename = nullptr;
   ImGui::StyleColorsDark();

   float scale_x = 1.0f;
   float scale_y = 1.0f;
   glfwGetWindowContentScale(window, &scale_x, &scale_y);
   ImGui::GetStyle().ScaleAllSizes(scale_x);
   ImGui::GetStyle().FontScaleDpi = scale_x;

   // with install_callbacks, ImGui's callbacks go on top of ours and forward to them
   ImGui_ImplGlfw_InitForOpenGL(window, install_callbacks);
#ifdef __EMSCRIPTEN__
   ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
#endif
   ImGui_ImplOpenGL3_Init("#version 300 es");
}

void shutdown_imgui()
{
   ImGui_ImplOpenGL3_Shutdown();
   ImGui_ImplGlfw_Shutdown();
   ImGui::DestroyContext();
}

void build_ui(App& app)
{
   ImGui_ImplOpenGL3_NewFrame();
   ImGui_ImplGlfw_NewFrame();
   ImGui::NewFrame();
   testbed::draw_ui(app);
   ImGui::Render();
}

void frame(void* user)
{
   auto& app = *static_cast<App*>(user);
   glfwPollEvents();

   static double last = glfwGetTime();
   const double now = glfwGetTime();
   app.advance(now - last);
   last = now;

   build_ui(app);

   int width = 0;
   int height = 0;
   glfwGetFramebufferSize(app.window, &width, &height);
   app.renderer->draw(width, height, app.camera, app.scene, app.display);
   ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
   glfwSwapBuffers(app.window);
}

struct Options
{
   std::size_t scene = 0;
   std::optional<std::string> screenshot;
   float time = 2.0f;
   bool ui = false;
   int width = 1280;
   int height = 720;
   std::optional<std::string> record;
   int frames = 72;
   float fps = 24.0f;
   float orbit = 0.0f;
   std::optional<float> throw_at;
};

template <class T>
bool parse(std::string_view text, T& value)
{
   const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
   return error == std::errc{} && end == text.data() + text.size();
}

std::optional<Options> parse_options(std::span<char*> args)
{
   Options options;
   for (std::size_t i = 1; i < args.size(); ++i)
   {
      const std::string_view arg = args[i];
      const std::string_view value = i + 1 < args.size() ? std::string_view{args[i + 1]} : std::string_view{};

      if (arg == "--scene")
      {
         const auto names = testbed::scene_names();
         const auto it = std::ranges::find(names, value);
         if (it == names.end())
         {
            std::println(stderr, "unknown scene '{}'", value);
            return std::nullopt;
         }
         options.scene = static_cast<std::size_t>(it - names.begin());
         ++i;
      }
      else if (arg == "--screenshot" && !value.empty())
      {
         options.screenshot = std::string{value};
         ++i;
      }
      else if (arg == "--time" && parse(value, options.time) && options.time >= 0.0f)
      {
         ++i;
      }
      else if (arg == "--size")
      {
         const auto x = value.find('x');
         if (x == std::string_view::npos || !parse(value.substr(0, x), options.width) ||
             !parse(value.substr(x + 1), options.height) || options.width <= 0 || options.height <= 0)
         {
            std::println(stderr, "--size expects WIDTHxHEIGHT");
            return std::nullopt;
         }
         ++i;
      }
      else if (arg == "--ui")
      {
         options.ui = true;
      }
      else if (arg == "--record" && !value.empty())
      {
         options.record = std::string{value};
         ++i;
      }
      else if (arg == "--frames" && parse(value, options.frames) && options.frames > 0)
      {
         ++i;
      }
      else if (arg == "--fps" && parse(value, options.fps) && options.fps > 0.0f)
      {
         ++i;
      }
      else if (arg == "--orbit" && parse(value, options.orbit))
      {
         ++i;
      }
      else if (float at = 0.0f; arg == "--throw" && parse(value, at) && at >= 0.0f)
      {
         options.throw_at = at;
         ++i;
      }
      else
      {
         std::println(stderr, "unknown or incomplete option {}", arg);
         return std::nullopt;
      }
   }
   return options;
}

int run(const Options& options)
{
   if (glfwInit() == GLFW_FALSE)
   {
      std::println(stderr, "glfwInit failed");
      return 1;
   }

   // OpenGL ES 3.0 natively, WebGL2 in the browser
   glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
   glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
   glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
   glfwWindowHint(GLFW_SAMPLES, 4);
   if (options.screenshot || options.record)
   {
      glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
   }

   App app;
   app.window = glfwCreateWindow(options.width, options.height, "tinyverlet testbed", nullptr, nullptr);
   if (app.window == nullptr)
   {
      std::println(stderr, "could not create an OpenGL ES 3.0 window");
      glfwTerminate();
      return 1;
   }
   glfwMakeContextCurrent(app.window);

#ifndef __EMSCRIPTEN__
   if (gladLoadGLES2(glfwGetProcAddress) == 0)
   {
      std::println(stderr, "could not load OpenGL ES 3.0 functions");
      glfwTerminate();
      return 1;
   }
   glfwSwapInterval(1);
#endif

   int exit_code = 0;
   try
   {
      app.renderer.emplace();
      app.load_scene(options.scene);

      if (options.record)
      {
         // fixed steps between frames, so recordings are reproducible
         std::filesystem::create_directories(*options.record);
         const int steps_per_frame =
             std::max(1, static_cast<int>(std::lround(1.0f / (options.fps * testbed::step_dt))));
         bool thrown = false;
         for (int frame_index = 0; frame_index < options.frames && exit_code == 0; ++frame_index)
         {
            const auto file = std::filesystem::path(*options.record) / std::format("frame_{:04}.png", frame_index);
            if (!app.renderer->screenshot(file, options.width, options.height, app.camera, app.scene, app.display))
            {
               std::println(stderr, "could not write {}", file.string());
               exit_code = 1;
            }
            for (int i = 0; i < steps_per_frame; ++i)
            {
               if (options.throw_at && !thrown && app.time >= *options.throw_at)
               {
                  app.throw_ball();
                  thrown = true;
               }
               app.step();
            }
            app.camera.yaw += options.orbit / options.fps;
         }
      }
      else if (options.screenshot)
      {
         // fixed steps, so screenshots are reproducible
         const auto steps = static_cast<int>(options.time / testbed::step_dt);
         for (int i = 0; i < steps; ++i)
         {
            app.step();
         }

         std::function<void()> overlay;
         if (options.ui)
         {
            init_imgui(app.window, false);
            app.show_fps = false;
            for (int i = 0; i < 3; ++i)  // a few frames so the auto-sized panel settles
            {
               build_ui(app);
            }
            overlay = []
            {
               ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            };
         }

         if (!app.renderer->screenshot(*options.screenshot, options.width, options.height, app.camera, app.scene,
                                       app.display, overlay))
         {
            std::println(stderr, "could not write {}", *options.screenshot);
            exit_code = 1;
         }
         if (options.ui)
         {
            shutdown_imgui();
         }
      }
      else
      {
         glfwSetWindowUserPointer(app.window, &app);
         glfwSetKeyCallback(app.window, on_key);
         glfwSetMouseButtonCallback(app.window, on_mouse_button);
         glfwSetCursorPosCallback(app.window, on_cursor);
         glfwSetScrollCallback(app.window, on_scroll);
         init_imgui(app.window, true);

#ifdef __EMSCRIPTEN__
         // hands control to the browser; App lives on because main never returns
         emscripten_set_main_loop_arg(frame, &app, 0, true);
#else
         while (glfwWindowShouldClose(app.window) == GLFW_FALSE)
         {
            frame(&app);
         }
         shutdown_imgui();
#endif
      }
   }
   catch (const std::exception& e)
   {
      std::println(stderr, "{}", e.what());
      exit_code = 1;
   }

   app.renderer.reset();  // GL objects go before the context
   glfwDestroyWindow(app.window);
   glfwTerminate();
   return exit_code;
}

}  // namespace

int main(int argc, char* argv[])
{
   const auto options = parse_options(std::span(argv, static_cast<std::size_t>(argc)));
   if (!options)
   {
      return 2;
   }
   return run(*options);
}
