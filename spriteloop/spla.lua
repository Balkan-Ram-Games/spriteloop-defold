local native = rawget(_G, "spla_native")

-- Low-level SpriteLoop package API.
--
-- This module wraps the native spla_native table and keeps a small Lua fallback so scripts can
-- still load enough information to fail gracefully when the native extension is unavailable.
local M = {}

-- Loads a .spla package from bytes already read by the caller.
-- path is retained for diagnostics and may be an absolute external file path.
function M.load_bytes(path, bytes)
    if type(path) ~= "string" or path == "" then
        error("spla.load_bytes path must be a non-empty string")
    end
    if type(bytes) ~= "string" then
        error("spla.load_bytes bytes must be a string")
    end
    if native and native.load_bytes then
        return native.load_bytes(path, bytes)
    end
    error("spla.load_bytes requires the SpriteLoop native extension")
end

-- Returns the native extension version string.
function M.version()
    if native and native.version then
        return native.version()
    end

    return "0.1.0-lua-placeholder"
end

-- Loads a .spla package from a Defold resource path.
-- path is normally a compiled project path such as "/example/assets/robot_idle.spla".
function M.load(path)
    local bytes, err = sys.load_resource(path)
    if not bytes then
        error("failed to load SpriteLoop package resource '" .. path .. "': " .. tostring(err))
    end

    if native and native.load_bytes then
        return M.load_bytes(path, bytes)
    end

    print("spla.load Lua placeholder: " .. path .. " (" .. #bytes .. " bytes)")
    return {
        path = path,
        byte_count = #bytes,
        loaded = true,
        native = false,
        tint_r = 1,
        tint_g = 1,
        tint_b = 1,
    }
end

-- Destroys a handle returned by load.
-- handle is a native userdata when the extension is loaded; Lua fallback tables are ignored.
function M.destroy(handle)
    if native and native.destroy then
        return native.destroy(handle)
    end
end

-- Starts playback. options can include { emit_events = false }.
function M.play(handle, animation_id, options)
    if native and native.play then
        return native.play(handle, animation_id, options)
    end

    return false
end

-- Stops playback on handle.
function M.stop(handle)
    if native and native.stop then
        return native.stop(handle)
    end
end

-- Advances handle playback by dt seconds.
function M.update(handle, dt)
    if native and native.update then
        return native.update(handle, dt)
    end
end

-- Sets absolute playback time. Destination events emit by default.
function M.set_time(handle, seconds, options)
    if native and native.set_time then
        return native.set_time(handle, seconds, options)
    end
end

-- Sets the current frame index. Destination events emit by default.
function M.set_frame(handle, frame_index, options)
    if native and native.set_frame then
        return native.set_frame(handle, frame_index, options)
    end
end

-- Returns and clears playback events collected by previous update calls.
function M.consume_events(handle)
    if native and native.consume_events then
        return native.consume_events(handle)
    end

    return {}
end

-- Sets the standalone instance position used by the low-level renderer path.
-- Component rendering uses game object/component transforms instead.
function M.set_position(handle, x, y)
    if native and native.set_position then
        return native.set_position(handle, x, y)
    end

    if type(handle) == "table" then
        handle.x = x
        handle.y = y
    end
end

-- Sets standalone instance scale. scale_y defaults to scale_x when omitted.
function M.set_scale(handle, scale_x, scale_y)
    if native and native.set_scale then
        return native.set_scale(handle, scale_x, scale_y)
    end

    if type(handle) == "table" then
        handle.scale_x = scale_x
        handle.scale_y = scale_y or scale_x
    end
end

-- Sets whether the handle should render.
function M.set_visible(handle, visible)
    if native and native.set_visible then
        return native.set_visible(handle, visible)
    end

    if type(handle) == "table" then
        handle.visible = visible
    end
end

-- Sets the active runtime skin by id or name.
function M.set_skin(handle, skin_id)
    if native and native.set_skin then
        return native.set_skin(handle, skin_id)
    end

    return false
end

-- Overrides one part to render a specific variant id, key, or name.
function M.set_variant(handle, part_id, variant_id)
    if native and native.set_variant then
        return native.set_variant(handle, part_id, variant_id)
    end

    return false
end

-- Clears one part variant override by part id, key, or name.
function M.clear_variant(handle, part_id)
    if native and native.clear_variant then
        return native.clear_variant(handle, part_id)
    end

    return false
end

-- Clears all part variant overrides.
function M.clear_variants(handle)
    if native and native.clear_variants then
        return native.clear_variants(handle)
    end
end

-- Sets a whole-character runtime RGB tint multiplier.
function M.set_tint(handle, r, g, b)
    if native and native.set_tint then
        return native.set_tint(handle, r, g, b)
    end

    if type(handle) == "table" then
        handle.tint_r = math.max(0, math.min(r, 1))
        handle.tint_g = math.max(0, math.min(g, 1))
        handle.tint_b = math.max(0, math.min(b, 1))
    end
end

-- Resets the whole-character runtime tint multiplier to white.
function M.clear_tint(handle)
    if native and native.clear_tint then
        return native.clear_tint(handle)
    end

    if type(handle) == "table" then
        handle.tint_r = 1
        handle.tint_g = 1
        handle.tint_b = 1
    end
end

-- Returns debug/package/playback information for handle.
function M.get_info(handle)
    if native and native.get_info then
        return native.get_info(handle)
    end

    return handle
end

return M
