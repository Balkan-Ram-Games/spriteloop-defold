local native = rawget(_G, "spla_native")

-- Low-level SpriteLoop package API.
--
-- This module wraps the native spla_native table and keeps a small Lua fallback so scripts can
-- still load enough information to fail gracefully when the native extension is unavailable.
local M = {}

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
        return native.load_bytes(path, bytes)
    end

    print("spla.load Lua placeholder: " .. path .. " (" .. #bytes .. " bytes)")
    return {
        path = path,
        byte_count = #bytes,
        loaded = true,
        native = false,
    }
end

-- Destroys a handle returned by load.
-- handle is a native userdata when the extension is loaded; Lua fallback tables are ignored.
function M.destroy(handle)
    if native and native.destroy then
        return native.destroy(handle)
    end
end

-- Starts playback of animation_id on handle and returns true when the animation exists.
function M.play(handle, animation_id)
    if native and native.play then
        return native.play(handle, animation_id)
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

-- Sets absolute playback time in seconds.
function M.set_time(handle, seconds)
    if native and native.set_time then
        return native.set_time(handle, seconds)
    end
end

-- Sets the current frame index in the active animation.
function M.set_frame(handle, frame_index)
    if native and native.set_frame then
        return native.set_frame(handle, frame_index)
    end
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

-- Returns debug/package/playback information for handle.
function M.get_info(handle)
    if native and native.get_info then
        return native.get_info(handle)
    end

    return handle
end

return M
