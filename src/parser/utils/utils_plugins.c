// SPDX-License-Identifier: MIT
#include "plugins/plugin_manager.h"
#include "parser.h"
#include "utils/format_expr.h"
#include "utils/colors.h"
#include "utils/utils.h"
#include "constants.h"
#include "ast/primitives.h"
#include <ctype.h>
#include "analysis/const_fold.h"

void register_plugin(ParserContext *ctx, const char *name, const char *alias)
{
    ZPlugin *plugin = ctx->hook_find_plugin ? (ZPlugin *)ctx->hook_find_plugin(name) : NULL;

    if (!plugin)
    {
#ifdef ZC_STATIC_PLUGINS
        plugin = ctx->hook_find_plugin ? (ZPlugin *)ctx->hook_find_plugin(name) : NULL;
        if (!plugin && strchr(name, '/'))
        {
            const char *last_slash = (char *)strrchr(name, '/');
            plugin =
                ctx->hook_find_plugin ? (ZPlugin *)ctx->hook_find_plugin(last_slash + 1) : NULL;
        }
#endif
        if (!plugin)
        {
            char path[MAX_PATH_LEN];
            snprintf(path, sizeof(path), "%s%s", name, z_get_plugin_ext());
            plugin = zptr_load_plugin(path);
        }

        if (!plugin && !strchr(name, '/'))
        {
            char path[MAX_PATH_LEN];
            snprintf(path, sizeof(path), "%s%s%s", z_get_run_prefix(), name, z_get_plugin_ext());
            plugin = zptr_load_plugin(path);
        }

        if (!plugin)
        {
            char path[MAX_PATH_LEN];
            snprintf(path, sizeof(path), ZEN_SHARE_DIR "/plugins/%s%s", name, z_get_plugin_ext());
            plugin = zptr_load_plugin(path);

            if (!plugin && strchr(name, '/'))
            {
                const char *last_slash = (char *)strrchr(name, '/');
                snprintf(path, sizeof(path), ZEN_SHARE_DIR "/plugins/%s%s", last_slash + 1,
                         z_get_plugin_ext());
                plugin = zptr_load_plugin(path);
            }
        }
    }

    if (!plugin)
    {
        zerror_at(TOKEN_UNKNOWN, "Could not load plugin '%s' (tried built-ins and dynamic loading)",
                  name);
        if (ctx->config->mode_lsp)
        {
            ImportedPlugin *p = xmalloc(sizeof(ImportedPlugin));
            p->name = xstrdup(name);
            p->alias = alias ? xstrdup(alias) : NULL;
            zmap_put(&ctx->imports.imported_plugins, alias ? alias : name, p);
            return;
        }
        if (ctx->is_fault_tolerant)
        {
            // Fuzzing or LSP — recover gracefully
            return;
        }
        exit(1); // whitelisted: guarded by is_fault_tolerant above
    }

    zptr_register_plugin(plugin);

    ImportedPlugin *p = xmalloc(sizeof(ImportedPlugin));
    p->name = xstrdup(plugin->name);
    p->alias = alias ? xstrdup(alias) : NULL;
    zmap_put(&ctx->imports.imported_plugins, alias ? alias : p->name, p);
}

const char *resolve_plugin(ParserContext *ctx, const char *name_or_alias)
{
    ImportedPlugin **p_ptr = zmap_get(&ctx->imports.imported_plugins, name_or_alias);
    if (p_ptr)
    {
        return (*p_ptr)->name;
    }

    zmap_iter_PluginMap it = zmap_iter_init(PluginMap, &ctx->imports.imported_plugins);
    const char *key;
    ImportedPlugin *p;
    while (zmap_iter_next(&it, &key, &p))
    {
        if (strcmp(p->name, name_or_alias) == 0)
        {
            return p->name;
        }
    }
    return NULL;
}
