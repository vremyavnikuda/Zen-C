// SPDX-License-Identifier: MIT
#include "parser.h"
#include "constants.h"
#include "ast/ast.h"
#include "analysis/move_check.h"
#include "plugins/plugin_manager.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "analysis/const_fold.h"
#include "utils/utils.h"
#include "ast/primitives.h"

void replace_it_with_var(ASTNode *node, char *var_name)
{
    if (!node)
    {
        return;
    }
    if (node->type == NODE_EXPR_VAR)
    {
        if (strcmp(node->var_ref.name, "it") == 0)
        {
            // Replace 'it' with var_name
            node->var_ref.name = xstrdup(var_name);
        }
    }
    else if (node->type == NODE_EXPR_CALL)
    {
        replace_it_with_var(node->call.callee, var_name);
        ASTNode *arg = node->call.args;
        while (arg)
        {
            replace_it_with_var(arg, var_name);
            arg = arg->next;
        }
    }
    else if (node->type == NODE_EXPR_MEMBER)
    {
        replace_it_with_var(node->member.target, var_name);
    }
    else if (node->type == NODE_EXPR_BINARY)
    {
        replace_it_with_var(node->binary.left, var_name);
        replace_it_with_var(node->binary.right, var_name);
    }
    else if (node->type == NODE_EXPR_UNARY)
    {
        replace_it_with_var(node->unary.operand, var_name);
    }
    else if (node->type == NODE_BLOCK)
    {
        ASTNode *s = node->block.statements;
        while (s)
        {
            replace_it_with_var(s, var_name);
            s = s->next;
        }
    }
}

ASTNode *parse_var_decl(ParserContext *ctx, Lexer *l, int is_export)
{
    Token tk = lexer_next(l); // eat 'let'

    // Destructuring: let {x, y} = ... OR let (a: type, b: type) = ...
    if (lexer_peek(l).type == TOK_LBRACE || lexer_peek(l).type == TOK_LPAREN)
    {
        int is_struct = (lexer_peek(l).type == TOK_LBRACE);
        lexer_next(l);
        int cap = 16;
        char **names = xmalloc(cap * sizeof(char *));
        char **types = xmalloc(cap * sizeof(char *));
        Type **type_infos = xmalloc(cap * sizeof(Type *));
        int count = 0;
        while (1)
        {
            if (count >= cap)
            {
                cap *= 2;
                names = xrealloc(names, cap * sizeof(char *));
                types = xrealloc(types, cap * sizeof(char *));
                type_infos = xrealloc(type_infos, cap * sizeof(Type *));
            }
            Token t = lexer_next(l);
            check_identifier(ctx, t);
            char *nm = token_strdup(t);
            names[count] = nm;
            types[count] = NULL;
            type_infos[count] = NULL;

            // Check for optional type annotation: name: type
            if (!is_struct && lexer_peek(l).type == TOK_COLON)
            {
                lexer_next(l); // eat :
                Type *type_obj = parse_type_formal(ctx, l);
                types[count] = type_obj ? type_to_string(type_obj) : xstrdup("unknown");
                type_infos[count] = type_obj;
                add_symbol(ctx, nm, types[count], type_obj, is_export);
            }
            else
            {
                add_symbol(ctx, nm, "unknown", NULL, is_export);
            }
            count++;

            Token next = lexer_next(l);
            if (next.type == (is_struct ? TOK_RBRACE : TOK_RPAREN))
            {
                break;
            }
            if (next.type != TOK_COMMA)
            {
                zpanic_at(next, "Expected comma in destructuring list");
            }
        }
        if (lexer_next(l).type != TOK_OP)
        {
            zpanic_at(lexer_peek(l), "Expected =");
        }
        ASTNode *init = parse_expression(ctx, l);
        if (lexer_peek(l).type == TOK_SEMICOLON)
        {
            lexer_next(l);
        }
        ASTNode *n = ast_create(NODE_DESTRUCT_VAR);
        n->token = tk;
        n->destruct.names = names;
        n->destruct.types = types;
        n->destruct.type_infos = type_infos;
        n->destruct.count = count;
        n->destruct.init_expr = init;
        n->destruct.is_struct_destruct = is_struct;
        return n;
    }

    // Normal Declaration OR Named Struct Destructuring
    Token name_tok = lexer_next(l);
    check_identifier(ctx, name_tok);
    char *name = token_strdup(name_tok);

    // Check for Struct Destructuring: var Point { x, y }
    if (lexer_peek(l).type == TOK_LBRACE)
    {
        lexer_next(l);
        char **names = xmalloc(16 * sizeof(char *));
        char **fields = xmalloc(16 * sizeof(char *));
        int count = 0;

        while (1)
        {
            // Parse field:name or just name
            Token t = lexer_next(l);
            check_identifier(ctx, t);
            char *ident = token_strdup(t);

            if (lexer_peek(l).type == TOK_COLON)
            {
                // field: var_name
                lexer_next(l); // eat :
                Token v = lexer_next(l);
                check_identifier(ctx, v);
                fields[count] = ident;
                names[count] = token_strdup(v);
            }
            else
            {
                // Shorthand: field (implies var name = field)
                fields[count] = ident;
                names[count] = ident; // Share pointer or duplicate? duplicate safer if we free
            }
            // Register symbol for variable
            add_symbol(ctx, names[count], "unknown", NULL, is_export);

            count++;

            Token next = lexer_next(l);
            if (next.type == TOK_RBRACE)
            {
                break;
            }
            if (next.type != TOK_COMMA)
            {
                zpanic_at(next, "Expected comma in struct pattern");
            }
        }

        if (lexer_next(l).type != TOK_OP)
        {
            zpanic_at(lexer_peek(l), "Expected =");
        }
        ASTNode *init = parse_expression(ctx, l);
        if (lexer_peek(l).type == TOK_SEMICOLON)
        {
            lexer_next(l);
        }

        ASTNode *n = ast_create(NODE_DESTRUCT_VAR);
        n->token = name_tok;
        n->destruct.names = names;
        n->destruct.field_names = fields;
        n->destruct.count = count;
        n->destruct.init_expr = init;
        n->destruct.is_struct_destruct = 1;
        n->destruct.struct_name = name; // "Point"
        return n;
    }

    // Check for Guard Pattern: var Some(val) = opt else { ... }
    if (lexer_peek(l).type == TOK_LPAREN)
    {
        lexer_next(l);
        Token val_tok = lexer_next(l);
        check_identifier(ctx, val_tok);
        char *val_name = token_strdup(val_tok);

        if (lexer_next(l).type != TOK_RPAREN)
        {
            zpanic_at(lexer_peek(l), "Expected ')' in guard pattern");
        }

        if (lexer_next(l).type != TOK_OP)
        {
            zpanic_at(lexer_peek(l), "Expected '=' after guard pattern");
        }

        ASTNode *init = parse_expression(ctx, l);

        Token t = lexer_next(l);
        if (t.type != TOK_IDENT || strncmp(t.start, "else", 4) != 0)
        {
            zpanic_at(t, "Expected 'else' in guard statement");
        }

        ASTNode *else_blk;
        if (lexer_peek(l).type == TOK_LBRACE)
        {
            else_blk = parse_block(ctx, l);
        }
        else
        {
            else_blk = ast_create(NODE_BLOCK);
            else_blk->block.statements = parse_statement(ctx, l);
        }

        if (lexer_peek(l).type == TOK_SEMICOLON)
        {
            lexer_next(l);
        }

        ASTNode *n = ast_create(NODE_DESTRUCT_VAR);
        n->token = t;
        n->destruct.names = xmalloc(sizeof(char *));
        n->destruct.names[0] = val_name;
        n->destruct.count = 1;
        n->destruct.init_expr = init;
        n->destruct.is_guard = 1;
        n->destruct.guard_variant = name;
        n->destruct.else_block = else_blk;

        add_symbol(ctx, val_name, "unknown", NULL, is_export);

        return n;
    }

    char *type = NULL;
    Type *type_obj = NULL; // --- NEW: Formal Type Object ---

    if (lexer_peek(l).type == TOK_COLON)
    {
        lexer_next(l);
        // Hybrid Parse: Get Object AND String
        type_obj = parse_type_formal(ctx, l);
        type = type_to_string(type_obj);
    }

    ASTNode *init = NULL;
    if (lexer_peek(l).type == TOK_OP && is_token(lexer_peek(l), "="))
    {
        lexer_next(l);

        // Peek for special initializers
        Token next = lexer_peek(l);
        if (next.type == TOK_IDENT && strncmp(next.start, "embed", 5) == 0)
        {
            init = parse_embed(ctx, l);

            // In fault-tolerant mode (LSP), parse_embed may return NULL
            // if the embedded file cannot be found. Create a placeholder.
            if (!init)
            {
                init = ast_create(NODE_RAW_STMT);
                init->token = next;
                init->raw_stmt.content = xstrdup("((Slice__char){0})");
                register_slice(ctx, "char");
                Type *fallback_t = type_new(TYPE_STRUCT);
                fallback_t->name = xstrdup("Slice__char");
                init->type_info = fallback_t;
            }

            if (!type && init->type_info)
            {
                type = type_to_string(init->type_info);
            }
            if (!type)
            {
                register_slice(ctx, "char");
                type = xstrdup("Slice__char");
            }
            if (lexer_peek(l).type == TOK_SEMICOLON)
            {
                lexer_next(l);
            }
        }
        else if (next.type == TOK_LBRACKET && type && strncmp(type, "Slice__", 7) == 0)
        {
            char *code = parse_array_literal(ctx, l, type);
            init = ast_create(NODE_RAW_STMT);
            init->token = next;
            init->raw_stmt.content = code;
            if (lexer_peek(l).type == TOK_SEMICOLON)
            {
                lexer_next(l);
            }
        }
        else if (next.type == TOK_LPAREN && type && strncmp(type, "Tuple__", 7) == 0)
        {
            char *code = parse_tuple_literal(ctx, l, type);
            init = ast_create(NODE_RAW_STMT);
            init->token = next;
            init->raw_stmt.content = code;
            if (lexer_peek(l).type == TOK_SEMICOLON)
            {
                lexer_next(l);
            }
        }
        else
        {
            init = parse_expression(ctx, l);
        }

        if (init && type)
        {
            char *rhs_type = init->resolved_type;
            if (!rhs_type && init->type_info)
            {
                rhs_type = type_to_string(init->type_info);
            }

            if (rhs_type && strchr(type, '*') && strchr(rhs_type, '*'))
            {
                // Strip stars to get struct names
                char target_struct[MAX_TYPE_NAME_LEN];
                strcpy(target_struct, type);
                target_struct[strlen(target_struct) - 1] = 0;
                char source_struct[MAX_TYPE_NAME_LEN];
                strcpy(source_struct, rhs_type);
                source_struct[strlen(source_struct) - 1] = 0;

                ASTNode *def = find_struct_def(ctx, source_struct);

                if (def && def->strct.parent && strcmp(def->strct.parent, target_struct) == 0)
                {
                    // Create Cast Node
                    ASTNode *cast = ast_create(NODE_EXPR_CAST);
                    cast->cast.target_type = xstrdup(type);
                    cast->cast.expr = init;
                    cast->type_info = type_obj; // Inherit formal type

                    init = cast; // Replace init with cast
                }
            }
        }

        // ** Type Inference Logic **
        if (!type && init)
        {
            if (init->type_info)
            {
                // Create new type to avoid inheriting is_const from builtins like true/false
                type_obj = type_new(init->type_info->kind);
                if (init->type_info->name)
                {
                    type_obj->name = xstrdup(init->type_info->name);
                }
                if (init->type_info->inner)
                {
                    type_obj->inner = init->type_info->inner; // Shallow copy for inner
                }
                if (init->type_info->kind == TYPE_ALIAS)
                {
                    type_obj->alias = init->type_info->alias;
                }
                // Copy function type args for lambda/closure support
                if (init->type_info->args && init->type_info->arg_count > 0)
                {
                    type_obj->args = init->type_info->args;
                    type_obj->arg_count = init->type_info->arg_count;
                    type_obj->is_varargs = init->type_info->is_varargs;
                }
                type_obj->array_size = init->type_info->array_size;
                type_obj->is_raw = init->type_info->is_raw;
                type_obj->is_explicit_struct = init->type_info->is_explicit_struct;
                type = type_to_string(type_obj);
            }
            else if (init->type == NODE_EXPR_SLICE)
            {
                zpanic_at(init->token, "Slice Node has NO Type Info!");
            }
            // Fallbacks for literals
            else if (init->type == NODE_EXPR_LITERAL)
            {
                if (init->literal.type_kind == LITERAL_INT)
                {
                    type = xstrdup("int");
                    type_obj = type_new(TYPE_INT);
                }
                else if (init->literal.type_kind == LITERAL_FLOAT)
                {
                    type = xstrdup("float");
                    type_obj = type_new(TYPE_FLOAT);
                }
                else if (init->literal.type_kind == LITERAL_STRING)
                {
                    type = xstrdup("string");
                    type_obj = type_new(TYPE_STRING);
                }
            }
            else if (init->type == NODE_EXPR_STRUCT_INIT)
            {
                type = xstrdup(init->struct_init.struct_name);
                type_obj = type_new(TYPE_STRUCT);
                type_obj->name = xstrdup(type);
            }
        }
    }

    if (!type && !init)
    {
        zpanic_at(name_tok, "Variable '%s' requires a type or initializer", name);
    }

    // Register in symbol table with actual token
    add_symbol_with_token(ctx, name, type, type_obj, name_tok, is_export);

    if (init && type_obj)
    {
        Type *t = init->type_info;
        if (!t && init->type == NODE_EXPR_VAR)
        {
            t = find_symbol_type_info(ctx, init->var_ref.name);
        }

        // Literal type construction for validation
        Type *temp_literal_type = NULL;
        if (!t && init->type == NODE_EXPR_LITERAL)
        {
            if (init->literal.type_kind == LITERAL_INT)
            {
                temp_literal_type = type_new(TYPE_INT);
            }
            else if (init->literal.type_kind == LITERAL_FLOAT)
            {
                temp_literal_type = type_new(TYPE_FLOAT);
            }
            else if (init->literal.type_kind == LITERAL_STRING)
            {
                temp_literal_type = type_new(TYPE_STRING);
            }
            else if (init->literal.type_kind == LITERAL_CHAR)
            {
                temp_literal_type = type_new(TYPE_CHAR);
            }
            t = temp_literal_type;
        }

        // Special case for literals: if implicit conversion works
        if (t && !type_eq(type_obj, t))
        {
            // Allow integer compatibility if types are roughly ints (lax check in type_eq handles
            // most, but let's be safe)
            if (!check_opaque_alias_compat(ctx, type_obj, t))
            {
                char *expected = type_to_string(type_obj);
                char *got = type_to_string(t);
                zpanic_at(init->token, "Type validation failed. Expected '%s', but got '%s'",
                          expected, got);
                zfree(expected);
                zfree(got);
            }
        }

        if (temp_literal_type)
        {
            zfree(temp_literal_type); // Simple free, shallow
        }
    }

    // NEW: Capture Const Integer Values
    if (init && init->type == NODE_EXPR_LITERAL && init->literal.type_kind == LITERAL_INT)
    {
        ZenSymbol *s = find_symbol_entry(ctx, name); // Helper to find the struct
        if (s)
        {
            s->is_const_value = 1;
            s->const_int_val = init->literal.int_val;
        }
    }

    if (lexer_peek(l).type == TOK_SEMICOLON)
    {
        lexer_next(l);
    }

    ASTNode *n = ast_create(NODE_VAR_DECL);
    n->token = name_tok; // Save location
    n->var_decl.name = name;
    n->var_decl.type_str = type;
    n->var_decl.type_info = type_obj;
    n->type_info = type_obj;

    // Auto-construct Trait Object
    if (type && is_trait(type))
    {
        init = transform_to_trait_object(ctx, type, init);
    }

    n->var_decl.init_expr = init;

    // Move Semantics Logic for Initialization
    if (init && init->type == NODE_EXPR_VAR)
    {
        // Move semantics placeholder: find_symbol_entry(ctx, init->var_ref.name);
    }

    // Global detection: Either no scope (yet) OR root scope (no parent)
    if (!ctx->current_scope || !ctx->current_scope->parent)
    {
        add_to_global_list(ctx, n);
    }

    // Check for 'defer' (Value-Returning Defer)
    // Only capture if it is NOT a block defer (defer { ... })
    // If it is a block defer, we leave it for the next parse_statement call.
    if (lexer_peek(l).type == TOK_DEFER)
    {
        Lexer lookahead = *l;
        lexer_next(&lookahead); // Eat defer
        if (lexer_peek(&lookahead).type != TOK_LBRACE)
        {
            // Proceed to consume
            tk = lexer_next(l); // eat defer (real)

            // Parse the defer expression/statement
            // Usually defer close(it);
            // We parse expression.
            ASTNode *expr = parse_expression(ctx, l);

            // Handle "it" substitution
            replace_it_with_var(expr, name);

            if (lexer_peek(l).type == TOK_SEMICOLON)
            {
                lexer_next(l);
            }

            ASTNode *d = ast_create(NODE_DEFER);
            d->token = tk;
            d->defer_stmt.stmt = expr;

            // Chain it: var_decl -> defer
            n->next = d;
        }
    }

    return n;
}
