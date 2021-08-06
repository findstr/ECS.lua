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
w:register("vector4", {
	x = 0,
	y = 0,
	z = 0,
	w = 0,
})

local eid1 = w:new {
	vector2 = {
		x = 101,
		y = 102,
	},
	vector3 = {
		x = 103,
		y = 104,
		z = 105,
	},
}

local eid2 = w:new {
	vector3 = {
		x = 201,
		y = 202,
		z = 203,
	},
	vector4 = {
		x = 204,
		y = 205,
		z = 206,
		w = 207,
	},
}

local eid3 = w:new {
	vector2 = {
		x = 301,
		y = 302,
	},
	vector4 = {
		x = 303,
		y = 304,
		z = 305,
		w = 306,
	}
}

print("==============test get")
local vector2, vector3, vector4 = w:get(eid1, "vector2", "vector3", "vector4")
assert(vector2.x == 101 and vector3.x == 103 and (not vector4))
print("===============test match")
print("all vector4")
for v4 in w:match("all", "vector4") do
	print(v4.x)
end
print("all vector4 and vector3")
for v4 in w:match("all", "vector4", "vector3") do
	print(v4.x)
end
print("all dirty vector4")
for v4 in w:match("dirty", "vector4") do
	print(v4.x)
end
for v4 in w:match("all", "vector4") do
	print("touch", v4.x)
	w:touch(v4)
end
print("all dirty vector4 and vector3")
for v4 in w:match("dirty", "vector4", "vector3") do
	print(v4.x)
end
print("all dirty vector4 and vector3 and vector2")
for v4 in w:match("dirty", "vector4", "vector3", "vector2") do
	print(v4.x)
end
print("add vector2 for vector4 and vector3")
for v4 in w:match("dirty", "vector4", "vector3") do
	w:add(v4.__eid, "vector2", {x = 0, y = 0})
	print(v4.x)
end
print("remove vector2 for vector4 and vector3")
for v4 in w:match("dirty", "vector4", "vector3", "vector2") do
	w:remove(v4.__eid, "vector2")
	print(v4.x)
end
print("dirty vector4 vector3 vector2")
for v4 in w:match("dirty", "vector4", "vector3", "vector2") do
	print(v4.x)
end

w:del(eid2)

w:update()

print("=========")
w:dump()
print("=========")

print("dead")
for v4 in w:match("dead", "vector4") do
	print("dead", v4.x)
end

print("all vector4")
for v4 in w:match("all", "vector4") do
	print(v4.x)
end


