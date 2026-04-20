#pragma once

#include "core/scene.h"
#include "renderer/render_config.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Maps string labels → scene-builder functions. Adding a new scene is
// two steps: write a build_scene_<label>() function, and call
// SceneRegistry::instance().register_scene("<label>", build_scene_<label>)
// from register_all_scenes() in scene_registry.cpp.
class SceneRegistry {
public:
    using Builder = std::function<void(Scene&, const RenderConfig&)>;

    static SceneRegistry& instance();

    void register_scene(const std::string& label, Builder fn);
    bool build(const std::string& label, Scene& scene, const RenderConfig& cfg) const;
    bool has(const std::string& label) const;
    std::vector<std::string> labels() const;

private:
    SceneRegistry() = default;
    std::unordered_map<std::string, Builder> builders_;
};

// Populates the registry with every built-in scene. Call once at startup.
void register_all_scenes();

// Individual scene builder declarations. Each lives in its own .cpp file.
void build_scene_cornell_box(Scene& scene, const RenderConfig& cfg);
void build_scene_spheres(Scene& scene, const RenderConfig& cfg);
void build_scene_ocean(Scene& scene, const RenderConfig& cfg);
