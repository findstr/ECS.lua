.PNONY:all mingw

all:ecs.so

mingw:ecs.dll

ecs.so:lualib-ecs.c
	gcc -O2 -g3 -Ilua/ --share -fPIC -o $@ $<

ecs.dll:lualib-ecs.c
	gcc -O2 -g3 -Ilua/ --share -fPIC -DLUA_BUILD_AS_DLL -o $@ $< ./lua/liblua.a

