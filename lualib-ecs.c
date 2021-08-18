#define LUA_LIB

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <lua.h>
#include <lauxlib.h>

#define TYPE_MIN_ID		1
#define TYPE_MAX_ID		255
#define TYPE_COUNT		256

#define ECS_WORLD		(1)

#define WORLD_PROTO_ID		1
#define WORLD_PROTO_DEFINE	2
#define WORLD_COMPONENTS	3
#define WORLD_MATCH_CTX		4
#define WORLD_KEY_EID		5
#define WORLD_KEY_TID		6
#define WORLD_UPVAL_N		6

#define LINK_NIL		(-1)
#define LINK_NONE		(-2)

#define ENTITY_MAX_COMPONENTS	(64)

#define array_size(a)		(sizeof(a) / sizeof(a[0]))

enum match_mode {
	MATCH_ALL = 0,
	MATCH_DIRTY = 1,
	MATCH_DEAD = 2,
};

struct component {
	int eid;
	int dirty_next;
	int dead_next;
};

struct component_pool {
	int cap;
	int free;
	int dirty_head;
	int dirty_tail;
	int dead_head;
	int dead_tail;
	struct component *buf;
};

struct component_ptr {
	int tid;
	int cid;
};

struct entity {
	int next;
	short cn;
	unsigned char components[32];
	int index[ENTITY_MAX_COMPONENTS];
};

struct match_ctx {
	struct world *w;
	int i;
	int kn;
	int keys[ENTITY_MAX_COMPONENTS];
};

struct world {
	int entity_cap;
	int entity_free; //TODO: make entity_free to fifo, this can recycle eid later to avoid some bug
	int entity_dead;
	int type_idx;
	struct entity *entity_buf;
	struct component_pool component_pool[TYPE_COUNT];
};

static inline int
component_has(struct entity *e, int tid)
{
	return e->components[tid] < ENTITY_MAX_COMPONENTS;
}

static inline int
component_clr(struct entity *e, int tid)
{
	unsigned char idx = e->components[tid];
	if (idx < ENTITY_MAX_COMPONENTS) {
		e->index[idx] = -1;
		e->components[tid] = ENTITY_MAX_COMPONENTS;
		--e->cn;
	}
}

static inline int
component_add(lua_State *L, struct world *w, struct entity *e, int tid)
{
	int cid, i;
	struct component *c;
	struct component_pool *cp;
	struct component_ptr *ptr;
	if (e->components[tid] < ENTITY_MAX_COMPONENTS) {
		return luaL_error(L, "Entity(%d) already exist component(%d)",
			e - w->entity_buf, tid);
	}
	if (e->cn >= ENTITY_MAX_COMPONENTS) {
		return luaL_error(L, "Entity(%d) add to many components",
			e - w->entity_buf);
	}
	//new component
	cp = &w->component_pool[tid];
	if (cp->free >= cp->cap) {
		cp->cap *= 2;
		cp->buf = realloc(cp->buf, cp->cap * sizeof(cp->buf[0]));
	}
	c = &cp->buf[cp->free++];
	c->eid = e - w->entity_buf;
	c->dirty_next = LINK_NONE;
	c->dead_next = LINK_NONE;
	cid = c - cp->buf;
	//add component into entity
	for (i = 0; i < ENTITY_MAX_COMPONENTS; i++) {
		if (e->index[i] < 0)
			break;
	}
	assert(i < ENTITY_MAX_COMPONENTS);
	e->components[tid] = i;
	e->index[i] = cid;
	return cid;
}

static inline void
component_dead(struct world *w, int tid, int cid)
{
	struct component *c;
	struct component_pool *cp;
	cp = &w->component_pool[tid];
	c = &cp->buf[cid];
	if (c->dead_next != LINK_NONE) //already dead?
		return ;
	c->dead_next = LINK_NIL;
	if (cp->dead_tail == LINK_NIL) {
		cp->dead_tail = cid;
		cp->dead_head = cid;
	} else {
		cp->buf[cp->dead_tail].dead_next = cid;
		cp->dead_tail = cid;
	}
	return ;
}

static inline void
component_dirty(struct world *w, int tid, int cid)
{
	struct component *c;
	struct component_pool *cp;
	cp = &w->component_pool[tid];
	c = &cp->buf[cid];
	if (c->dirty_next != LINK_NONE) //already diryt
		return ;
	c->dirty_next = LINK_NIL;
	if (cp->dirty_tail == LINK_NIL) {
		cp->dirty_tail = cid;
		cp->dirty_head = cid;
	} else {
		cp->buf[cp->dirty_tail].dirty_next = cid;
		cp->dirty_tail = cid;
	}
	return ;
}

static inline struct entity *
entity_alloc(struct world *w)
{
	struct entity *e;
	int eid = w->entity_free;
	if (eid < 0) {
		int i = 0;
		int oldcap = w->entity_cap;
		int newcap = oldcap * 2;
		w->entity_cap = newcap;
		w->entity_buf = realloc(w->entity_buf,
			newcap * sizeof(w->entity_buf[0]));
		w->entity_free = oldcap + 1;
		e = &w->entity_buf[oldcap];
		for (i = w->entity_free; i < newcap - 1; i++) {
			w->entity_buf[i].cn = -1;
			w->entity_buf[i].next = i+1;
		}
		w->entity_buf[newcap - 1].cn = -1;
		w->entity_buf[newcap - 1].next = LINK_NIL;
	} else {
		e = &w->entity_buf[eid];
		w->entity_free = e->next;
	}
	e->cn = 0;
	memset(e->components, ENTITY_MAX_COMPONENTS, sizeof(e->components));
	memset(e->index, -1, sizeof(e->index));
	e->next = LINK_NONE;
	return e;
}

static inline void
entity_dead(struct world *w, struct entity *e)
{
	int t;
	if (e->cn < 0) {
		assert(e->next != LINK_NONE);
		return ;
	}
	assert(e->next == LINK_NONE);
	for (t = TYPE_MIN_ID; t <= w->type_idx; t++) {
		int i = e->components[t];
		if (i < ENTITY_MAX_COMPONENTS)
			component_dead(w, t, e->index[i]);
	}
	e->next = w->entity_dead;
	w->entity_dead = e - w->entity_buf;
}

static inline void
entity_free(struct world *w, struct entity *e)
{
	assert(e->cn == -1);
	e->next = w->entity_free;
	w->entity_free = e - w->entity_buf;
}

static inline int
get_typeid(lua_State *L, int stk, int proto_id)
{
	int id;
	stk = lua_absindex(L, stk);
	lua_pushvalue(L, stk);
	lua_gettable(L, proto_id);
	id = lua_tointeger(L,-1);
	lua_pop(L, 1);
	luaL_argcheck(L, id >= TYPE_MIN_ID, stk, "invalid type");
	return id;
}

static inline struct entity *
get_entity(lua_State *L, struct world *w, int stk)
{
	struct entity *e;
	int eid = luaL_checkinteger(L, stk);
	luaL_argcheck(L, eid < w->entity_cap, 2, "eid is invalid");
	e = &w->entity_buf[eid];
	luaL_argcheck(L, e->cn >= 0, 2, "entity is dead");
	return e;
}

static inline int
get_cid_in_entity(struct entity *e, int tid)
{
	int i = e->components[tid];
	if (i >= ENTITY_MAX_COMPONENTS)
		return -1;
	return e->index[i];
}

static inline void
update_cid_in_entity(struct entity *e, int tid, int cid)
{
	int i = e->components[tid];
	assert(i < ENTITY_MAX_COMPONENTS);
	e->index[i] = cid;
}


//world.register(W, "componentA", {foo=1, bar="hello"})
static int
lworld_register(lua_State *L)
{
	struct world *w;
	int type, tid;
	struct component_pool *cp;
	w = luaL_checkudata(L, ECS_WORLD, "ECS_WORLD");
	lua_getiuservalue(L, ECS_WORLD, WORLD_PROTO_ID);
	//check if 'componentA' has been declared
	lua_pushvalue(L, 2);
	type = lua_gettable(L, -2);
	luaL_argcheck(L, type == LUA_TNIL, 1, "duplicated component type");
	lua_pop(L, 1);
	//new component type
	tid = ++w->type_idx;
	luaL_argcheck(L, tid >= TYPE_MIN_ID &&
		tid <= TYPE_MAX_ID, 1, "component type is too may");
	lua_getiuservalue(L, ECS_WORLD, WORLD_COMPONENTS);
	lua_createtable(L, 128, 0);
	lua_seti(L, -2, tid);
	lua_pop(L, 1);
	cp = &w->component_pool[tid];
	cp->cap = 64;
	cp->free = 0;
	cp->dirty_head = LINK_NIL;
	cp->dirty_tail = LINK_NIL;
	cp->dead_head = LINK_NIL;
	cp->dead_tail = LINK_NIL;
	cp->buf = malloc(cp->cap * sizeof(cp->buf[0]));
	//set proto id
	lua_pushvalue(L, 2);
	lua_pushinteger(L, tid);
	lua_rawset(L, -3);
	lua_pop(L, 1);
	////////
	lua_getiuservalue(L, ECS_WORLD, WORLD_PROTO_DEFINE);
	lua_pushvalue(L, 2);
	lua_pushvalue(L, 3);
	lua_rawset(L, -3);
	lua_pop(L, 1);
	return 0;
}

//world.new(W, {vector={x=1,y=2}, componentA={foo=1, bar="hello"}})
static int
lworld_new_entity(lua_State *L)
{
	int i, components, proto_id;
	struct world *w = luaL_checkudata(L, ECS_WORLD, "ECS_WORLD");
	struct entity *e = entity_alloc(w);
	int eid = e - w->entity_buf;
	lua_getiuservalue(L, ECS_WORLD, WORLD_COMPONENTS);
	components = lua_gettop(L);
	lua_getiuservalue(L, ECS_WORLD, WORLD_PROTO_ID);
	proto_id = components + 1;
	lua_pushnil(L);  /* first key */
	while (lua_next(L, 2) != 0) {
		int cid;
		int tid = get_typeid(L, -2, proto_id);
		lua_getiuservalue(L, ECS_WORLD, WORLD_KEY_EID);
		lua_pushinteger(L, eid);
		lua_settable(L, -3);
		lua_getiuservalue(L, ECS_WORLD, WORLD_KEY_TID);
		lua_pushinteger(L, tid);
		lua_settable(L, -3);
		cid = component_add(L, w, e, tid);
		luaL_argcheck(L, cid >= 0, 2, "entity has duplicated component");
		lua_geti(L, components, tid);
		lua_pushvalue(L, -2);
		lua_seti(L, -2, cid);
		lua_pop(L, 2);
	}
	lua_pop(L, 1);
	lua_pushinteger(L, eid);
	return 1;
}

//world.del(W, eid)
static int
lworld_del_entity(lua_State *L)
{
	int i;
	struct world *w = luaL_checkudata(L, ECS_WORLD, "ECS_WORLD");
	struct entity *e = get_entity(L, w, 2);
	entity_dead(w, e);
	return 0;
}

//world.get(W, eid, "componentA", "componentB")
static int
lworld_get_component(lua_State *L)
{
	int i, top, proto_id, components;
	struct world *w = luaL_checkudata(L, ECS_WORLD, "ECS_WORLD");
	struct entity *e = get_entity(L, w, 2);
	top = lua_gettop(L);
	lua_getiuservalue(L, ECS_WORLD, WORLD_PROTO_ID);
	proto_id = top + 1;
	lua_getiuservalue(L, ECS_WORLD, WORLD_COMPONENTS);
	components = top + 2;
	for (i = 3; i <= top; i++) {
		int tid = get_typeid(L, i, proto_id);
		int cid = get_cid_in_entity(e, tid);
		if (cid >= 0) {
			lua_geti(L, components, tid);
			lua_geti(L, -1, cid);
			lua_replace(L, -2);
		} else {
			lua_pushnil(L);
		}
	}
	return (top - 3 + 1);
}

//world.add(W, eid, "component", value)
static int
lworld_add_component(lua_State *L)
{
	int tid, cid;
	struct world *w = luaL_checkudata(L, ECS_WORLD, "ECS_WORLD");
	struct entity *e = get_entity(L, w, 2);
	lua_getiuservalue(L, ECS_WORLD, WORLD_PROTO_ID);
	tid = get_typeid(L, 3, lua_gettop(L));
	cid = component_add(L, w, e, tid);
	lua_getiuservalue(L, ECS_WORLD, WORLD_COMPONENTS);
	lua_geti(L, -1, tid);
	lua_pushvalue(L, 4);
	lua_seti(L, -2, cid);
	lua_pop(L, 3);
	return 0;
}

//world.remove(W, eid, "component", ...)
static int
lworld_remove_component(lua_State *L)
{
	int i, proto_id;
	int tid, cid;
	struct world *w = luaL_checkudata(L, ECS_WORLD, "ECS_WORLD");
	struct entity *e = get_entity(L, w, 2);
	lua_getiuservalue(L, ECS_WORLD, WORLD_PROTO_ID);
	proto_id = lua_gettop(L);
	for (i = 3; i <= (proto_id - 1); i++) {
		tid = get_typeid(L, i, proto_id);
		cid = get_cid_in_entity(e, tid);
		component_dead(w, tid, cid);
	}
	lua_pop(L, 1);
	return 0;
}

//world.touch(W, component)
static int
lworld_touch_component(lua_State *L)
{
	int eid, tid, cid;
	struct entity *e;
	struct world *w = luaL_checkudata(L, ECS_WORLD, "ECS_WORLD");
	lua_getiuservalue(L, ECS_WORLD, WORLD_KEY_EID);
	lua_gettable(L, 2);
	eid = luaL_checkinteger(L, -1);
	luaL_argcheck(L, eid > 0 && eid < w->entity_cap, 2, "invalid component");
	lua_getiuservalue(L, ECS_WORLD, WORLD_KEY_TID);
	lua_gettable(L, 2);
	tid = luaL_checkinteger(L, -1);
	luaL_argcheck(L, tid >= TYPE_MIN_ID && tid <= TYPE_MAX_ID,
		2, "invalid component");
	e = &w->entity_buf[eid];
	cid = get_cid_in_entity(e, tid);
	component_dirty(w, tid, cid);
	return 0;
}

static void
push_result(lua_State *L, int *keys, int kn, struct entity *e)
{
	int i;
	if (e != NULL) { //match one
		int components;
		lua_getiuservalue(L, 1, 1);
		lua_getiuservalue(L, -1, WORLD_COMPONENTS);
		lua_replace(L, -2);
		components = lua_gettop(L);
		for (i = 0; i < kn; i++) {
			int tid = keys[i];
			int cid = get_cid_in_entity(e, tid);
			lua_geti(L, components, tid);
			lua_geti(L, -1, cid);
			lua_replace(L, -2);
		}
	} else {
		for (i = 0; i < kn; i++)
			lua_pushnil(L);
	}
}

static inline struct entity *
restrict_component(struct entity *ebuf, struct component *c, int *keys, int kn)
{
	int i;
	struct entity *e;
	e = &ebuf[c->eid];
	for (i = 1; i < kn; i++) {
		if (!component_has(e, keys[i]))
			return NULL;
	}
	return e;
}

//match_all(match_ctx *mctx, nil)
static int
match_all(lua_State *L)
{
	int i;
	int mi, free;
	int kn, *keys;
	struct entity *e = NULL;
	struct component_pool *cp;
	struct match_ctx *mctx = lua_touserdata(L, 1);
	struct world *w = mctx->w;
	struct entity *entity_buf = w->entity_buf;
	kn = mctx->kn;
	keys = mctx->keys;
	cp = &w->component_pool[mctx->keys[0]];
	mi = mctx->i;
	free = cp->free;
	while (mi < free) {
		struct component *c = &cp->buf[mi++];
		if (c->dead_next == LINK_NONE) {
			e = restrict_component(entity_buf, c, keys, kn);
			if (e != NULL)
				break;
		}
	}
	mctx->i = mi;
	push_result(L, keys, kn, e);
	return kn;
}

static int
match_dirty(lua_State *L)
{
	int next;
	int kn, *keys;
	struct entity *e = NULL;
	struct component_pool *cp;
	struct match_ctx *mctx = lua_touserdata(L, 1);
	struct world *w = mctx->w;
	struct entity *entity_buf = w->entity_buf;
	keys = mctx->keys;
	kn = mctx->kn;
	next = mctx->i;
	cp = &w->component_pool[keys[0]];
	while (next != LINK_NIL) {
		struct component *c = &cp->buf[next];
		next = c->dirty_next;
		if (c->dead_next == LINK_NONE) {
			e = restrict_component(entity_buf, c, keys, kn);
			if (e != NULL)
				break;
		}
	}
	mctx->i = next;
	push_result(L, keys, kn, e);
	return kn;
}

static int
match_dead(lua_State *L)
{
	int next;
	int kn, *keys;
	struct entity *e = NULL;
	struct component_pool *cp;
	struct match_ctx *mctx = lua_touserdata(L, 1);
	struct world *w = mctx->w;
	struct entity *entity_buf = w->entity_buf;
	keys = mctx->keys;
	kn = mctx->kn;
	next = mctx->i;
	cp = &w->component_pool[keys[0]];
	while (next != LINK_NIL) {
		struct component *c = &cp->buf[next];
		next = c->dead_next;
		e = restrict_component(entity_buf, c, keys, kn);
		if (e != NULL)
			break;
	}
	mctx->i = next;
	push_result(L, keys, kn, e);
	return kn;

}

//world.match(W, 'all/dirty/dead', 'componentA', 'componentB')
static int
lworld_match_component(lua_State *L)
{
	int i;
	size_t sz;
	struct world *w;
	enum match_mode mode;
	int (*iter)(lua_State *L) = NULL;
	struct match_ctx *mctx = NULL;
	const char *m = luaL_checklstring(L, 2, &sz);
	int top = lua_gettop(L);
	w = luaL_checkudata(L, ECS_WORLD, "ECS_WORLD");
	switch (sz) {
	case 3:
		if (m[0] == 'a' && m[1] == 'l' && m[2] == 'l') {
			iter = match_all;
			mode = MATCH_ALL;
		}
		break;
	case 5:
		if (memcmp(m, "dirty", 5) == 0) {
			iter = match_dirty;
			mode = MATCH_DIRTY;
		}
		break;
	case 4:
		if (memcmp(m, "dead", 4) == 0) {
			iter = match_dead;
			mode = MATCH_DEAD;
		}
		break;
	}
	if (iter == NULL)
		return luaL_argerror(L, 2, "mode can only be[all,dirty,dead]");
	luaL_argcheck(L, top >= 3, 3, "lost the component name");
	luaL_argcheck(L, top < array_size(mctx->keys), top, "too many component");
	lua_getiuservalue(L, ECS_WORLD, WORLD_PROTO_ID);
	lua_pushcfunction(L, iter);
	lua_getiuservalue(L, ECS_WORLD, WORLD_MATCH_CTX);
	mctx = lua_touserdata(L, -1);
	mctx->w = w;
	mctx->kn = 0;
	for (i = 3; i <= top; i++)
		mctx->keys[mctx->kn++] = get_typeid(L, i, top+1);
	switch (mode) {
	case MATCH_ALL:
		mctx->i = 0;
		break;
	case MATCH_DIRTY:
		mctx->i = w->component_pool[mctx->keys[0]].dirty_head;
		break;
	case MATCH_DEAD:
		mctx->i = w->component_pool[mctx->keys[0]].dead_head;
		break;
	}
	lua_pushnil(L);
	return 3;
}

//world.update(W)
static int
lworld_update(lua_State *L)
{
	int next;
	int t, components;
	struct component_pool *pool;
	struct world *w = luaL_checkudata(L, ECS_WORLD, "ECS_WORLD");
	struct entity *entity_buf = w->entity_buf;
	//clear dead entity
	next = w->entity_dead;
	while (next != LINK_NIL) {
		struct entity *e;
		e = &entity_buf[next];
		e->cn = -1;
		next = e->next;
		entity_free(w, e);
	}
	pool = w->component_pool;
	//clear dead component
	lua_getiuservalue(L, ECS_WORLD, WORLD_COMPONENTS);
	for (t = 0; t <= w->type_idx; t++) {
		int w = 0, r = 0, free;
		struct component *buf;
		struct component_pool *cp;
		cp = &pool[t];
		cp->dirty_head = LINK_NIL;
		cp->dirty_tail = LINK_NIL;
		cp->dead_head = LINK_NIL;
		cp->dead_tail = LINK_NIL;
		lua_geti(L, -1, t);
		buf = cp->buf;
		free = cp->free;
		for (r = 0; r < free; r++) {
			struct component *c;
			c = &buf[r];
			c->dirty_next = LINK_NONE;
			if (c->dead_next == LINK_NONE) {//alive
				if (w != r) {
					struct entity *e;
					e = &entity_buf[c->eid];
					buf[w] = *c;
					lua_geti(L, -1, r);
					lua_seti(L, -2, w);
					update_cid_in_entity(e, t, w);
				}
				w++;
			} else {//dead component
				struct entity *e;
				e = &entity_buf[c->eid];
				if (e->next == LINK_NONE)
					component_clr(e, t);
			}
		}
		cp->free = w;
		while (w < free) {
			lua_pushnil(L);
			lua_seti(L, -2, w);
			w++;
		}
		lua_pop(L, 1);
	}
	return 0;
}

static void
print_value(lua_State *L, int stk, int tab)
{
	switch (lua_type(L, stk)) {
	case LUA_TTABLE:
		printf("%*p=>", tab * 4, lua_topointer(L, stk));
		break;
	case LUA_TSTRING:
		printf("%*s=>", tab * 4, lua_tostring(L, stk));
		break;
	default:
		printf("%*d=>", tab * 4, lua_tointeger(L, stk));
		break;
	}
}

static int
dump_table(lua_State *L, int stk, int tab)
{
	lua_pushnil(L);  /* first key */
	while (lua_next(L, stk) != 0) {
		//print key
		print_value(L, -2, tab);
		switch (lua_type(L, -1)) {
		case LUA_TTABLE:
			printf("{\n");
			dump_table(L, lua_absindex(L, -1), tab+1);
			printf("}\n");
			break;
		case LUA_TSTRING:
			printf("%*s\n", tab * 4, lua_tostring(L, -1));
			break;
		default:
			printf("%*d\n", tab * 4, lua_tointeger(L, -1));
			break;
		}
		lua_pop(L, 1);
	}
}

static int
dump_upval(lua_State *L, int upval)
{
	const char *name[] = {
		"PROTO_ID",
		"PROTO_DEFINE",
		"COMPONENTS",
	};
	printf("==dump== %s\n", name[upval-1]);
	if (lua_getiuservalue(L, ECS_WORLD, upval) == LUA_TTABLE)
		dump_table(L, lua_gettop(L), 0);
	lua_pop(L, 1);
}

static int
lworld_dump(lua_State *L)
{
	int i;
	for (i = 1; i <= WORLD_COMPONENTS; i++)
		dump_upval(L, i);
	return 0;
}

static int
lcreate_world(lua_State *L)
{
	int i;
	struct world *w;
	struct match_ctx *mctx;
	w = (struct world *)lua_newuserdatauv(L, sizeof(*w), WORLD_UPVAL_N);
	memset(w, 0, sizeof(*w));
	w->entity_cap = 128;
	w->entity_free = 0;
	w->entity_dead = LINK_NIL;
	w->type_idx = 0;
	w->entity_buf = malloc(w->entity_cap * sizeof(w->entity_buf[0]));
	for (i = 0; i < w->entity_cap - 1; i++) {
		w->entity_buf[i].cn = -1;
		w->entity_buf[i].next = i+1;
	}
	w->entity_buf[w->entity_cap - 1].cn = -1;
	w->entity_buf[w->entity_cap - 1].next = LINK_NIL;
	if (luaL_getmetatable(L, "ECS_WORLD") == LUA_TNIL) {
		luaL_Reg world_mt[] = {
			{"__index", NULL},
			{"__name", NULL},
			{"register", lworld_register},
			{"new", lworld_new_entity},
			{"del", lworld_del_entity},
			{"get", lworld_get_component},
			{"add", lworld_add_component},
			{"remove", lworld_remove_component},
			{"touch", lworld_touch_component},
			{"match", lworld_match_component},
			{"update", lworld_update},
			{"dump", lworld_dump},
			{NULL, NULL},
		};
		lua_pop(L, 1);
		luaL_newlibtable(L, world_mt);
		luaL_setfuncs(L, world_mt, 0);
		lua_pushvalue(L, -1);
		lua_setfield(L, -2, "__index");
		lua_pushliteral(L, "ECS_WORLD");
		lua_setfield(L, -2, "__name");
		lua_pushvalue(L, -1);
		lua_setfield(L, LUA_REGISTRYINDEX, "ECS_WORLD");
	}
	lua_setmetatable(L, -2);

	lua_createtable(L, 0, 128);
	lua_setiuservalue(L, 1, WORLD_PROTO_ID);

	lua_createtable(L, 0, 128);
	lua_setiuservalue(L, 1, WORLD_PROTO_DEFINE);

	lua_createtable(L, TYPE_MAX_ID, 0);
	lua_setiuservalue(L, 1, WORLD_COMPONENTS);

	mctx = lua_newuserdatauv(L, sizeof(*mctx), 1);
	lua_pushvalue(L, 1);
	lua_setiuservalue(L, -2, 1);
	lua_setiuservalue(L, 1, WORLD_MATCH_CTX);

	lua_pushliteral(L, "__eid");
	lua_setiuservalue(L, 1, WORLD_KEY_EID);

	lua_pushliteral(L, "__tid");
	lua_setiuservalue(L, 1, WORLD_KEY_TID);

	return 1;
}

LUAMOD_API int
luaopen_ecs_c(lua_State *L)
{
	luaL_Reg tbl[] = {
		{"create_world", lcreate_world},
		{NULL, NULL},
	};
	luaL_checkversion(L);
	luaL_newlib(L, tbl);
	return 1;
}

