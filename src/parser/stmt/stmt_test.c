// SPDX-License-Identifier: MIT

#include "parser.h"
#include "constants.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ast/ast.h"
#include "utils/format_expr.h"
#include "plugins/plugin_manager.h"
#include "zen/zen_facts.h"
#include "zprep_plugin.h"
#include "analysis/move_check.h"

char *curr_func_ret = NULL;

static void auto_import_std_test(ParserContext *ctx)
{
    if (find_func(ctx, "expect_eq"))
    {
        return;
    }

    char *resolved = z_resolve_path("std/test.zc", ctx->current_filename, ctx->config);
    if (!resolved)
    {
        return;
    }

    if (is_file_imported(ctx, resolved))
    {
        zfree(resolved);
        return;
    }
    if (zmap_get(&ctx->imports.currently_parsing, resolved))
    {
        zfree(resolved);
        return;
    }
    zmap_put(&ctx->imports.currently_parsing, resolved, resolved);

    char *src = load_file(resolved, ctx->current_filename);

    Lexer i;
    lexer_init(&i, src, ctx->config, ctx->current_filename);
    const char *saved_fn = ctx->current_filename;
    ctx->current_filename = resolved;

    parse_program_nodes(ctx, &i);

    ctx->current_filename = saved_fn;
    zmap_remove(&ctx->imports.currently_parsing, resolved);
    mark_file_imported(ctx, resolved);
    zfree(resolved);
}

ASTNode *parse_test(ParserContext *ctx, Lexer *l)
{
    auto_import_std_test(ctx);

    lexer_next(l);
    Token t = lexer_next(l);
    if (t.type != TOK_STRING && t.type != TOK_RAW_STRING)
    {
        zpanic_at(t, "Test name must be a string literal");
    }

    char *name = token_get_string_content(t);

    ASTNode *body = parse_block(ctx, l);

    ASTNode *n = ast_create(NODE_TEST);
    n->test_stmt.name = name;
    n->test_stmt.body = body;
    return n;
}

ASTNode *parse_assert(ParserContext *ctx, Lexer *l)
{
    Token tk = lexer_next(l);
    if (lexer_peek(l).type == TOK_LPAREN)
    {
        lexer_next(l);
    }

    ASTNode *cond = parse_expression(ctx, l);

    char *msg = NULL;
    int is_literal = 0;
    if (lexer_peek(l).type == TOK_COMMA)
    {
        lexer_next(l);
        Token st = lexer_next(l);
        if (st.type == TOK_STRING)
        {
            is_literal = 1;
            msg = xmalloc(st.len + 1);
            strncpy(msg, st.start, st.len);
            msg[st.len] = 0;
        }
        else if (st.type == TOK_IDENT)
        {
            is_literal = 0;
            msg = xmalloc(st.len + 1);
            strncpy(msg, st.start, st.len);
            msg[st.len] = 0;
        }
        else
        {
            zpanic_at(st, "Expected message string");
        }
    }

    if (lexer_peek(l).type == TOK_RPAREN)
    {
        lexer_next(l);
    }
    if (lexer_peek(l).type == TOK_SEMICOLON)
    {
        lexer_next(l);
    }

    ASTNode *n = ast_create(NODE_ASSERT);
    n->token = tk;
    n->assert_stmt.condition = cond;
    n->assert_stmt.message = msg;
    n->assert_stmt.message_is_literal = is_literal;
    return n;
}

ASTNode *parse_expect(ParserContext *ctx, Lexer *l)
{
    Token tk = lexer_next(l);
    if (lexer_peek(l).type == TOK_LPAREN)
    {
        lexer_next(l);
    }

    ASTNode *cond = parse_expression(ctx, l);

    char *msg = NULL;
    int is_literal = 0;
    if (lexer_peek(l).type == TOK_COMMA)
    {
        lexer_next(l);
        Token st = lexer_next(l);
        if (st.type == TOK_STRING)
        {
            is_literal = 1;
            msg = xmalloc(st.len + 1);
            strncpy(msg, st.start, st.len);
            msg[st.len] = 0;
        }
        else if (st.type == TOK_IDENT)
        {
            is_literal = 0;
            msg = xmalloc(st.len + 1);
            strncpy(msg, st.start, st.len);
            msg[st.len] = 0;
        }
        else
        {
            zpanic_at(st, "Expected message string");
        }
    }

    if (lexer_peek(l).type == TOK_RPAREN)
    {
        lexer_next(l);
    }
    if (lexer_peek(l).type == TOK_SEMICOLON)
    {
        lexer_next(l);
    }

    ASTNode *n = ast_create(NODE_EXPECT);
    n->token = tk;
    n->assert_stmt.condition = cond;
    n->assert_stmt.message = msg;
    n->assert_stmt.message_is_literal = is_literal;
    return n;
}

ASTNode *parse_return(ParserContext *ctx, Lexer *l)
{
    Token return_token = lexer_next(l);

    if (ctx->cg.in_defer_block)
    {
        zpanic_at(return_token, "'return' is not allowed inside a 'defer' block");
    }

    ASTNode *n = ast_create(NODE_RETURN);
    n->token = return_token;

    int handled = 0;

    if (curr_func_ret && strncmp(curr_func_ret, "Tuple__", 7) == 0 &&
        lexer_peek(l).type == TOK_LPAREN)
    {

        int is_tuple_lit = 0;
        int depth = 0;

        Lexer temp_l = *l;

        while (1)
        {
            Token t = lexer_next(&temp_l);
            if (t.type == TOK_EOF)
            {
                break;
            }
            if (t.type == TOK_SEMICOLON)
            {
                break;
            }

            if (t.type == TOK_LPAREN)
            {
                depth++;
            }
            if (t.type == TOK_RPAREN)
            {
                depth--;
                if (depth == 0)
                {
                    break;
                }
            }

            if (depth == 1 && t.type == TOK_COMMA)
            {
                is_tuple_lit = 1;
                break;
            }
        }

        if (is_tuple_lit)
        {
            n->ret.value = parse_tuple_expression(ctx, l, curr_func_ret, NULL);
            n->ret.value->token = return_token;
            handled = 1;
        }
    }

    if (!handled)
    {
        if (lexer_peek(l).type == TOK_SEMICOLON)
        {
            n->ret.value = NULL;
        }
        else
        {
            n->ret.value = parse_expression(ctx, l);
        }
    }

    if (lexer_peek(l).type == TOK_SEMICOLON)
    {
        lexer_next(l);
    }
    return n;
}
