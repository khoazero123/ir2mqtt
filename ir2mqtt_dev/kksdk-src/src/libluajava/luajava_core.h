#ifndef LUAJAVA_CORE_H
#define LUAJAVA_CORE_H

#include <stddef.h>
#include <stdint.h>

struct lua_State;

#define KK_LUA_MAX_TABLE_LEN 10240

typedef struct lua_State kk_lua_state_t;

#ifdef __cplusplus
extern "C" {
#endif

kk_lua_state_t *kk_lua_create_state(void);
void kk_lua_release_state(kk_lua_state_t *L);

int kk_lua_set_global_number(kk_lua_state_t *L, const char *name, double value);
int kk_lua_set_global_byte_array(kk_lua_state_t *L, const char *name, const uint8_t *data,
                                 size_t len);
int kk_lua_set_global_number_map(kk_lua_state_t *L, const char *name, const double *keys,
                                 const double *values, size_t count);

/* Returns static "OK" or an error buffer owned by the caller. */
char *kk_lua_run_script(kk_lua_state_t *L, const char *script);

int kk_lua_get_global_byte_array(kk_lua_state_t *L, const char *name, uint8_t **out,
                                 size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
