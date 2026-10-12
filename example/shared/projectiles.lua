local spriteloop = require "spriteloop.spriteloop"
local M = {}

-- Part transforms are component-local. All teaching actors use only a
-- component position offset, so the game object's matrix applies scale/facing.
function M.muzzle(component, part, offset)
    local marker = spriteloop.get_part_transform(component, part)
    if not marker then return end
    go.update_world_transform()
    local point = marker.position + offset
    local world = go.get_world_transform() * vmath.vector4(point.x, point.y, point.z, 1)
    return vmath.vector3(world.x, world.y, 0.1)
end

function M.fire(component, part, offset, direction)
    local position = M.muzzle(component, part, offset)
    if position then
        factory.create("#bullet_factory", position, nil,
            { velocity = vmath.vector3(direction * 260, 0, 0), visual = hash("turret_bullet") }, 0.22)
    end
end

-- Convert a world point to the GUI's design coordinates, including camera
-- movement and zoom. Without a scene camera the default projection is 1:1.
function M.screen_position(position)
    local cameras = camera.get_cameras()
    for i = #cameras, 1, -1 do
        if camera.get_enabled(cameras[i]) then
            local clip = camera.get_projection(cameras[i]) * camera.get_view(cameras[i])
                * vmath.vector4(position.x, position.y, position.z, 1)
            if math.abs(clip.w) < 0.00001 then return nil end
            return vmath.vector3((clip.x / clip.w + 1) * sys.get_config_number("display.width", 1280) * 0.5,
                (clip.y / clip.w + 1) * sys.get_config_number("display.height", 720) * 0.5, 0)
        end
    end
    return position
end

return M
