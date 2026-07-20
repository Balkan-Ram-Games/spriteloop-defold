local native = rawget(_G, "spriteloop_native")

-- Component-oriented SpriteLoop API.
--
-- Scripts should use this module with component URLs, for example
-- spriteloop.play_anim("#robot", "idle"). The native table is registered by the C++ extension.
local M = {}
local listeners = {}
local next_listener_id = 1

-- Calls a native component function by name.
-- name must exist in spriteloop_native; extra arguments are forwarded unchanged.
local function call(name, ...)
    if not native or not native[name] then
        error("spriteloop native function unavailable: " .. name)
    end
    return native[name](...)
end

-- Plays an animation id or name on the SpriteLoop component at url.
-- options can include loop and emit_events booleans.
function M.play_anim(url, animation_id, options)
    return call("play_anim", url, animation_id, options)
end

-- Replaces a component's package with an arbitrary .spla byte string at runtime.
-- This is intended for tools and preview workflows; path is used for diagnostics.
function M.load_bytes(url, path, bytes)
    return call("load_bytes", url, path, bytes)
end

-- Stops animation playback on the component at url.
function M.stop_anim(url)
    return call("stop_anim", url)
end

-- Sets absolute playback time in seconds. Destination events emit by default.
function M.set_time(url, seconds, options)
    return call("set_time", url, seconds, options)
end

-- Sets the current animation frame index. Destination events emit by default.
function M.set_frame(url, frame_index, options)
    return call("set_frame", url, frame_index, options)
end

-- Returns and clears playback events collected by previous component updates.
function M.consume_events(url)
    return call("consume_events", url)
end

local function listener_key(url)
    return tostring(url)
end

-- Registers a Lua callback for an event name on one SpriteLoop component.
-- Returns a numeric listener id that can be passed to unlisten.
function M.listen(url, event_name, callback)
    if type(callback) ~= "function" then
        error("spriteloop.listen callback must be a function")
    end
    if event_name == nil or event_name == "" then
        error("spriteloop.listen event_name must be specified")
    end

    local key = listener_key(url)
    local listener_id = next_listener_id
    next_listener_id = next_listener_id + 1
    listeners[key] = listeners[key] or {}
    listeners[key][event_name] = listeners[key][event_name] or {}
    table.insert(listeners[key][event_name], {
        id = listener_id,
        callback = callback,
    })

    return listener_id
end

-- Removes listeners for a component. Pass only url to remove every listener for the component,
-- pass an event name to remove all callbacks for that event, or pass a listener id to remove one
-- registered callback.
function M.unlisten(url, listener_or_event_name)
    local key = listener_key(url)
    local component_listeners = listeners[key]
    if not component_listeners then
        return false
    end

    if listener_or_event_name == nil then
        listeners[key] = nil
        return true
    end

    if type(listener_or_event_name) == "number" then
        local removed = false
        for event_name, callbacks in pairs(component_listeners) do
            for index = #callbacks, 1, -1 do
                if callbacks[index].id == listener_or_event_name then
                    table.remove(callbacks, index)
                    removed = true
                end
            end
            if #callbacks == 0 then
                component_listeners[event_name] = nil
            end
        end
        return removed
    end

    local callbacks = component_listeners[listener_or_event_name]
    if callbacks then
        component_listeners[listener_or_event_name] = nil
        return true
    end
    return false
end

-- Consumes queued native events and dispatches matching Lua listeners.
function M.dispatch_events(url)
    local key = listener_key(url)
    local component_listeners = listeners[key]
    local events = M.consume_events(url)
    if not component_listeners then
        return events
    end

    for _, event in ipairs(events) do
        local event_name = event.event_name or event.name
        local callbacks = component_listeners[event_name]
        if callbacks then
            for _, listener in ipairs(callbacks) do
                listener.callback(event)
            end
        end
    end
    return events
end

-- Sets the per-component playback rate multiplier.
function M.set_playback_rate(url, rate)
    return call("set_playback_rate", url, rate)
end

-- Sets component visibility without deleting or recreating the component.
function M.set_visible(url, visible)
    return call("set_visible", url, visible)
end

-- Sets the active runtime skin by id or name.
function M.set_skin(url, skin_id_or_name)
    return call("set_skin", url, skin_id_or_name)
end

-- Overrides one part to render a specific variant id, key, or name.
function M.set_variant(url, part_id_key_or_name, variant_id_key_or_name)
    return call("set_variant", url, part_id_key_or_name, variant_id_key_or_name)
end

-- Clears one part variant override by part id, key, or name.
function M.clear_variant(url, part_id_key_or_name)
    return call("clear_variant", url, part_id_key_or_name)
end

-- Clears all part variant overrides.
function M.clear_variants(url)
    return call("clear_variants", url)
end

-- Sets a whole-character runtime RGB tint multiplier.
function M.set_tint(url, r, g, b)
    return call("set_tint", url, r, g, b)
end

-- Resets the whole-character runtime tint multiplier to white.
function M.clear_tint(url)
    return call("clear_tint", url)
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
