local native = rawget(_G, "spriteloop_native")

-- Component-oriented SpriteLoop API.
--
-- Scripts should use this module with component URLs, for example
-- spriteloop.play_anim("#robot", "idle"). The native table is registered by the C++ extension.
local M = {}

-- Calls a native component function by name.
-- name must exist in spriteloop_native; extra arguments are forwarded unchanged.
local function call(name, ...)
    if not native or not native[name] then
        error("spriteloop native function unavailable: " .. name)
    end
    return native[name](...)
end

-- Plays an animation id or name on the SpriteLoop component at url.
-- options can include { loop = true } or { loop = false }.
function M.play_anim(url, animation_id, options)
    return call("play_anim", url, animation_id, options)
end

-- Stops animation playback on the component at url.
function M.stop_anim(url)
    return call("stop_anim", url)
end

-- Sets absolute playback time in seconds on the component at url.
function M.set_time(url, seconds)
    return call("set_time", url, seconds)
end

-- Sets the current animation frame index on the component at url.
function M.set_frame(url, frame_index)
    return call("set_frame", url, frame_index)
end

-- Returns and clears playback events collected by previous component updates.
function M.consume_events(url)
    return call("consume_events", url)
end

-- Sets the per-component playback rate multiplier.
function M.set_playback_rate(url, rate)
    return call("set_playback_rate", url, rate)
end

-- Sets component visibility without deleting or recreating the component.
function M.set_visible(url, visible)
    return call("set_visible", url, visible)
end

-- Sets the active runtime skin by id.
function M.set_skin(url, skin_id)
    return call("set_skin", url, skin_id)
end

-- Overrides one part to render a specific variant id.
function M.set_variant(url, part_id, variant_id)
    return call("set_variant", url, part_id, variant_id)
end

-- Clears one part variant override.
function M.clear_variant(url, part_id)
    return call("clear_variant", url, part_id)
end

-- Clears all part variant overrides.
function M.clear_variants(url)
    return call("clear_variants", url)
end

-- Debug helper: releases one SpriteLoop component runtime instance.
-- This is intended for lifecycle/cache diagnostics, not normal gameplay.
function M.debug_destroy_component(url)
    return call("debug_destroy_component", url)
end

-- Returns debug/package/playback information for the component at url.
function M.get_info(url)
    return call("get_info", url)
end

-- Returns shared package/texture cache information for diagnostics.
function M.get_cache_info()
    return call("get_cache_info")
end

return M
