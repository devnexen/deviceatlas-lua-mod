#include "dac.h"

#include <stdio.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

typedef struct {
   da_atlas_t atlas;
   da_config_t cfg;
   void *atlasptr;
   size_t atlasimgsz;
   time_t jsoncreation;
#if DATLAS_MAJOR_VERSION == 2
   // FIXME: could be dropped in some months
   int jsonrevision;
#endif
   char *jsonversion;
   const char *jsonpath;
} dalua_t;

#define GETDALUA                                    \
    dalua_t *dl;                                    \
                                                    \
    dl = (dalua_t *)luaL_checkudata(L, 1, "DAlua"); \
                                                    \
    if (dl->atlasptr == 0)                          \
        return (0);


static size_t
jsonreadp(void *ctx, size_t len, char *buf)
{
    return fread(buf, 1, len, ctx);
}

static da_status_t
jsonseekp(void *ctx, off_t off)
{
    return fseek(ctx, SEEK_SET, off) != -1 ? DA_OK : DA_SYS;
}

static void
free_atlas(dalua_t *dl)
{
    da_atlas_close(&dl->atlas);
    free(dl->atlasptr);
    dl->atlasptr = 0;
    dl->jsonpath = 0;
    dl->atlasimgsz = 0;
    dl->jsonversion = 0;
#if DATLAS_MAJOR_VERSION == 2
    dl->jsonrevision = 0;
#endif
    dl->jsoncreation = (time_t)0;
}

static int
dalua_new(lua_State *L)
{
    dalua_t *dl;
    dl = (dalua_t *)lua_newuserdata(L, sizeof(*dl));
    if (dl == 0)
        return (0);

    dl->atlasptr = 0;
    dl->jsonpath = 0;
    dl->atlasimgsz = 0;
    dl->jsonversion = 0;
#if DATLAS_MAJOR_VERSION == 2
    dl->jsonrevision = 0;
    dl->cfg.ua_props = 1u;
    dl->cfg.lang_props = 1u;
#else
    dl->cfg.cache_size = 0u;
#endif
    dl->jsoncreation = (time_t)0;

    luaL_getmetatable(L, "DAlua");
    lua_setmetatable(L, -2);

    return (1);
}

static int
dalua_set_config(lua_State *L)
{
    dalua_t *dl;
    dl = (dalua_t *)luaL_checkudata(L, 1, "DAlua");
    if (dl == 0) {
        luaL_error(L, "needs to be instantiated");
        lua_pushboolean(L, 0);
        return (0);
    }

    if (lua_istable(L, 2) == 1) {
        for (lua_pushnil(L); lua_next(L, 2) != 0; lua_pop(L, 1)) {
            if (!lua_isstring(L, -2))
                continue;
	    if (!lua_isboolean(L, -1) || !lua_isinteger(L, -1))
                continue;
	    const char *key = luaL_checkstring(L, -2);
	    unsigned int value = (luaL_checkinteger(L, -1) != 0);
#if DATLAS_MAJOR_VERSION == 2
	    if (strcasecmp(key, "uaprops") == 0)
                dl->cfg.ua_props = value;
	    else if (strcasecmp(key, "lgprops") == 0)
                dl->cfg.lang_props = value;
#else
	    if (strcasecmp(key, "cache_size") == 0)
		dl->cfg.cache_size = value;
#endif
        }
        lua_pushboolean(L, 1);
	return (1);
    }

    luaL_error(L, "only a table is accepted");
    lua_pushboolean(L, 0);
    return (0);
}

static int
dalua_load_data_from_file(lua_State *L)
{
    dalua_t *dl;
    FILE *jsonp;
    const char *jsonpath;
    da_status_t status;

    dl = (dalua_t *)luaL_checkudata(L, 1, "DAlua");
    if (dl->atlasptr != 0)
        free_atlas(dl);

    jsonpath = luaL_checkstring(L, 2);
    jsonp = fopen(jsonpath, "r");
    if (jsonp == 0) {
        lua_pushboolean(L, 0);
        return (1);
    }

    status = da_atlas_compile(jsonp, jsonreadp, jsonseekp, &dl->atlasptr, &dl->atlasimgsz);
    fclose(jsonp);

    if (status == DA_OK) {
        da_property_decl_t extra[1] = {{ 0, 0 }};
        status = da_atlas_open(&dl->atlas, extra, dl->atlasptr, dl->atlasimgsz);
        if (status == DA_OK) {
#if DATLAS_MAJOR_VERSION == 2
            da_atlas_setconfig(&dl->atlas, &dl->cfg);
            dl->jsonrevision = da_getdatarevision(&dl->atlas);
#endif
            dl->jsonpath = jsonpath;
            dl->jsoncreation = da_getdatacreation(&dl->atlas);
            dl->jsonversion = da_getdataversion(&dl->atlas);
            lua_pushboolean(L, 1);
            return (1);
        }
    }

    luaL_error(L, "error loading %s file", jsonpath);
    lua_pushboolean(L, 0);
    return (1);
}

static int
dalua_get_properties(lua_State *L)
{
#define  MAX_PROPS   26
    dalua_t *dl;
    da_evidence_t ev[MAX_PROPS];
    da_deviceinfo_t device;
    da_propid_t *prop;
    da_type_t proptype;
    da_status_t status;
    size_t evsz = 0, i;
    const char *propname;

    dl = (dalua_t *)luaL_checkudata(L, 1, "DAlua");
    if (dl->atlasptr == 0) {
        luaL_error(L, "data file needs to be loaded");
        return (0);
    }

    if (lua_isstring(L, 2) == 1) {
        const char *value = luaL_checkstring(L, 2);
        char *p = strdup(value);
        ev[evsz].key = da_atlas_header_evidence_id(&dl->atlas, "user-agent");
        ev[evsz ++].value = p;
        if (!lua_isnil(L, 3) && lua_isstring(L, 3) == 1) {
            value = luaL_checkstring(L, 3);
            ev[evsz].key = da_atlas_clientprop_evidence_id(&dl->atlas);
            ev[evsz ++].value = strdup(value);
            if (!lua_isnil(L, 4) && lua_isstring(L, 4) == 1) {
                value = luaL_checkstring(L, 4);
                ev[evsz].key = da_atlas_accept_language_evidence_id(&dl->atlas);
                ev[evsz ++].value = strdup(value);
            }
        }
    } else if (lua_istable(L, 2) == 1) {
        for (lua_pushnil(L); lua_next(L, 2) != 0 && evsz < MAX_PROPS; lua_pop(L, 1)) {
            if (!lua_isstring(L, -2) || !lua_isstring(L, -1))
                continue;
            const char *key = luaL_checkstring(L, -2);
            const char *value = luaL_checkstring(L, -1);
            da_evidence_id_t evid;
            if (strcasecmp(key, "accept-language") == 0)
                evid = da_atlas_accept_language_evidence_id(&dl->atlas);
            else if (strcasecmp(key, "clientside") == 0)
                evid = da_atlas_clientprop_evidence_id(&dl->atlas);
            else
                evid = da_atlas_header_evidence_id(&dl->atlas, key);

            if (evid == -1)
                continue;
            char *p = strdup(value);
            ev[evsz].key = evid;
            ev[evsz ++].value = p;
        }
    } else {
        luaL_error(L, "only a string or a table are accepted");
        return (0);
    }

    lua_newtable(L);
    status = da_searchv(&dl->atlas, &device, ev, evsz);
    if (status == DA_OK) {
        for (status = da_getfirstprop(&device, &prop); status == DA_OK;
                status = da_getnextprop(&device, &prop)) {
            da_getpropname(&device, *prop, &propname);
            da_getproptype(&device, *prop, &proptype);

            switch (proptype) {
            case DA_TYPE_INTEGER:
            case DA_TYPE_NUMBER: {
               long value;
               da_getpropinteger(&device, *prop, &value);
               lua_pushinteger(L, (int)value);
               break;
            }
            case DA_TYPE_BOOLEAN: {
               bool value;
               da_getpropboolean(&device, *prop, &value);
               lua_pushboolean(L, value == true);
               break;
            }
            case DA_TYPE_STRING: {
               const char *value;
               da_getpropstring(&device, *prop, &value);
               lua_pushstring(L, value);
               break;
            }
            default:
               lua_pushnil(L);
               break;
            }

            char *p = strdup(propname);
            char *e = p;
            while (*e++)
                if (*e == '.')
                    *e = '_';
            lua_setfield(L, -2, p);
            free(p);
        }
    } else {
        luaL_error(L, "error getting properties");
        return (0);
    }

    da_close(&device);

    for (i = 0; i < evsz; i ++)
        free(ev[i].value);

    return (1);
}

#if DATLAS_MAJOR_VERSION == 2
static int
dalua_get_jsonrevision(lua_State *L)
{
    GETDALUA

    lua_pushinteger(L, dl->jsonrevision);
    return (1);
}
#endif

static int
dalua_get_jsoncreation(lua_State *L)
{
    GETDALUA

    lua_pushinteger(L, dl->jsoncreation);
    return (1);
}

static int
dalua_get_jsonversion(lua_State *L)
{
    GETDALUA

    lua_pushstring(L, dl->jsonversion);
    return (1);
}

static int
dalua_free(lua_State *L)
{
    dalua_t *dl;

    dl = (dalua_t *)luaL_checkudata(L, 1, "DAlua");

    if (dl->atlasptr != 0)
        free_atlas(dl);

    return (0);
}

static int
dalua_tostring(lua_State *L)
{
    dalua_t *dl;
    luaL_Buffer buf;
    char sbuf[64];

    dl = (dalua_t *)luaL_checkudata(L, 1, "DAlua");
    luaL_buffinit(L, &buf);
    luaL_addstring(&buf, "\n");
    luaL_addstring(&buf, "DeviceAtlas instance (");
    snprintf(sbuf, sizeof(sbuf), "%p", dl->atlasptr);
    luaL_addstring(&buf, sbuf);
    memset(sbuf, 0, sizeof(sbuf));
    luaL_addstring(&buf, ")\n");
    luaL_addstring(&buf, "JSON's loaded in memory: ");
    luaL_addstring(&buf, "\n\n");
    luaL_addstring(&buf, "[\n");
    luaL_addstring(&buf, "\tpath: ");
    luaL_addstring(&buf, dl->jsonpath != 0 ? dl->jsonpath : "/");
    luaL_addstring(&buf, "\n");
#if DATLAS_MAJOR_VERSION == 2
    luaL_addstring(&buf, "\trevision: ");
    snprintf(sbuf, sizeof(sbuf), "%d", dl->jsonrevision);
    luaL_addstring(&buf, sbuf);
    luaL_addstring(&buf, "\n");
#endif
    memset(sbuf, 0, sizeof(sbuf));
    luaL_addstring(&buf, "\tcreation timestamp: ");
    snprintf(sbuf, sizeof(sbuf), "%ld", dl->jsoncreation);
    luaL_addstring(&buf, sbuf);
    luaL_addstring(&buf, "\n");
    luaL_addstring(&buf, "\tversion: ");
    luaL_addstring(&buf, dl->jsonversion != 0 ? dl->jsonversion : "/");
    luaL_addstring(&buf, "\n");
#if DATLAS_MAJOR_VERSION == 2
    luaL_addstring(&buf, "\tuar properties: ");
    luaL_addstring(&buf, dl->cfg.ua_props != 0 ? "true" : "false");
    luaL_addstring(&buf, "\n");
    luaL_addstring(&buf, "\tlang properties: ");
    luaL_addstring(&buf, dl->cfg.lang_props != 0 ? "true" : "false");
#else
    memset(sbuf, 0, sizeof(sbuf));
    luaL_addstring(&buf, "\tcache size: ");
    snprintf(sbuf, sizeof(sbuf), "%u", dl->cfg.cache_size);
    luaL_addstring(&buf, sbuf);
#endif
    luaL_addstring(&buf, "\n]\n");
    luaL_pushresult(&buf);

    return (1);
}

static const struct luaL_Reg dalua_methods[] = {
    { "load_data_from_file", dalua_load_data_from_file },
    { "get_properties", dalua_get_properties },
#if DATLAS_MAJOR_VERSION == 2
    { "get_jsonrevision", dalua_get_jsonrevision },
#endif
    { "get_jsoncreation", dalua_get_jsoncreation },
    { "get_jsonversion", dalua_get_jsonversion },
    { "set_config", dalua_set_config },
    { "__tostring", dalua_tostring },
    { "__gc", dalua_free },
    { NULL, NULL },
};

static const struct luaL_Reg dalua_functions[] = {
    { "new", dalua_new },
    { NULL, NULL },
};

int
luaopen_dalua(lua_State *L)
{
    luaL_newmetatable(L, "DAlua");
    lua_pushstring(L, "__index");
    lua_pushvalue(L, -2);
    lua_settable(L, -3);
    luaL_setfuncs(L, dalua_methods, 0);
    luaL_newlib(L, dalua_functions);

    return (1);
}
