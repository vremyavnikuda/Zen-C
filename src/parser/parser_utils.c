// SPDX-License-Identifier: MIT
#include "../plugins/plugin_manager.h"
#include "parser.h"
#include "../utils/format_expr.h"
#include "../utils/colors.h"
#include "../utils/utils.h"
#include "../constants.h"
#include "../ast/primitives.h"
#include <ctype.h>
#include "analysis/const_fold.h"

// This file is intentionally minimal. All functions extracted to src/parser/utils/*.c
