/* Port libluajava.so — Lua 5.4.6 embed + logic ggabv / rls (Kookong). */
#include "luajava_core.h"

#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#ifdef __ANDROID__
#include <android/log.h>
#define KK_LOG_TAG "KOOKONG"
#define KK_LOGW(...) __android_log_print(ANDROID_LOG_WARN, KK_LOG_TAG, __VA_ARGS__)
#else
#define KK_LOGW(...) ((void)0)
#endif

static char *dup_err(const char *msg) {
    const char *s = msg ? msg : "error";
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) {
        memcpy(p, s, n);
    }
    return p;
}

kk_lua_state_t *kk_lua_create_state(void) {
    kk_lua_state_t *L = luaL_newstate();
    if (!L) {
        KK_LOGW("cls:new lua state error");
        return NULL;
    }
    luaL_openlibs(L);
    return L;
}

void kk_lua_release_state(kk_lua_state_t *L) {
    if (L) {
        lua_close(L);
    }
}

int kk_lua_set_global_number(kk_lua_state_t *L, const char *name, double value) {
    if (!L || !name) {
        return -1;
    }
    lua_pushnumber(L, value);
    lua_setglobal(L, name);
    return 0;
}

static int byte_table_missing_index(lua_State *L) {
    if (lua_isnumber(L, 2)) {
        lua_pushinteger(L, 0);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static void attach_byte_table_metatable(lua_State *L) {
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, byte_table_missing_index);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
}

int kk_lua_set_global_byte_array(kk_lua_state_t *L, const char *name, const uint8_t *data,
                                 size_t len) {
    if (!L || !name) {
        KK_LOGW("ggabv:addr is 0");
        return -1;
    }
    if (!data && len > 0) {
        KK_LOGW("ggabv:get name buf error");
        return -2;
    }
    if (len > KK_LUA_MAX_TABLE_LEN) {
        KK_LOGW("ggabv:tableLen(%zu) is too big", len);
        return -3;
    }

    lua_createtable(L, (int)len, 0);
    for (size_t i = 0; i < len; i++) {
        lua_pushinteger(L, (lua_Integer)data[i]);
        lua_rawseti(L, -2, (lua_Integer)(i + 1));
    }
    attach_byte_table_metatable(L);
    lua_setglobal(L, name);
    return 0;
}

int kk_lua_set_global_number_map(kk_lua_state_t *L, const char *name, const double *keys,
                                 const double *values, size_t count) {
    if (!L || !name || !keys || !values) {
        return -1;
    }
    if (count > KK_LUA_MAX_TABLE_LEN) {
        return -3;
    }

    lua_createtable(L, 0, (int)count);
    for (size_t i = 0; i < count; i++) {
        lua_pushnumber(L, keys[i]);
        lua_pushnumber(L, values[i]);
        lua_settable(L, -3);
    }
    lua_setglobal(L, name);
    return 0;
}

char *kk_lua_run_script(kk_lua_state_t *L, const char *script) {
    static const char ok_msg[] = "OK";

    if (!L) {
        KK_LOGW("rls:addr is 0");
        return NULL;
    }
    if (!script) {
        KK_LOGW("rls:get script buf error");
        return NULL;
    }

    if (luaL_loadstring(L, script) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        return dup_err(err);
    }

    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        return dup_err(err);
    }

    return (char *)ok_msg;
}

static int lua_stack_index_to_integer(lua_State *L, int idx, lua_Integer *out) {
    if (lua_isinteger(L, idx)) {
        *out = lua_tointeger(L, idx);
        return 0;
    }
    if (lua_isnumber(L, idx)) {
        *out = (lua_Integer)lua_tonumber(L, idx);
        return 0;
    }
    return -1;
}

int kk_lua_get_global_byte_array(kk_lua_state_t *L, const char *name, uint8_t **out,
                                 size_t *out_len) {
    if (!L || !name || !out || !out_len) {
        return -1;
    }

    lua_getglobal(L, name);
    if (!lua_istable(L, -1)) {
        lua_settop(L, 0);
        return -2;
    }

    /* Port libluajava ggabv get path (JEB): rawlen cap + lua_next + maxPos scatter. */
    const size_t table_len = (size_t)lua_rawlen(L, -1);
    if (table_len > KK_LUA_MAX_TABLE_LEN) {
        KK_LOGW("ggabv:tableLen(%zu) is too big", table_len);
        lua_settop(L, 0);
        return -3;
    }

    int *pos_buf = NULL;
    uint8_t *byte_buf = NULL;
    if (table_len > 0) {
        pos_buf = (int *)calloc(table_len, sizeof(int));
        if (!pos_buf) {
            KK_LOGW("ggabv:calloc posBuf error");
            lua_settop(L, 0);
            return -4;
        }
        byte_buf = (uint8_t *)calloc(table_len, 1);
        if (!byte_buf) {
            KK_LOGW("ggabv:calloc byteBuf error");
            free(pos_buf);
            lua_settop(L, 0);
            return -4;
        }
    }

    int max_pos = 0;
    size_t pair_count = 0;
    size_t pair_cap = table_len;

    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        lua_Integer pos = 0;
        if (lua_stack_index_to_integer(L, -2, &pos) != 0 || pos <= 0) {
            KK_LOGW("ggabv:pos[%zu]=%d is illegal, tableLen=%zu", pair_count, (int)pos,
                    table_len);
            free(pos_buf);
            free(byte_buf);
            lua_settop(L, 0);
            return -2;
        }
        lua_Integer val = 0;
        if (lua_stack_index_to_integer(L, -1, &val) != 0) {
            val = 0;
        }

        if (pair_count >= pair_cap) {
            const size_t new_cap = pair_cap == 0 ? 8 : pair_cap * 2;
            int *np = (int *)realloc(pos_buf, new_cap * sizeof(int));
            uint8_t *nb = (uint8_t *)realloc(byte_buf, new_cap);
            if (!np || !nb) {
                free(np ? np : pos_buf);
                free(nb ? nb : byte_buf);
                KK_LOGW("ggabv:calloc posBuf error");
                lua_settop(L, 0);
                return -4;
            }
            pos_buf = np;
            byte_buf = nb;
            pair_cap = new_cap;
        }

        pos_buf[pair_count] = (int)pos;
        byte_buf[pair_count] = (uint8_t)val;
        if ((int)pos > max_pos) {
            max_pos = (int)pos;
        }
        pair_count++;
        lua_pop(L, 1);
    }

    if (max_pos > KK_LUA_MAX_TABLE_LEN) {
        KK_LOGW("ggabv:maxPos(%d) is too big", max_pos);
        free(pos_buf);
        free(byte_buf);
        lua_settop(L, 0);
        return -3;
    }

    if (max_pos == 0) {
        *out = NULL;
        *out_len = 0;
        free(pos_buf);
        free(byte_buf);
        lua_settop(L, 0);
        return 0;
    }

    uint8_t *ret = (uint8_t *)calloc((size_t)max_pos, 1);
    if (!ret) {
        KK_LOGW("ggabv:calloc retBytes error");
        free(pos_buf);
        free(byte_buf);
        lua_settop(L, 0);
        return -4;
    }

    for (size_t i = 0; i < pair_count; i++) {
        const int p = pos_buf[i];
        if (p > 0 && p <= max_pos) {
            ret[p - 1] = byte_buf[i];
        }
    }

    free(pos_buf);
    free(byte_buf);
    lua_settop(L, 0);
    *out = ret;
    *out_len = (size_t)max_pos;
    return 0;
}
