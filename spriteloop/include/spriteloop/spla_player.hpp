#pragma once

#include "spriteloop/spla_package.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace spriteloop {

struct SplaPlaybackEvent {
    std::string animation_id;
    std::string animation_name;
    int frame_index = 0;
    int source_frame = 0;
    std::string name;
    std::string data;
};

class SplaPlayer {
public:
    static constexpr std::size_t default_max_pending_events = 256;

    explicit SplaPlayer(
        const SplaPackage& package,
        std::size_t max_pending_events = default_max_pending_events) noexcept;

    [[nodiscard]] const SplaPackage& package() const noexcept;

    [[nodiscard]] bool play(const std::string& animation_id, bool emit_events = true);
    void stop() noexcept;
    void update(float delta_seconds) noexcept;
    void set_time(float seconds, bool emit_events = true) noexcept;
    void set_frame(int frame_index, bool emit_events = true) noexcept;
    void set_loop_override(bool loop) noexcept;
    void clear_loop_override() noexcept;

    [[nodiscard]] bool is_playing() const noexcept;
    [[nodiscard]] float time() const noexcept;
    [[nodiscard]] int current_frame_index() const noexcept;
    [[nodiscard]] bool effective_loop() const noexcept;
    [[nodiscard]] const SplaAnimation* current_animation() const noexcept;
    [[nodiscard]] const SplaFrame* current_frame() const noexcept;
    [[nodiscard]] std::size_t pending_event_count() const noexcept;
    [[nodiscard]] std::size_t max_pending_events() const noexcept;
    [[nodiscard]] std::uint64_t dropped_event_count() const noexcept;
    [[nodiscard]] std::vector<SplaPlaybackEvent> consume_events();

private:
    [[nodiscard]] const SplaAnimation* find_animation(const std::string& animation_id) const noexcept;
    [[nodiscard]] int frame_index_for_time(const SplaAnimation& animation) const noexcept;
    [[nodiscard]] int raw_frame_index_for_time(const SplaAnimation& animation) const noexcept;
    void queue_crossed_events(const SplaAnimation& animation, int previous_raw_frame, int next_raw_frame);
    void queue_frame_events(const SplaAnimation& animation, int frame_index);
    void queue_event(SplaPlaybackEvent event);
    void sync_frame_to_time() noexcept;

    const SplaPackage* package_ = nullptr;
    std::size_t current_animation_index_ = invalid_index;
    double elapsed_seconds_ = 0.0;
    int current_frame_index_ = 0;
    bool playing_ = false;
    bool playback_events_enabled_ = true;
    bool loop_override_ = false;
    bool has_loop_override_ = false;
    std::size_t max_pending_events_ = default_max_pending_events;
    std::uint64_t dropped_event_count_ = 0;
    std::deque<SplaPlaybackEvent> pending_events_;

    static constexpr std::size_t invalid_index = static_cast<std::size_t>(-1);
};

} // namespace spriteloop
