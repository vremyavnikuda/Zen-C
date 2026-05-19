// SPDX-License-Identifier: MIT
#include "parser.h"

// Returns 1 if the token can serve as a field/method name in member access.
// Accepts TOK_IDENT (normal names), TOK_INT (tuple access like .0),
// and any keyword that starts with an identifier character (like .expect).
// This file is intentionally minimal. All functions extracted to src/parser/expr/*.c
