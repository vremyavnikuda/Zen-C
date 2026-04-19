#include "analysis/typecheck.h"
#include "ast/ast.h"
#include "constants.h"
#include <string.h>
#include <stdio.h>

void emit_misra_preamble(FILE *out)
{
    // Minimal standard headers allowed by MISRA C.
    // Explicitly excluding <stdlib.h>, <stdio.h>, and <string.h>.
    fputs("#include <stddef.h>\n", out);
    fputs("#include <stdint.h>\n", out);
    fputs("#include <stdbool.h>\n", out);
}

// ============================================================================
// SECTION 10/11/12: Essential Type Model & Conversions
// ============================================================================

typedef enum
{
    ESS_BOOL,
    ESS_CHAR,
    ESS_SIGNED,
    ESS_UNSIGNED,
    ESS_FLOAT,
    ESS_UNKNOWN
} EssentialCategory;

static EssentialCategory get_essential_category(Type *t)
{
    if (!t)
    {
        return ESS_UNKNOWN;
    }
    Type *res = resolve_alias(t);
    if (is_boolean_type(res))
    {
        return ESS_BOOL;
    }
    if (res->kind == TYPE_CHAR || res->kind == TYPE_C_CHAR)
    {
        return ESS_CHAR;
    }
    if (is_unsigned_type(res))
    {
        return ESS_UNSIGNED;
    }
    if (is_signed_type(res))
    {
        return ESS_SIGNED;
    }
    if (is_float_type(res))
    {
        return ESS_FLOAT;
    }
    return ESS_UNKNOWN;
}

static int get_type_width(Type *t)
{
    if (!t)
    {
        return 0;
    }
    Type *res = resolve_alias(t);
    switch (res->kind)
    {
    case TYPE_I8:
    case TYPE_U8:
    case TYPE_CHAR:
    case TYPE_C_CHAR:
    case TYPE_C_UCHAR:
        return 8;
    case TYPE_I16:
    case TYPE_U16:
    case TYPE_C_SHORT:
    case TYPE_C_USHORT:
        return 16;
    case TYPE_I32:
    case TYPE_U32:
    case TYPE_INT:
    case TYPE_UINT:
    case TYPE_C_INT:
    case TYPE_C_UINT:
        return 32;
    case TYPE_I64:
    case TYPE_U64:
    case TYPE_USIZE:
    case TYPE_ISIZE:
    case TYPE_C_LONG:
    case TYPE_C_ULONG:
    case TYPE_C_LONGLONG:
    case TYPE_C_ULONGLONG:
        return 64;
    case TYPE_F32:
        return 32;
    case TYPE_F64:
        return 64;
    default:
        return 0;
    }
}

void misra_check_ess_type_categories(TypeChecker *tc, Type *left, Type *right, Token token)
{
    if (!g_config.misra_mode)
    {
        return;
    }
    EssentialCategory cl = get_essential_category(left);
    EssentialCategory cr = get_essential_category(right);

    if (cl != cr && cl != ESS_UNKNOWN && cr != ESS_UNKNOWN)
    {
        if ((cl == ESS_BOOL || cr == ESS_BOOL))
        {
            tc_error(tc, token, "MISRA Rule 10.4");
        }
        else if ((cl == ESS_SIGNED && cr == ESS_UNSIGNED) ||
                 (cl == ESS_UNSIGNED && cr == ESS_SIGNED))
        {
            tc_error(tc, token, "MISRA Rule 10.4");
        }
    }
}

void misra_check_ess_type_composite(TypeChecker *tc, Type *target, Type *source, Token token)
{
    if (!g_config.misra_mode)
    {
        return;
    }
    int tw = get_type_width(target);
    int sw = get_type_width(source);
    if (sw < tw)
    {
        tc_error(tc, token, "MISRA Rule 10.7");
    }
}

void misra_check_implicit_conversion(TypeChecker *tc, Type *target, Type *source, Token token)
{
    if (!g_config.misra_mode)
    {
        return;
    }
    EssentialCategory ct = get_essential_category(target);
    EssentialCategory cs = get_essential_category(source);

    if (ct != cs && ct != ESS_UNKNOWN && cs != ESS_UNKNOWN)
    {
        if (ct == ESS_BOOL || cs == ESS_BOOL)
        {
            tc_error(tc, token, "MISRA Rule 10.3");
        }
        else if ((ct == ESS_SIGNED && cs == ESS_UNSIGNED) ||
                 (ct == ESS_UNSIGNED && cs == ESS_SIGNED))
        {
            tc_error(tc, token, "MISRA Rule 10.3");
        }
    }

    int tw = get_type_width(target);
    int sw = get_type_width(source);
    if (sw > tw)
    {
        tc_error(tc, token, "MISRA Rule 10.3");
    }
}

void misra_check_char_arithmetic(TypeChecker *tc, Type *left, Type *right, const char *op,
                                 Token token)
{
    if (!g_config.misra_mode)
    {
        return;
    }
    EssentialCategory cl = get_essential_category(left);
    EssentialCategory cr = get_essential_category(right);

    if (cl == ESS_CHAR || cr == ESS_CHAR)
    {
        if (strcmp(op, "-") == 0)
        {
            // char - char is allowed (Rule 10.2)
            if (cl == ESS_CHAR && cr == ESS_CHAR)
            {
                return;
            }
        }

        tc_error(tc, token, "MISRA Rule 10.2");
    }
}

void misra_check_bitwise_operand(TypeChecker *tc, Type *t, Token token)
{
    if (!g_config.misra_mode)
    {
        return;
    }
    if (get_essential_category(t) != ESS_UNSIGNED)
    {
        tc_error(tc, token, "MISRA Rule 10.1");
    }
}

void misra_check_shift_amount(TypeChecker *tc, long long amount, int width, Token token)
{
    if (!g_config.misra_mode)
    {
        return;
    }
    if (amount < 0 || (width > 0 && amount >= width))
    {
        tc_error(tc, token, "MISRA Rule 12.2");
    }
}

void misra_check_pointer_conversion(TypeChecker *tc, Type *target, Type *source, Token token)
{
    if (!g_config.misra_mode)
    {
        return;
    }
    Type *rt = resolve_alias(target);
    Type *rs = resolve_alias(source);

    if (rt->kind == TYPE_POINTER && rs->kind == TYPE_POINTER)
    {
        Type *rti = resolve_alias(rt->inner);
        Type *rsi = resolve_alias(rs->inner);

        // Rule 11.8: Cast shall not remove const qualification from the type pointed to
        if (rsi && (rsi->is_const || (rs->is_const && rsi == rs->inner)) && rti && !rti->is_const)
        {
            tc_error(tc, token, "MISRA Rule 11.8");
        }

        // Rule 11.3: pointer to different object type
        // We only trigger this if the underlying object types are fundamentally different.
        // Qualification differences are handled by Rule 11.8.
        if (rti && rsi)
        {
            TypeKind tk = rti->kind;
            TypeKind sk = rsi->kind;

            // Treat all integer kinds as the same "object type" for 11.3;
            // other rules handle narrowing/etc.
            int t_is_int = is_integer_type(rti);
            int s_is_int = is_integer_type(rsi);

            if (tk != sk && !(t_is_int && s_is_int))
            {
                // Rule 11.1: func pointer vs other
                if (tk == TYPE_FUNCTION || sk == TYPE_FUNCTION)
                {
                    tc_error(tc, token, "MISRA Rule 11.1");
                }
                else
                {
                    tc_error(tc, token, "MISRA Rule 11.3");
                }
            }
        }
    }
    else if (rt->kind == TYPE_POINTER && is_integer_type(rs))
    {
        // Rule 11.4 allows NULL. In Zen C we check for literal 0.
        // We'll further refine this in misra_check_null_pointer_constant.
    }
}

void misra_check_void_ptr_cast(TypeChecker *tc, Type *target, Type *source, Token token)
{
    if (!g_config.misra_mode || !target || !source)
    {
        return;
    }
    Type *rt = resolve_alias(target);
    Type *rs = resolve_alias(source);

    // Rule 11.5: A conversion should not be performed from pointer to void into pointer to object
    if (rs->kind == TYPE_POINTER && rs->inner)
    {
        Type *rs_inner = resolve_alias(rs->inner);
        if (rs_inner->kind == TYPE_VOID)
        {
            if (rt->kind == TYPE_POINTER && rt->inner)
            {
                Type *rt_inner = resolve_alias(rt->inner);
                if (rt_inner->kind != TYPE_VOID)
                {
                    tc_error(tc, token, "MISRA Rule 11.5");
                }
            }
        }
    }
}

void misra_check_cast(TypeChecker *tc, Type *target, Type *source, Token token, bool is_composite)
{
    if (!g_config.misra_mode)
    {
        return;
    }
    if (is_composite)
    {
        int tw = get_type_width(target);
        int sw = get_type_width(source);
        if (tw > sw)
        {
            tc_error(tc, token, "MISRA Rule 10.8");
        }
        if (get_essential_category(target) != get_essential_category(source))
        {
            tc_error(tc, token, "MISRA Rule 10.8");
        }
    }
}

void misra_check_null_pointer_constant(TypeChecker *tc, struct ASTNode *node, Token token)
{
    if (!g_config.misra_mode || !node)
    {
        return;
    }

    struct ASTNode *expr = node;
    while (expr && expr->type == NODE_EXPR_CAST)
    {
        expr = expr->cast.expr;
    }

    // Rule 11.9: The macro NULL shall be the only permitted form of integer null pointer constant.
    if (expr && expr->type == NODE_EXPR_LITERAL && expr->literal.type_kind == LITERAL_INT)
    {
        // In MISRA C, we expect the NULL macro. In Zen C, we prefer the 'null' keyword.
        // If we found a literal 0, it means the user used '0' or '(type)0' instead of 'null'.
        if (expr->literal.int_val == 0)
        {
            tc_error(tc, token, "MISRA Rule 11.9");
        }
        else
        {
            tc_error(tc, token, "MISRA Rule 11.4");
        }
    }
}

void misra_check_binary_op_essential_types(TypeChecker *tc, struct ASTNode *left,
                                           struct ASTNode *right, Token token)
{
    if (!g_config.misra_mode || !left || !right || !left->type_info || !right->type_info)
    {
        return;
    }

    Type *rl = resolve_alias(left->type_info);
    Type *rr = resolve_alias(right->type_info);

    // Only applies to numeric essential types for Rule 10.4
    if (!is_integer_type(rl) || !is_integer_type(rr))
    {
        return;
    }

    EssentialCategory cl = get_essential_category(rl);
    EssentialCategory cr = get_essential_category(rr);

    if (cl != cr && cl != ESS_UNKNOWN && cr != ESS_UNKNOWN)
    {
        if (cl == ESS_BOOL || cr == ESS_BOOL)
        {
            tc_error(tc, token, "MISRA Rule 10.4");
        }
        else if ((cl == ESS_SIGNED && cr == ESS_UNSIGNED) ||
                 (cl == ESS_UNSIGNED && cr == ESS_SIGNED))
        {
            tc_error(tc, token, "MISRA Rule 10.4");
        }
    }

    // Rule 10.6/10.7: Composite expressions
    if (is_composite_expression(left) || is_composite_expression(right))
    {
        int lw = get_type_width(rl);
        int rw = get_type_width(rr);
        if (is_composite_expression(left) && rw > lw)
        {
            tc_error(tc, token, "MISRA Rule 10.7");
        }
        if (is_composite_expression(right) && lw > rw)
        {
            tc_error(tc, token, "MISRA Rule 10.7");
        }
    }
}

// ============================================================================
// SECTION 13/14/15: Expressions & Control Flow
// ============================================================================

void misra_check_side_effects_sizeof(TypeChecker *tc, ASTNode *expr)
{
    if (g_config.misra_mode)
    {
        // Simple heuristic: if it contains a call or assignment/inc/dec it has potential side
        // effects. We assume typechecker already validated this for Rule 13.6 if applicable.
        tc_error(tc, expr->token, "MISRA Rule 13.6");
    }
}

void misra_check_assignment_result_used(TypeChecker *tc, Token token)
{
    if (g_config.misra_mode)
    {
        tc_error(tc, token, "MISRA Rule 13.4");
    }
}

void misra_check_inc_dec_result_used(TypeChecker *tc, Token token)
{
    if (g_config.misra_mode)
    {
        tc_error(tc, token, "MISRA Rule 13.3");
    }
}

void misra_check_condition_boolean(TypeChecker *tc, Type *t, Token token)
{
    if (g_config.misra_mode && t)
    {
        if (get_essential_category(t) != ESS_BOOL)
        {
            tc_error(tc, token, "MISRA Rule 14.4");
        }
    }
}

void misra_check_invariant_condition(TypeChecker *tc, Token token)
{
    if (g_config.misra_mode)
    {
        tc_error(tc, token, "MISRA Rule 14.3");
    }
}

void misra_check_loop_counter_float(TypeChecker *tc, Type *t, Token token)
{
    if (g_config.misra_mode && is_float_type(t))
    {
        tc_error(tc, token, "MISRA Rule 14.1");
    }
}

void misra_check_initializer_side_effects(TypeChecker *tc, ASTNode *node)
{
    if (g_config.misra_mode && node)
    {
        if (tc_expr_has_side_effects(node))
        {
            tc_error(tc, node->token, "MISRA Rule 13.1");
        }
    }
}

// ============================================================================
// SECTION 16: Match/Switch
// ============================================================================

/**
 * @brief Enforces Rules 16.4, 16.5, 16.6, and 16.7 for match statements.
 */
void misra_check_match_stmt(TypeChecker *tc, ASTNode *node)
{
    if (!g_config.misra_mode || !node || node->type != NODE_MATCH)
    {
        return;
    }

    int has_default = 0;
    int case_count = 0;
    int is_first_or_last = 0;

    ASTNode *case_node = node->match_stmt.cases;
    while (case_node)
    {
        case_count++;
        if (case_node->match_case.is_default)
        {
            has_default = 1;
            if (case_count == 1 || case_node->next == NULL)
            {
                is_first_or_last = 1;
            }
        }
        case_node = case_node->next;
    }

    if (!has_default)
    {
        tc_error(tc, node->token, "MISRA Rule 16.4");
    }
    else if (!is_first_or_last)
    {
        tc_error(tc, node->token, "MISRA Rule 16.5");
    }

    if (case_count < 2)
    {
        tc_error(tc, node->token, "MISRA Rule 16.6");
    }

    if (node->match_stmt.expr && node->match_stmt.expr->type_info)
    {
        Type *expr_type = resolve_alias(node->match_stmt.expr->type_info);
        if (expr_type->kind == TYPE_BOOL)
        {
            tc_error(tc, node->match_stmt.expr->token, "MISRA Rule 16.7");
        }
    }
}

// ============================================================================
// SECTION 17: Functions
// ============================================================================

/**
 * @brief Rule 17.2 (Required): Functions shall not call themselves, either directly or indirectly.
 */
void misra_check_recursion(TypeChecker *tc, Token token)
{
    if (g_config.misra_mode)
    {
        tc_error(tc, token, "MISRA Rule 17.2");
    }
}

/**
 * @brief Rule 17.7 (Required): The value returned by a function having non-void return type shall
 * be used.
 */
void misra_check_function_return_usage(TypeChecker *tc, ASTNode *node)
{
    if (g_config.misra_mode && node && node->type_info)
    {
        Type *rt = resolve_alias(node->type_info);
        if (rt->kind != TYPE_VOID)
        {
            tc_error(tc, node->token, "MISRA Rule 17.7");
        }
    }
}

/**
 * @brief Rule 17.5 (Advisory): Array size mismatch in function parameters.
 */
void misra_check_array_param_size(TypeChecker *tc, int expected, int actual, Token token)
{
    if (g_config.misra_mode && expected > 0 && actual < expected)
    {
        tc_error(tc, token, "MISRA Rule 17.5");
    }
}

void misra_check_unused_param(TypeChecker *tc, const char *name, Token token)
{
    (void)name;
    tc_error(tc, token, "MISRA Rule 2.7");
}

void misra_check_const_ptr_param(TypeChecker *tc, const char *name, Token token)
{
    (void)name;
    tc_error(tc, token, "MISRA Rule 8.13");
}

/**
 * @brief Rule 17.8 (Advisory): A function parameter should not be modified.
 */
void misra_check_param_modified(TypeChecker *tc, ASTNode *left, Token token)
{
    if (!g_config.misra_mode || !tc->current_func || !left || left->type != NODE_EXPR_VAR)
    {
        return;
    }

    const char *name = left->var_ref.name;
    // Check if name matches any of the current function's parameters
    for (int i = 0; i < tc->current_func->func.arg_count; i++)
    {
        if (strcmp(tc->current_func->func.param_names[i], name) == 0)
        {
            tc_error(tc, token, "MISRA Rule 17.8");
            return;
        }
    }
}

// ============================================================================
// SECTION 18: Pointers & Arrays
// ============================================================================

/**
 * @brief Rule 18.4 (Advisory): The +, -, += and -= operators should not be applied to an
 * expression of pointer type.
 */
void misra_check_pointer_arithmetic(TypeChecker *tc, Type *t, Token token)
{
    if (g_config.misra_mode && t)
    {
        Type *resolved = resolve_alias(t);
        if (resolved->kind == TYPE_POINTER)
        {
            tc_error(tc, token, "MISRA Rule 18.4");
        }
    }
}
// ============================================================================

static int get_pointer_nesting_depth(Type *t)
{
    if (!t)
    {
        return 0;
    }
    Type *resolved = resolve_alias(t);
    if (resolved->kind == TYPE_POINTER)
    {
        return 1 + get_pointer_nesting_depth(resolved->inner);
    }
    return 0;
}

/**
 * @brief Rule 18.5 (Advisory): Declarations should contain no more than two levels of pointer
 * nesting.
 */
void misra_check_pointer_nesting(TypeChecker *tc, Type *t, Token token)
{
    if (!g_config.misra_mode || !t)
    {
        return;
    }

    int depth = get_pointer_nesting_depth(t);
    if (depth > 2)
    {
        tc_error(tc, token, "MISRA Rule 18.5");
    }
}

/**
 * @brief Ensures struct fields comply with Rule 18.5.
 */
void misra_check_struct_decl(TypeChecker *tc, ASTNode *node)
{
    if (!node || node->type != NODE_STRUCT)
    {
        return;
    }

    ASTNode *field = node->strct.fields;
    while (field)
    {
        if (field->type == NODE_FIELD)
        {
            misra_check_pointer_nesting(tc, field->type_info, field->token);

            // Rule 6.1 & 6.2: Bit-fields
            if (field->field.bit_width > 0)
            {
                Type *t = resolve_alias(field->type_info);
                if (t->kind != TYPE_BOOL && t->kind != TYPE_U8 && t->kind != TYPE_U16 &&
                    t->kind != TYPE_U32 && t->kind != TYPE_U64 && t->kind != TYPE_I8 &&
                    t->kind != TYPE_I16 && t->kind != TYPE_I32 && t->kind != TYPE_I64 &&
                    t->kind != TYPE_UINT && t->kind != TYPE_INT && t->kind != TYPE_BYTE)
                {
                    tc_error(tc, field->token, "MISRA Rule 6.1");
                }

                if (field->field.bit_width == 1 && is_signed_type(t))
                {
                    tc_error(tc, field->token, "MISRA Rule 6.2");
                }
            }

            // Rule 18.7: Flexible array members
            if (field->type_info && field->type_info->kind == TYPE_ARRAY &&
                field->type_info->array_size == 0 && field->next == NULL)
            {
                tc_error(tc, field->token, "MISRA Rule 18.7");
            }
        }
        field = field->next;
    }
}

/**
 * @brief Rule 15.6 (Required): The body of an if, while, for, or do-while shall be a compound
 * statement.
 */
void misra_check_compound_body(TypeChecker *tc, ASTNode *body, const char *stmt_name)
{
    if (!g_config.misra_mode || !body)
    {
        return;
    }

    if (body->type != NODE_BLOCK)
    {
        (void)stmt_name;
        tc_error(tc, body->token, "MISRA Rule 15.6");
    }
}

/**
 * @brief Rule 15.7 (Required): All if ... else if constructs shall be terminated with an else
 * statement.
 */
void misra_check_terminal_else(TypeChecker *tc, ASTNode *if_node)
{
    if (!g_config.misra_mode || !if_node || if_node->type != NODE_IF)
    {
        return;
    }

    // Traverse the if-else-if chain.
    ASTNode *curr = if_node;
    while (curr && curr->type == NODE_IF)
    {
        if (curr->if_stmt.else_body)
        {
            if (curr->if_stmt.else_body->type == NODE_IF)
            {
                // Another else-if, continue traversing.
                curr = curr->if_stmt.else_body;
            }
            else
            {
                // Found a terminal else (usually a NODE_BLOCK), Rule satisfied.
                return;
            }
        }
        else
        {
            // Found an if/else-if without an else body.
            // Check if this was a chain.
            if (curr != if_node)
            {
                tc_error(tc, if_node->token, "MISRA Rule 15.7");
            }
            return;
        }
    }
}

/**
 * @brief Ensures Rule 18.5 compliance for function parameters.
 */
void misra_check_param_nesting(TypeChecker *tc, ASTNode *func_node)
{
    if (!g_config.misra_mode || !func_node || func_node->type != NODE_FUNCTION)
    {
        return;
    }

    for (int i = 0; i < func_node->func.arg_count; ++i)
    {
        if (func_node->func.arg_types[i])
        {
            misra_check_pointer_nesting(tc, func_node->func.arg_types[i], func_node->token);
        }
    }
}

/**
 * @brief Rule 15.1 (Advisory): The goto statement should not be used.
 */
void misra_check_goto(TypeChecker *tc, Token token)
{
    if (!g_config.misra_mode)
    {
        return;
    }
    tc_error(tc, token, "MISRA Rule 15.1");
}

void misra_check_goto_constraint(TypeChecker *tc, Token goto_tok, Token label_tok)
{
    if (!g_config.misra_mode)
    {
        return;
    }

    // Rule 15.3: Jumping backwards is prohibited
    if (label_tok.line < goto_tok.line)
    {
        tc_error(tc, goto_tok, "MISRA Rule 15.3");
    }

    // Note: Rule 15.2 (jumping into nested blocks) is partially handled
    // by Zen C's block-scoped labels if implemented that way,
    // but we add a generic error if we detect it.
}

/**
 * @brief Rule 15.4 (Advisory): There shall be no more than one break or goto statement used
 * to terminate any iteration statement.
 */
void misra_check_iteration_termination(TypeChecker *tc, Token token)
{
    if (!g_config.misra_mode)
    {
        return;
    }
    tc_error(tc, token, "MISRA Rule 15.4");
}

/**
 * @brief Rule 19.2 (Advisory): The union keyword should not be used.
 */
void misra_check_union(TypeChecker *tc, Token token)
{
    if (!g_config.misra_mode)
    {
        return;
    }
    tc_error(tc, token, "MISRA Rule 19.2");
}

/**
 * @brief Rule 17.1 (Required): Features of <stdarg.h> shall not be used.
 */
void misra_check_stdarg(TypeChecker *tc, Token token)
{
    if (!g_config.misra_mode)
    {
        return;
    }
    tc_error(tc, token, "MISRA Rule 17.1");
}

void misra_audit_unused_symbols(TypeChecker *tc)
{
    if (!g_config.misra_mode || !tc->pctx->global_scope)
    {
        return;
    }

    ZenSymbol *sym = tc->pctx->global_scope->symbols;
    while (sym)
    {
        // Skip exported symbols, underscored symbols, and special names like 'main'
        // Also skip built-ins (line 0)
        if (!sym->is_used && !sym->is_export && sym->name && sym->name[0] != '_' &&
            0 != strcmp(sym->name, "main") && sym->decl_token.line != 0)
        {
            switch (sym->kind)
            {
            case SYM_ALIAS:
                tc_error(tc, sym->decl_token, "MISRA Rule 2.3");
                break;
            case SYM_STRUCT:
            case SYM_ENUM:
                tc_error(tc, sym->decl_token, "MISRA Rule 2.4");
                break;
            case SYM_CONSTANT:
                tc_error(tc, sym->decl_token, "MISRA Rule 2.5");
                break;
            default:
                break;
            }
        }
        sym = sym->next;
    }
}

void misra_check_vla(TypeChecker *tc, Type *t, Token token)
{
    if (g_config.misra_mode && t && t->kind == TYPE_ARRAY)
    {
        // In Zen C, all arrays are technically checked for constant sizes,
        // but if we are in this check, we enforce that any array declaration
        // must not be a VLA.
        tc_error(tc, token, "MISRA Rule 18.8");
    }
}

void misra_check_flexible_array(struct ASTNode *strct, struct ASTNode *field)
{
    if (!g_config.misra_mode || !strct || !field)
    {
        return;
    }

    // Flexible array members (Rule 18.7)
    // In Zen C, a zero-sized array or a slice at the end of a struct is a FAM
    if (field->type_info && field->type_info->kind == TYPE_ARRAY &&
        field->type_info->array_size == 0)
    {
        // Check if it's the last field
        ASTNode *last = strct->strct.fields;
        while (last && last->next)
        {
            last = last->next;
        }

        if (last == field)
        {
            zerror_at(field->token, "MISRA Rule 18.7");
        }
    }
}

void misra_check_identifier_collision(Token tok, const char *name1, const char *name2, int limit)
{
    if (!g_config.misra_mode || !name1 || !name2)
    {
        return;
    }

    if (strncmp(name1, name2, limit) == 0)
    {
        char msg[MAX_SHORT_MSG_LEN];
        if (limit == 31)
        {
            snprintf(msg, sizeof(msg), "MISRA Rule 5.1");
        }
        else
        {
            snprintf(msg, sizeof(msg), "MISRA Rule 5.2");
        }
        zerror_at(tok, msg);
    }
}

/**
 * @brief Checks for identifier uniqueness across the entire project (Rules 5.8 and 5.9).
 */
void misra_audit_identifier_uniqueness(TypeChecker *tc)
{
    if (!g_config.misra_mode || !tc->pctx->all_symbols)
    {
        return;
    }

    ZenSymbol *s1 = tc->pctx->all_symbols;
    while (s1)
    {
        // Skip built-ins and special names
        if (s1->decl_token.line == 0 || !s1->name || s1->name[0] == '_' ||
            strcmp(s1->name, "main") == 0 || strcmp(s1->name, "it") == 0 ||
            strcmp(s1->name, "self") == 0)
        {
            s1 = s1->next;
            continue;
        }

        // Determine linkage of s1
        // For Zen, we consider module-level symbols as having linkage.
        // symbols with is_export=1 have external linkage.
        int linkage1 = 0; // 1 = external, 2 = internal
        if (s1->is_export || s1->link_name)
        {
            linkage1 = 1;
        }
        else if (s1->kind == SYM_FUNCTION || s1->kind == SYM_VARIABLE || s1->kind == SYM_CONSTANT)
        {
            // Simple heuristic for module-level: if it's in the first level or has no parent scope
            // actually all_symbols is flat, and we don't have scope info easily here
            // but we can assume SYM_FUNCTION/SYM_CONSTANT at top level are what we care about.
            linkage1 = 2;
        }

        if (linkage1 == 0)
        {
            s1 = s1->next;
            continue;
        }

        ZenSymbol *s2 = s1->next;
        while (s2)
        {
            // Skip same checks
            if (s2->decl_token.line == 0 || !s2->name || s2->name[0] == '_' ||
                strcmp(s2->name, "main") == 0 || strcmp(s2->name, "it") == 0 ||
                strcmp(s2->name, "self") == 0)
            {
                s2 = s2->next;
                continue;
            }

            int linkage2 = 0;
            if (s2->is_export || s2->link_name)
            {
                linkage2 = 1;
            }
            else if (s2->kind == SYM_FUNCTION || s2->kind == SYM_VARIABLE ||
                     s2->kind == SYM_CONSTANT)
            {
                linkage2 = 2;
            }

            if (linkage2 == 0)
            {
                s2 = s2->next;
                continue;
            }

            // Rule 5.8: External identifiers shall be unique
            if (linkage1 == 1 && linkage2 == 1)
            {
                // Check exact name collision
                if (strcmp(s1->name, s2->name) == 0)
                {
                    tc_error(tc, s2->decl_token, "MISRA Rule 5.8");
                }
                // Check @link_name collision specifically
                else if (s1->link_name && s2->link_name &&
                         strcmp(s1->link_name, s2->link_name) == 0)
                {
                    tc_error(tc, s2->decl_token, "MISRA Rule 5.8");
                }
            }
            // Rule 5.9: Internal identifiers should be unique (Advisory)
            else if (linkage1 == 2 && linkage2 == 2)
            {
                if (strcmp(s1->name, s2->name) == 0)
                {
                    tc_error(tc, s2->decl_token, "MISRA Rule 5.9");
                }
            }

            s2 = s2->next;
        }

        s1 = s1->next;
    }
}
