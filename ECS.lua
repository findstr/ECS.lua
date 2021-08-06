local ECS = {}
local c = require "ecs.c"
local worlds = {}

function ECS.fetch_world(name)
	local w = worlds[name]
	if not w then
		w = c.create_world()
		worlds[name] = w
	end
	return w
end

return ECS


