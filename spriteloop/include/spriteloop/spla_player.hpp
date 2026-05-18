#pragma once

#include "spriteloop/spla_package.hpp"

#include <cstddef>
#include <string>

namespace spriteloop {

class SplaPlayer {
public:
    explicit SplaPlayer(const SplaPackage& package) noexcept;

    [[nodiscard]] const SplaPackage& package() const noexcept;

    [[nodiscard]] bool play(const std::string& animation_id);
    void stop() noexcept;
    void update(float delta_seconds) noexcept;
    void set_time(float seconds) noexcept;
    void set_frame(int frame_index) noexcept;
    void set_loop_override(bool loop) noexcept;
    void clear_loop_override() noexcept;

    [[nodiscard]] bool is_playing() const noexcept;
    [[nodiscard]] float time() const noexcept;
    [[nodiscard]] int current_frame_index() const noexcept;
    [[nodiscard]] bool effective_loop() const noexcept;
    [[nodiscard]] const SplaAnimation* current_animation() const noexcept;
    [[nodiscard]] const SplaFrame* current_frame() const noexcept;

private:
    [[nodiscard]] const SplaAnimation* find_animation(const std::string& animation_id) const noexcept;
    [[nodiscard]] int frame_index_for_time(const SplaAnimation& animation) const noexcept;
    void sync_frame_to_time() noexcept;

    const SplaPackage* package_ = nullptr;
    std::size_t current_animation_index_ = invalid_index;
    double elapsed_seconds_ = 0.0;
    int current_frame_index_ = 0;
    bool playing_ = false;
    bool loop_override_ = false;
    bool has_loop_override_ = false;

    static constexpr std::size_t invalid_index = static_cast<std::size_t>(-1);
};

} // namespace spriteloop
