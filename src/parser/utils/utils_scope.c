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

void enter_scope(ParserContext *ctx)
{
    Scope *s = symbol_scope_create(ctx->current_scope, NULL);
    ctx->current_scope = s;
}

void exit_scope(ParserContext *ctx)
{
    if (!ctx->current_scope || ctx->current_scope == ctx->global_scope)
    {
        return;
    }

    ZenSymbol *sym = ctx->current_scope->symbols;
    while (sym)
    {
        if (!sym->is_used && strcmp(sym->name, "self") != 0 && sym->name[0] != '_')
        {
        }
        sym = sym->next;
    }

    ctx->current_scope = ctx->current_scope->parent;
}

void register_symbol_to_lsp(ParserContext *ctx, ZenSymbol *s)
{
    if (!ctx || !s)
    {
        return;
    }

    ZenSymbol *curr = ctx->all_symbols;
    while (curr)
    {
        if (curr->kind == s->kind && curr->decl_token.line == s->decl_token.line &&
            curr->decl_token.col == s->decl_token.col && curr->name && s->name &&
            strcmp(curr->name, s->name) == 0)
        {
            return;
        }
        curr = curr->next;
    }

    ZenSymbol *lsp_copy = xmalloc(sizeof(ZenSymbol));
    memcpy(lsp_copy, s, sizeof(ZenSymbol));
    lsp_copy->original = s;
    lsp_copy->next = ctx->all_symbols;
    ctx->all_symbols = lsp_copy;
    if (s->name)
    {
        lsp_copy->name = xstrdup(s->name);
    }
    if (s->cfg_condition)
    {
        lsp_copy->cfg_condition = xstrdup(s->cfg_condition);
    }

    lsp_copy->is_local = s->is_local;
}
