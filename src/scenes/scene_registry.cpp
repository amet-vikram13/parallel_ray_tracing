#include "scenes/scene_registry.h"
#include <algorithm>
#include <utility>

SceneRegistry& SceneRegistry::instance() {
    static SceneRegistry reg;
    return reg;
}

void SceneRegistry::register_scene(const std::string& label, Builder fn) {
    builders_[label] = std::move(fn);
}

bool SceneRegistry::build(const std::string& label, Scene& scene,
                          const RenderConfig& cfg) const {
    auto it = builders_.find(label);
    if (it == builders_.end()) return false;
    scene = Scene{};
    scene.name = label;
    it->second(scene, cfg);
    return true;
}

bool SceneRegistry::has(const std::string& label) const {
    return builders_.find(label) != builders_.end();
}

std::vector<std::string> SceneRegistry::labels() const {
    std::vector<std::string> out;
    out.reserve(builders_.size());
    for (const auto& kv : builders_) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

void register_all_scenes() {
    SceneRegistry& reg = SceneRegistry::instance();
    reg.register_scene("cornell_box", build_scene_cornell_box);
    reg.register_scene("spheres",     build_scene_spheres);
    reg.register_scene("ocean",       build_scene_ocean);
}
