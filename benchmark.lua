local use_ecs = ...
local ECS = require "ECS"
local json = require "json"

local w = ECS.fetch_world("Admin")

w:register("vector2", {
	x = 0,
	y = 0,
})

w:register("vector3", {
	x = 0,
	y = 0,
	z = 0
})

local test_count = 10000
local x = 0

if use_ecs then
	for i = 1, test_count do
		w:new {
			vector2 = {x = i, y = i},
			--vector3 = {x = i, y = i, z = i}
		}
	end
	for k = 1, 1000 do
		local list, n = w:select("vector2")
		--[[
		for i = 1, n do
			local e = list[i]
			x = x + e.vector2.x
		end
		]]
	end
else
	local tbl = {}
	for i = 1, test_count do
		tbl[i] = {
			vector2 = {x = i, y = i}
		}
	end
	for k = 1, 1000 do
		for i = 1, test_count do
			local e = tbl[i]
			tbl[i] = e
			--x = x + e.vector2.x
		end
	end
end

