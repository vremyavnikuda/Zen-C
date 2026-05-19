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

static void sync_type_linkage(ParserContext *ctx, Type *t)
{
    if (!t)
    {
        return;
    }
    if ((t->kind == TYPE_STRUCT || t->kind == TYPE_ENUM) && !t->link_name && t->name)
    {
        ASTNode *def = find_struct_def(ctx, t->name);
        if (def && def->link_name)
        {
            t->link_name = xstrdup(def->link_name);
        }
    }
    if (t->inner)
    {
        sync_type_linkage(ctx, t->inner);
    }
    for (int i = 0; i < t->arg_count; i++)
    {
        sync_type_linkage(ctx, t->args[i]);
    }
}

static void sync_link_names_recursive(ParserContext *ctx, ASTNode *node)
{
    if (!node)
    {
        return;
    }

    if (node->type_info)
    {
        sync_type_linkage(ctx, node->type_info);
    }

    switch (node->type)
    {
    case NODE_FUNCTION:
        if (node->func.ret_type_info)
        {
            sync_type_linkage(ctx, node->func.ret_type_info);
        }
        if (node->func.arg_types)
        {
            for (int i = 0; i < node->func.arg_count; i++)
            {
                sync_type_linkage(ctx, node->func.arg_types[i]);
            }
        }
        sync_link_names_recursive(ctx, node->func.body);
        break;
    case NODE_STRUCT:
        sync_link_names_recursive(ctx, node->strct.fields);
        break;
    case NODE_VAR_DECL:
        sync_link_names_recursive(ctx, node->var_decl.init_expr);
        break;
    case NODE_BLOCK:
        sync_link_names_recursive(ctx, node->block.statements);
        break;
    case NODE_IF:
        sync_link_names_recursive(ctx, node->if_stmt.condition);
        sync_link_names_recursive(ctx, node->if_stmt.then_body);
        sync_link_names_recursive(ctx, node->if_stmt.else_body);
        break;
    case NODE_RETURN:
        sync_link_names_recursive(ctx, node->ret.value);
        break;
    case NODE_EXPR_CALL:
        sync_link_names_recursive(ctx, node->call.callee);
        sync_link_names_recursive(ctx, node->call.args);
        break;
    case NODE_EXPR_BINARY:
        sync_link_names_recursive(ctx, node->binary.left);
        sync_link_names_recursive(ctx, node->binary.right);
        break;
    case NODE_EXPR_UNARY:
        sync_link_names_recursive(ctx, node->unary.operand);
        break;
    case NODE_EXPR_MEMBER:
        sync_link_names_recursive(ctx, node->member.target);
        break;
    case NODE_EXPR_CAST:
        sync_link_names_recursive(ctx, node->cast.expr);
        break;
    case NODE_ROOT:
        sync_link_names_recursive(ctx, node->root.children);
        break;
    default:
        break;
    }

    sync_link_names_recursive(ctx, node->next);
}

void audit_section_5(ParserContext *ctx, Scope *scope, const char *name, const char *link_name,
                     Token tok)
{
    if (!scope || !name)
    {
        return;
    }
    if (strcmp(name, "it") == 0 || strcmp(name, "self") == 0)
    {
        return;
    }

    if (ctx->config->misra_mode)
    {
        if (ctx->hook_check_standard_macro_name)
        {
            ctx->hook_check_standard_macro_name(tok, name);
        }
    }

    Scope *p = scope;
    int limit = (p == ctx->global_scope) ? 31 : 63;

    while (p)
    {
        ZenSymbol *sh = p->symbols;
        while (sh)
        {
            if (p != scope && strcmp(sh->name, name) == 0 && !ctx->silent_warnings)
            {
                if (ctx->config->misra_mode)
                {
                    zerror_at(tok, "MISRA Rule 5.3");
                }
                else
                {
                    warn_shadowing(tok, name);
                }
            }

            if (ctx->config->misra_mode)
            {
                const char *actual_name = link_name ? link_name : name;
                const char *sh_actual_name = sh->link_name ? sh->link_name : sh->name;

                if (strcmp(sh_actual_name, actual_name) != 0)
                {
                    if (ctx->hook_check_identifier_collision)
                    {
                        ctx->hook_check_identifier_collision(tok, sh_actual_name, actual_name,
                                                             limit);
                    }
                }
            }

            sh = sh->next;
        }
        p = p->parent;
    }
}

void sync_all_link_names(ParserContext *ctx, ASTNode *root)
{
    sync_link_names_recursive(ctx, root);
}
