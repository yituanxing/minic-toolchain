typedef struct MiniLdScriptSymbolProvenance {
    size_t resolver_index;
    size_t command_index;
    size_t output_command;
    uint64_t value;
    bool active;
} MiniLdScriptSymbolProvenance;

/* An output-section symbol may be informational, rather than needed to
 * place bytes: e.g. Linux's _kernel_size = _end - _text appears before the
 * later _end definition. GNU resolves it after the section layout settles.
 * Capture the assignment's original dot, not the end-of-file dot. */
typedef struct MiniLdScriptDeferredOutputSymbol {
    const char *name;
    MiniLdScriptExprId expression;
    size_t output_command;
    uint64_t dot;
    bool hidden;
    bool pending;
} MiniLdScriptDeferredOutputSymbol;

typedef struct MiniLdScriptDeferredByte {
    MiniLdScriptExprId expression;
    uint64_t dot;
    size_t patch_begin;
    unsigned width;
} MiniLdScriptDeferredByte;

typedef struct MiniLdScriptLayoutState {
    MiniLdState *state;
    MiniLdLayout *layout;
    const MiniLdScript *script;
    bool *section_placed;
    uint64_t dot;
    uint64_t load_bias;
    bool have_load_bias;
    uint64_t headers_size;
    bool have_headers_size;
    size_t dot_output_command;
    MiniLdScriptSymbolProvenance *symbol_provenance;
    size_t symbol_provenance_count;
    size_t symbol_provenance_capacity;
    MiniLdScriptDeferredOutputSymbol *deferred_outputs;
    size_t deferred_output_count;
    size_t deferred_output_capacity;
    MiniLdScriptDeferredByte *deferred_bytes;
    size_t deferred_byte_count;
    size_t deferred_byte_capacity;
    bool allow_forward_guess;
} MiniLdScriptLayoutState;

static bool script_layout_defer_byte(MiniLdScriptLayoutState *script_state,
                                     MiniLdScriptExprId expression,
                                     unsigned width) {
    MiniLdScriptDeferredByte *entry;

    if (script_state->deferred_byte_count ==
        script_state->deferred_byte_capacity) {
        size_t capacity = script_state->deferred_byte_capacity == 0U
                              ? 16U : script_state->deferred_byte_capacity * 2U;
        MiniLdScriptDeferredByte *next;

        if (capacity < script_state->deferred_byte_capacity ||
            capacity > SIZE_MAX / sizeof(*next)) {
            return false;
        }
        next = realloc(script_state->deferred_bytes,
                       capacity * sizeof(*next));
        if (next == NULL) return false;
        script_state->deferred_bytes = next;
        script_state->deferred_byte_capacity = capacity;
    }
    entry = &script_state->deferred_bytes[script_state->deferred_byte_count++];
    entry->expression = expression;
    entry->dot = script_state->dot;
    entry->patch_begin = script_state->layout->script_byte_count;
    entry->width = width;
    return true;
}

static bool script_layout_defer_output_symbol(
    MiniLdScriptLayoutState *script_state, const char *name,
    MiniLdScriptExprId expression, size_t output_command, bool hidden) {
    MiniLdScriptDeferredOutputSymbol *entry;

    if (script_state->deferred_output_count ==
        script_state->deferred_output_capacity) {
        size_t capacity = script_state->deferred_output_capacity == 0U
                              ? 32U : script_state->deferred_output_capacity * 2U;
        MiniLdScriptDeferredOutputSymbol *items;

        if (capacity < script_state->deferred_output_capacity ||
            capacity > SIZE_MAX / sizeof(*items)) {
            fprintf(script_state->state->diagnostics,
                    "minic-ld: script-deferred-symbol-overflow\n");
            return false;
        }
        items = realloc(script_state->deferred_outputs,
                        capacity * sizeof(*items));
        if (items == NULL) {
            fprintf(script_state->state->diagnostics,
                    "minic-ld: out-of-memory:script-deferred-symbols\n");
            return false;
        }
        script_state->deferred_outputs = items;
        script_state->deferred_output_capacity = capacity;
    }
    entry = &script_state->deferred_outputs[
        script_state->deferred_output_count++];
    entry->name = name;
    entry->expression = expression;
    entry->output_command = output_command;
    entry->dot = script_state->dot;
    entry->hidden = hidden;
    entry->pending = true;
    return true;
}

static bool static_align_up_u64(uint64_t value,
                                uint64_t alignment,
                                uint64_t *out) {
    uint64_t mask;

    if (alignment <= 1U) {
        *out = value;
        return true;
    }
    if ((alignment & (alignment - 1U)) != 0U) {
        return false;
    }
    mask = alignment - 1U;
    if (value > UINT64_MAX - mask) {
        return false;
    }
    *out = (value + mask) & ~mask;
    return true;
}

static bool script_layout_has_symbol_assignment(
    const MiniLdScript *script, const char *name) {
    size_t i;

    for (i = 0U; i < script->command_count; ++i) {
        const MiniLdScriptCommand *command = &script->commands[i];
        size_t j;

        if (command->kind == MINILD_SCRIPT_DEFINE_SYMBOL &&
            strcmp(command->value.symbol.name, name) == 0) {
            return true;
        }
        if (command->kind != MINILD_SCRIPT_OUTPUT_SECTION) continue;
        for (j = 0U; j < command->value.section.item_count; ++j) {
            const MiniLdScriptSectionItem *item =
                &command->value.section.items[j];

            if (item->kind == MINILD_SCRIPT_SECTION_DEFINE_SYMBOL &&
                strcmp(item->value.symbol.name, name) == 0) {
                return true;
            }
        }
    }
    return false;
}

/* GNU's expression evaluator interns every NAME lookup, including names
 * appearing only in linker-script expressions.  A later script reference
 * therefore activates an earlier PROVIDE, even with no undefined ELF
 * symbol (Linux zboot's LONG(zboot_code_size) is one such consumer).
 *
 * Walk command roots rather than the expression arena: an inactive
 * PROVIDE's own RHS must not make that PROVIDE reachable.  References from
 * an active PROVIDE can in turn activate another PROVIDE. */
static bool script_layout_expr_mentions_symbol(const MiniLdScript *script,
                                                MiniLdScriptExprId expression,
                                                const char *name,
                                                unsigned depth) {
    const MiniLdScriptExpr *expr;

    if (expression == MINILD_SCRIPT_EXPR_NONE ||
        expression >= script->expression_count || depth >= 256U) {
        return false;
    }
    expr = &script->expressions[expression];
    if (expr->kind == MINILD_SCRIPT_EXPR_SYMBOL) {
        return strcmp(expr->name, name) == 0;
    }
    return script_layout_expr_mentions_symbol(script, expr->left, name,
                                              depth + 1U) ||
           script_layout_expr_mentions_symbol(script, expr->right, name,
                                              depth + 1U);
}

static bool script_layout_name_referenced_unconditionally(
    const MiniLdScript *script, const char *name) {
    size_t i, j;

    for (i = 0U; i < script->command_count; ++i) {
        const MiniLdScriptCommand *command = &script->commands[i];
        const MiniLdScriptOutputSection *output;

        if (command->kind == MINILD_SCRIPT_DEFINE_SYMBOL) {
            if (!command->value.symbol.provide &&
                script_layout_expr_mentions_symbol(script,
                    command->value.symbol.expression, name, 0U)) {
                return true;
            }
            continue;
        }
        if (command->kind == MINILD_SCRIPT_SET_DOT ||
            command->kind == MINILD_SCRIPT_ASSERT) {
            if (script_layout_expr_mentions_symbol(script,
                    command->value.expression, name, 0U)) {
                return true;
            }
            continue;
        }
        if (command->kind != MINILD_SCRIPT_OUTPUT_SECTION) continue;
        output = &command->value.section;
        if (script_layout_expr_mentions_symbol(script, output->address,
                                               name, 0U) ||
            script_layout_expr_mentions_symbol(script, output->at,
                                               name, 0U) ||
            script_layout_expr_mentions_symbol(script, output->align,
                                               name, 0U) ||
            script_layout_expr_mentions_symbol(script, output->fill,
                                               name, 0U)) {
            return true;
        }
        for (j = 0U; j < output->item_count; ++j) {
            const MiniLdScriptSectionItem *item = &output->items[j];
            if (item->kind == MINILD_SCRIPT_SECTION_PATTERN ||
                item->kind == MINILD_SCRIPT_SECTION_CONSTRUCTORS ||
                (item->kind == MINILD_SCRIPT_SECTION_DEFINE_SYMBOL &&
                 item->value.symbol.provide)) {
                continue;
            }
            if (script_layout_expr_mentions_symbol(script,
                    item->kind == MINILD_SCRIPT_SECTION_DEFINE_SYMBOL
                        ? item->value.symbol.expression : item->value.expression,
                    name, 0U)) {
                return true;
            }
        }
    }
    return false;
}

static bool script_layout_name_referenced_by_script_inner(
    const MiniLdScript *script, const char *name, unsigned depth) {
    size_t i, j;

    if (script_layout_name_referenced_unconditionally(script, name)) {
        return true;
    }
    if (depth >= 128U) return false;
    for (i = 0U; i < script->command_count; ++i) {
        const MiniLdScriptCommand *command = &script->commands[i];
        const MiniLdScriptOutputSection *output;

        if (command->kind == MINILD_SCRIPT_DEFINE_SYMBOL) {
            if (command->value.symbol.provide &&
                strcmp(command->value.symbol.name, name) != 0 &&
                script_layout_expr_mentions_symbol(script,
                    command->value.symbol.expression, name, 0U) &&
                script_layout_name_referenced_by_script_inner(script,
                    command->value.symbol.name, depth + 1U)) {
                return true;
            }
            continue;
        }
        if (command->kind != MINILD_SCRIPT_OUTPUT_SECTION) continue;
        output = &command->value.section;
        for (j = 0U; j < output->item_count; ++j) {
            const MiniLdScriptSectionItem *item = &output->items[j];
            if (item->kind == MINILD_SCRIPT_SECTION_DEFINE_SYMBOL &&
                item->value.symbol.provide &&
                strcmp(item->value.symbol.name, name) != 0 &&
                script_layout_expr_mentions_symbol(script,
                    item->value.symbol.expression, name, 0U) &&
                script_layout_name_referenced_by_script_inner(script,
                    item->value.symbol.name, depth + 1U)) {
                return true;
            }
        }
    }
    return false;
}

static bool script_layout_name_referenced_by_script(
    const MiniLdScript *script, const char *name) {
    return script_layout_name_referenced_by_script_inner(script, name, 0U);
}

static bool script_layout_forward_guess(MiniLdScriptLayoutState *script_state,
                                        const char *name,
                                        uint64_t *value_out) {
    MiniLdState *state = script_state->state;
    size_t i;

    if (!script_state->allow_forward_guess ||
        !state->script_forward_allow ||
        !script_layout_has_symbol_assignment(script_state->script, name)) {
        return false;
    }
    for (i = 0U; i < state->script_forward_guess_count; ++i) {
        const MiniLdScriptForwardGuess *guess =
            &state->script_forward_guesses[i];

        if (strcmp(guess->name, name) == 0) {
            *value_out = guess->value;
            return true;
        }
    }
    if (state->script_forward_guess_count ==
        state->script_forward_guess_capacity) {
        size_t capacity = state->script_forward_guess_capacity == 0U
                              ? 8U : state->script_forward_guess_capacity * 2U;
        MiniLdScriptForwardGuess *guesses;

        if (capacity < state->script_forward_guess_capacity ||
            capacity > SIZE_MAX / sizeof(*guesses)) {
            return false;
        }
        guesses = realloc(state->script_forward_guesses,
                          capacity * sizeof(*guesses));
        if (guesses == NULL) return false;
        state->script_forward_guesses = guesses;
        state->script_forward_guess_capacity = capacity;
    }
    /* The location counter at a forward placement reference is a concrete
     * lower bound for a later end symbol. Never use an ungrounded zero. */
    state->script_forward_guesses[state->script_forward_guess_count].name = name;
    state->script_forward_guesses[state->script_forward_guess_count].value =
        script_state->dot;
    ++state->script_forward_guess_count;
    *value_out = script_state->dot;
    return true;
}

static bool script_layout_symbol_value(void *context,
                                       const char *name,
                                       uint64_t *value_out) {
    MiniLdScriptLayoutState *script_state = context;
    size_t index = symbol_resolver_lookup(&script_state->state->resolver, name);
    MiniLdResolvedSymbol *symbol;

    if (index == SIZE_MAX) {
        return script_layout_forward_guess(script_state, name, value_out);
    }
    symbol = &script_state->state->resolver.entries[index];
    if (symbol->section == MINILD_SECTION_ABS) {
        *value_out = symbol->value;
        return true;
    }
    if (symbol->section < 0 ||
        (size_t)symbol->section >= script_state->state->work.count ||
        !script_state->section_placed[symbol->section] ||
        script_state->layout->work[symbol->section].discarded) {
        return script_layout_forward_guess(script_state, name, value_out);
    }
    *value_out = script_state->layout->work[symbol->section].vaddr +
                 symbol->value;
    return true;
}

static bool script_layout_section_value(void *context,
                                        const char *name,
                                        uint64_t *value_out) {
    MiniLdScriptLayoutState *script_state = context;
    size_t i;

    for (i = 0U; i < script_state->script->command_count; ++i) {
        const MiniLdScriptCommand *command = &script_state->script->commands[i];

        if (command->kind == MINILD_SCRIPT_OUTPUT_SECTION &&
            script_state->layout->script_outputs[i].defined &&
            strcmp(command->value.section.name, name) == 0) {
            *value_out = script_state->layout->script_outputs[i].start;
            return true;
        }
    }
    return false;
}

static bool script_layout_section_size(void *context,
                                       const char *name,
                                       uint64_t *value_out) {
    MiniLdScriptLayoutState *script_state = context;
    size_t i;

    for (i = 0U; i < script_state->script->command_count; ++i) {
        const MiniLdScriptCommand *command = &script_state->script->commands[i];
        const MiniLdLayoutScriptOutput *output =
            &script_state->layout->script_outputs[i];

        if (command->kind == MINILD_SCRIPT_OUTPUT_SECTION && output->defined &&
            strcmp(command->value.section.name, name) == 0) {
            if (output->end < output->start) {
                return false;
            }
            *value_out = output->end - output->start;
            return true;
        }
    }
    return false;
}

static bool script_layout_evaluate(MiniLdScriptLayoutState *script_state,
                                   MiniLdScriptExprId expression,
                                   uint64_t *value_out) {
    MiniLdScriptEvalContext context;

    memset(&context, 0, sizeof(context));
    context.dot = script_state->dot;
    context.headers_size = script_state->headers_size;
    context.have_headers_size = script_state->have_headers_size;
    context.resolve_symbol = script_layout_symbol_value;
    context.resolve_section = script_layout_section_value;
    context.resolve_section_size = script_layout_section_size;
    context.user = script_state;
    return minild_script_evaluate(script_state->script,
                                  expression,
                                  &context,
                                  value_out,
                                  script_state->state->diagnostics);
}

static bool script_layout_evaluate_placement(
    MiniLdScriptLayoutState *script_state, MiniLdScriptExprId expression,
    uint64_t *value_out) {
    bool saved = script_state->allow_forward_guess;
    bool ok;

    script_state->allow_forward_guess = true;
    ok = script_layout_evaluate(script_state, expression, value_out);
    script_state->allow_forward_guess = saved;
    return ok;
}

/* GNU permits top-level script symbol assignments to refer to input
 * sections placed later in SECTIONS.  Whether that reference is direct or
 * buried inside ABSOLUTE/arithmetic/ALIGN is immaterial.  Delay only the
 * assignment, never the address-dependent placement commands themselves. */
static bool script_layout_expr_unplaced(
    MiniLdScriptLayoutState *script_state, MiniLdScriptExprId id,
    unsigned depth) {
    const MiniLdScriptExpr *expr;
    uint64_t dummy;

    if (id == MINILD_SCRIPT_EXPR_NONE) {
        return false;
    }
    if (id >= script_state->script->expression_count || depth > 128U) {
        return false; /* The actual evaluator diagnoses malformed ASTs. */
    }
    expr = &script_state->script->expressions[id];
    if (expr->kind == MINILD_SCRIPT_EXPR_SYMBOL) {
        return !script_layout_symbol_value(script_state, expr->name, &dummy);
    }
    if (expr->kind == MINILD_SCRIPT_EXPR_ADDR) {
        return !script_layout_section_value(script_state, expr->name, &dummy);
    }
    if (expr->kind == MINILD_SCRIPT_EXPR_SIZEOF) {
        return !script_layout_section_size(script_state, expr->name, &dummy);
    }
    return script_layout_expr_unplaced(script_state, expr->left, depth + 1U) ||
           script_layout_expr_unplaced(script_state, expr->right, depth + 1U);
}

static bool script_layout_define_symbol(MiniLdScriptLayoutState *script_state,
                                        const char *name,
                                        uint64_t value) {
    size_t index = symbol_resolver_lookup(&script_state->state->resolver, name);
    size_t i;

    for (i = 0U; i < script_state->symbol_provenance_count; ++i) {
        MiniLdScriptSymbolProvenance *binding =
            &script_state->symbol_provenance[i];

        if (index != SIZE_MAX && binding->resolver_index == index) {
            binding->active = false;
        }
    }

    if (!script_state->have_headers_size && index != SIZE_MAX) {
        uint64_t current;

        if (script_layout_symbol_value(script_state, name, &current) &&
            current == value) {
            return true;
        }
    }
    return symbol_resolver_assign_absolute(&script_state->state->resolver,
                                            script_state->state->diagnostics,
                                            name, value);
}

static bool script_layout_hide_symbol(MiniLdScriptLayoutState *script_state,
                                      const char *name) {
    size_t index = symbol_resolver_lookup(&script_state->state->resolver,
                                          name);

    if (index == SIZE_MAX) {
        fprintf(script_state->state->diagnostics,
                "minic-ld: linker-script-missing-hidden-symbol:%s\n", name);
        return false;
    }
    script_state->state->resolver.entries[index].other =
        (unsigned char)((script_state->state->resolver.entries[index].other &
                         ~(unsigned char)0x03U) | STV_HIDDEN);
    return true;
}

static bool script_layout_is_literal_expr(const MiniLdScript *script,
                                          MiniLdScriptExprId id,
                                          unsigned depth) {
    const MiniLdScriptExpr *expr;

    if (id == MINILD_SCRIPT_EXPR_NONE || id >= script->expression_count ||
        depth > 128U) {
        return false;
    }
    expr = &script->expressions[id];
    switch (expr->kind) {
    case MINILD_SCRIPT_EXPR_INTEGER:
        return true;
    case MINILD_SCRIPT_EXPR_NEGATE:
    case MINILD_SCRIPT_EXPR_BITWISE_NOT:
        return script_layout_is_literal_expr(script, expr->left, depth + 1U);
    case MINILD_SCRIPT_EXPR_ADD:
    case MINILD_SCRIPT_EXPR_SUBTRACT:
    case MINILD_SCRIPT_EXPR_MULTIPLY:
    case MINILD_SCRIPT_EXPR_DIVIDE:
    case MINILD_SCRIPT_EXPR_MODULO:
    case MINILD_SCRIPT_EXPR_SHIFT_LEFT:
    case MINILD_SCRIPT_EXPR_SHIFT_RIGHT:
    case MINILD_SCRIPT_EXPR_BITWISE_AND:
    case MINILD_SCRIPT_EXPR_BITWISE_XOR:
    case MINILD_SCRIPT_EXPR_BITWISE_OR:
        return script_layout_is_literal_expr(script, expr->left, depth + 1U) &&
               script_layout_is_literal_expr(script, expr->right, depth + 1U);
    default:
        return false;
    }
}

/* GNU top-level `foo = .` and `foo = . +/- constant` inherit the location
 * counter's section, whereas `foo = ABSOLUTE(.)` is an absolute definition.
 * Do not infer provenance from a previous symbol's ELF st_shndx: GNU treats
 * e.g. `alias = top_after_data` differently from `alias = .`. */
static bool script_layout_is_dot_relative_expr(const MiniLdScript *script,
                                                MiniLdScriptExprId id,
                                                unsigned depth) {
    const MiniLdScriptExpr *expr;

    if (id == MINILD_SCRIPT_EXPR_NONE || id >= script->expression_count ||
        depth > 128U) {
        return false;
    }
    expr = &script->expressions[id];
    if (expr->kind == MINILD_SCRIPT_EXPR_DOT) {
        return true;
    }
    if (expr->kind == MINILD_SCRIPT_EXPR_ADD) {
        return (script_layout_is_dot_relative_expr(script, expr->left,
                                                    depth + 1U) &&
                script_layout_is_literal_expr(script, expr->right,
                                               depth + 1U)) ||
               (script_layout_is_literal_expr(script, expr->left,
                                               depth + 1U) &&
                script_layout_is_dot_relative_expr(script, expr->right,
                                                    depth + 1U));
    }
    return expr->kind == MINILD_SCRIPT_EXPR_SUBTRACT &&
           script_layout_is_dot_relative_expr(script, expr->left,
                                              depth + 1U) &&
           script_layout_is_literal_expr(script, expr->right, depth + 1U);
}

/* A top-level GNU assignment can be an alias of a relocatable input symbol:
 * `jiffies = jiffies_64` must keep the section binding, not become SHN_ABS.
 * Arithmetic with one relocatable operand and a fixed scalar preserves the
 * base; ABSOLUTE(), differences of relocatables and bitwise transforms do
 * not. The numeric value is still evaluated by the common expression engine. */
static size_t script_layout_expr_reloc_section(
    MiniLdScriptLayoutState *script_state, MiniLdScriptExprId id,
    unsigned depth) {
    const MiniLdScriptExpr *expr;
    size_t left;
    size_t right;

    if (id == MINILD_SCRIPT_EXPR_NONE ||
        id >= script_state->script->expression_count || depth > 128U) {
        return SIZE_MAX;
    }
    expr = &script_state->script->expressions[id];
    if (expr->kind == MINILD_SCRIPT_EXPR_SYMBOL) {
        size_t index = symbol_resolver_lookup(&script_state->state->resolver,
                                               expr->name);
        if (index != SIZE_MAX &&
            script_state->state->resolver.entries[index].section >= 0) {
            size_t section = (size_t)script_state->state->resolver.entries[
                index].section;
            if (section < script_state->state->work.count &&
                script_state->section_placed[section] &&
                !script_state->layout->work[section].discarded) {
                return section;
            }
        }
        return SIZE_MAX;
    }
    if (expr->kind != MINILD_SCRIPT_EXPR_ADD &&
        expr->kind != MINILD_SCRIPT_EXPR_SUBTRACT) {
        return SIZE_MAX;
    }
    left = script_layout_expr_reloc_section(
        script_state, expr->left, depth + 1U);
    right = script_layout_expr_reloc_section(
        script_state, expr->right, depth + 1U);
    if (left != SIZE_MAX && right == SIZE_MAX) {
        return left;
    }
    if (expr->kind == MINILD_SCRIPT_EXPR_ADD &&
        right != SIZE_MAX && left == SIZE_MAX) {
        return right;
    }
    return SIZE_MAX;
}

static bool script_layout_bind_symbol_to_anchor(
    MiniLdScriptLayoutState *script_state, MiniLdResolvedSymbol *symbol,
    size_t anchor, uint64_t absolute_value) {
    uint64_t anchor_vaddr;

    if (anchor >= script_state->state->work.count ||
        anchor > (size_t)INT_MAX) {
        return false;
    }
    anchor_vaddr = script_state->layout->work[anchor].vaddr;
    if (absolute_value < anchor_vaddr &&
        anchor_vaddr - absolute_value > INT64_MAX) {
        fprintf(script_state->state->diagnostics,
                "minic-ld: linker-script-section-offset-out-of-range:%s\n",
                symbol->name);
        return false;
    }
    symbol->section = (int)anchor;
    symbol->value = absolute_value - anchor_vaddr;
    symbol->script_relative_negative = absolute_value < anchor_vaddr;
    return true;
}

static bool script_layout_define_top_symbol(
    MiniLdScriptLayoutState *script_state, size_t command_index,
    const char *name, MiniLdScriptExprId expr_id, uint64_t value) {
    MiniLdScriptSymbolProvenance *entry;
    size_t index;

    if (!script_state->have_headers_size) {
        size_t section = script_layout_expr_reloc_section(
            script_state, expr_id, 0U);
        if (section != SIZE_MAX) {
            MiniLdSymbolResolver *resolver = &script_state->state->resolver;
            MiniLdResolvedSymbol *symbol;
            unsigned char info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
            unsigned char other = STV_DEFAULT;

            index = symbol_resolver_lookup(resolver, name);
            if (index != SIZE_MAX) {
                /* Keep an original undefined object's type/visibility. */
                info = resolver->entries[index].info;
                other = resolver->entries[index].other;
            }
            if (!symbol_resolver_merge_global(
                    resolver, script_state->state->diagnostics,
                    name, MINILD_SECTION_UNDEF, 0U, 0U,
                    info, other, SIZE_MAX, &index)) {
                return false;
            }
            symbol = &resolver->entries[index];
            if (section > (size_t)INT_MAX) {
                return false;
            }
            if (!script_layout_bind_symbol_to_anchor(
                    script_state, symbol, section, value)) {
                return false;
            }
            symbol->size = 0U;
            symbol->info = info;
            symbol->other = other;
            symbol->source_symbol = SIZE_MAX;
            symbol->script_placeholder = false;
            return true;
        }
    }

    if (!script_layout_define_symbol(script_state, name, value)) {
        return false;
    }
    if (!script_layout_is_dot_relative_expr(script_state->script, expr_id,
                                            0U)) {
        return true;
    }
    index = symbol_resolver_lookup(&script_state->state->resolver, name);
    if (index == SIZE_MAX) {
        fprintf(script_state->state->diagnostics,
                "minic-ld: linker-script-missing-defined-symbol:%s\n", name);
        return false;
    }
    if (script_state->symbol_provenance_count ==
            script_state->symbol_provenance_capacity) {
        size_t capacity = script_state->symbol_provenance_capacity == 0U
                              ? 16U
                              : script_state->symbol_provenance_capacity * 2U;
        MiniLdScriptSymbolProvenance *items;

        if (capacity < script_state->symbol_provenance_capacity ||
            capacity > SIZE_MAX / sizeof(*items)) {
            fprintf(script_state->state->diagnostics,
                    "minic-ld: linker-script-symbol-provenance-overflow\n");
            return false;
        }
        items = realloc(script_state->symbol_provenance,
                        capacity * sizeof(*items));
        if (items == NULL) {
            fprintf(script_state->state->diagnostics,
                    "minic-ld: out-of-memory:linker-script-symbol-provenance\n");
            return false;
        }
        script_state->symbol_provenance = items;
        script_state->symbol_provenance_capacity = capacity;
    }
    entry = &script_state->symbol_provenance[
        script_state->symbol_provenance_count++];
    entry->resolver_index = index;
    entry->command_index = command_index;
    entry->output_command = script_state->dot_output_command;
    entry->value = value;
    entry->active = true;
    return true;
}

/* A script reference is as real as an input relocation for GNU PROVIDE.
 * The input side has already been admitted when this is called. */
static bool script_layout_provide_needed(const MiniLdState *state,
                                         const MiniLdScript *script,
                                         const char *name) {
    size_t index = symbol_resolver_lookup(&state->resolver, name);

    if (index != SIZE_MAX &&
        state->resolver.entries[index].section == MINILD_SECTION_UNDEF &&
        !state->resolver.entries[index].script_placeholder) {
        return true;
    }
    return script_layout_name_referenced_by_script(script, name);
}

/* Register future shared-script definitions in the ONE canonical resolver
 * before dynsym/PLT/GOT sizing.  This pass deliberately does not evaluate
 * expressions: the value of `.` and section membership belong to layout.
 * The synthetic slot is a real UND placeholder, not an invented ABS zero.
 * PROVIDE is active only for an original, unresolved input reference;
 * artificially reserved forced names must not trigger it. */
static bool minild_script_declare_shared_name(MiniLdState *state,
                                               const MiniLdScript *script,
                                               const char *name,
                                               bool provide,
                                               bool hidden,
                                               bool provably_absolute) {
    size_t index = symbol_resolver_lookup(&state->resolver, name);

    if (provide) {
        if (script_layout_provide_needed(state, script, name) &&
            (index == SIZE_MAX ||
             state->resolver.entries[index].section ==
                 MINILD_SECTION_UNDEF)) {
            if (index == SIZE_MAX) {
                if (!symbol_resolver_merge_global(
                        &state->resolver, state->diagnostics, name,
                        MINILD_SECTION_UNDEF, 0U, 0U,
                        ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE), STV_DEFAULT,
                        SIZE_MAX, &index)) {
                    return false;
                }
            }
            if (!state->resolver.entries[index].script_planned_defined) {
                state->resolver.entries[index].script_planned_absolute =
                    provably_absolute;
            } else {
                state->resolver.entries[index].script_planned_absolute &=
                    provably_absolute;
            }
            state->resolver.entries[index].script_planned_defined = true;
            if (hidden) {
                state->resolver.entries[index].other =
                    (unsigned char)((state->resolver.entries[index].other &
                                     ~(unsigned char)0x03U) | STV_HIDDEN);
            }
        }
        return true;
    }

    if (index == SIZE_MAX) {
        if (!symbol_resolver_merge_global(&state->resolver,
                                          state->diagnostics,
                                          name,
                                          MINILD_SECTION_UNDEF,
                                          0U,
                                          0U,
                                          ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE),
                                          STV_DEFAULT,
                                          SIZE_MAX,
                                          &index)) {
            return false;
        }
        state->resolver.entries[index].script_placeholder = true;
    }
    if (!state->resolver.entries[index].script_planned_defined) {
        state->resolver.entries[index].script_planned_absolute =
            provably_absolute;
    } else {
        state->resolver.entries[index].script_planned_absolute &=
            provably_absolute;
    }
    state->resolver.entries[index].script_planned_defined = true;
    if (hidden) {
        state->resolver.entries[index].other =
            (unsigned char)((state->resolver.entries[index].other &
                             ~(unsigned char)0x03U) | STV_HIDDEN);
    }
    return true;
}

/* Prove only pure literal arithmetic or an explicit ABSOLUTE(...).
 * Section/dot/symbol provenance is resolved during layout, and an output
 * section's ordinary assignment may be rebound to the section even when
 * its expression is a literal.  Unknown expressions stay conservative. */
static bool minild_script_expr_guaranteed_absolute(
    const MiniLdScript *script, MiniLdScriptExprId id, bool in_section,
    unsigned depth) {
    const MiniLdScriptExpr *expr;

    if (id == MINILD_SCRIPT_EXPR_NONE || id >= script->expression_count ||
        depth >= 128U) {
        return false;
    }
    expr = &script->expressions[id];
    if (expr->kind == MINILD_SCRIPT_EXPR_ABSOLUTE) {
        return true;
    }
    if (in_section) {
        return false;
    }
    switch (expr->kind) {
    case MINILD_SCRIPT_EXPR_INTEGER:
        return true;
    case MINILD_SCRIPT_EXPR_NEGATE:
        return minild_script_expr_guaranteed_absolute(
            script, expr->left, false, depth + 1U);
    case MINILD_SCRIPT_EXPR_ADD:
    case MINILD_SCRIPT_EXPR_SUBTRACT:
    case MINILD_SCRIPT_EXPR_MULTIPLY:
    case MINILD_SCRIPT_EXPR_DIVIDE:
    case MINILD_SCRIPT_EXPR_SHIFT_LEFT:
        return minild_script_expr_guaranteed_absolute(
                   script, expr->left, false, depth + 1U) &&
               minild_script_expr_guaranteed_absolute(
                   script, expr->right, false, depth + 1U);
    default:
        return false;
    }
}

/* Only the shared -T driver invokes this directly after input admission,
 * COMMON allocation and source /DISCARD/.  Static links preserve their
 * existing script declaration and PROVIDE behavior unchanged. */
static bool minild_script_declare_shared_symbols(MiniLdState *state,
                                                  const MiniLdScript *script) {
    size_t i;

    for (i = 0U; i < script->command_count; ++i) {
        const MiniLdScriptCommand *command = &script->commands[i];
        size_t j;

        if (command->kind == MINILD_SCRIPT_DEFINE_SYMBOL) {
            if (!minild_script_declare_shared_name(
                    state, script,
                    command->value.symbol.name,
                    command->value.symbol.provide,
                    command->value.symbol.hidden,
                    minild_script_expr_guaranteed_absolute(
                        script, command->value.symbol.expression, false,
                        0U))) {
                return false;
            }
            continue;
        }
        if (command->kind != MINILD_SCRIPT_OUTPUT_SECTION ||
            command->value.section.discard) {
            continue;
        }
        for (j = 0U; j < command->value.section.item_count; ++j) {
            const MiniLdScriptSectionItem *item =
                &command->value.section.items[j];

            if (item->kind == MINILD_SCRIPT_SECTION_DEFINE_SYMBOL &&
                !minild_script_declare_shared_name(
                    state, script, item->value.symbol.name,
                    item->value.symbol.provide,
                    item->value.symbol.hidden,
                    minild_script_expr_guaranteed_absolute(
                        script, item->value.symbol.expression, true, 0U))) {
                return false;
            }
        }
    }
    return true;
}

static bool script_layout_place_section(MiniLdScriptLayoutState *script_state,
                                        size_t section_index,
                                        size_t command_index) {
    MiniLdSection *section = &script_state->state->work.sections[section_index];
    const MiniLdScriptOutputSection *output =
        &script_state->script->commands[command_index].value.section;
    uint64_t aligned;

    /* GNU (INFO) is a non-allocating output section even when an input
     * fragment originally carried SHF_ALLOC. Keep its VMA/dot assignment
     * and bytes in the ELF file, but never map it through PT_LOAD. */
    if (output->info) {
        section->flags &= ~(uint64_t)SHF_ALLOC;
    }
    /* Linker-created GOT output sections obey pointer alignment; scripts
     * may omit an explicit ALIGN even when a synthetic GOT member exists. */
    if ((strcmp(output->name, ".got.plt") == 0 ||
         strcmp(output->name, ".got") == 0) &&
        section->align <
            (script_state->state->target->elf_class == ELFCLASS64 ? 8U : 4U)) {
        section->align =
            script_state->state->target->elf_class == ELFCLASS64 ? 8U : 4U;
    }

    if (!static_align_up_u64(script_state->dot,
                             section->align == 0U ? 1U : section->align,
                             &aligned)) {
        fprintf(script_state->state->diagnostics,
                "minic-ld: linker-script-section-alignment-overflow:%s\n",
                section->name);
        return false;
    }
    script_state->dot = aligned;
    script_state->layout->work[section_index].vaddr = script_state->dot;
    script_state->section_placed[section_index] = true;
    script_state->layout->work[section_index].owner_command = command_index;

    if ((section->flags & SHF_ALLOC) != 0U &&
        !minild_target_relax_section(script_state->state,
                                    section_index,
                                    script_state->dot)) {
        return false;
    }
    if ((uint64_t)section->size > UINT64_MAX - script_state->dot) {
        fprintf(script_state->state->diagnostics,
                "minic-ld: linker-script-section-size-overflow:%s\n",
                section->name);
        return false;
    }
    script_state->dot += (uint64_t)section->size;
    return true;
}

/*
 * GNU SORT_BY_NAME must be stable for equal input section names.
 * The previous insertion sort took O(n*n) strcmp calls for large Linux
 * .initcall and .text linker-script patterns. Sort by (name, input index)
 * once instead, preserving the exact old tie order.
 */
typedef struct MiniLdNamedMatch {
    size_t index;
    const char *name;
} MiniLdNamedMatch;

static int minild_compare_named_match(const void *lhs, const void *rhs) {
    const MiniLdNamedMatch *a = lhs;
    const MiniLdNamedMatch *b = rhs;
    int c = strcmp(a->name, b->name);
    if (c != 0) {
        return c;
    }
    if (a->index != b->index) {
        return a->index < b->index ? -1 : 1;
    }
    return 0;
}

static const char *script_section_source_name(const MiniLdSection *section) {
    return section->input_name != NULL ? section->input_name : section->name;
}

/* GNU scans input sections once for *(.foo .bar): these names are one
 * parenthesized selector, not two successive selectors. SORT_BY_NAME is an
 * explicit ordering boundary, and separate selectors remain independent. */
static size_t script_layout_selector_span(
    const MiniLdScriptOutputSection *output, size_t first) {
    const MiniLdScriptPattern *pattern =
        &output->items[first].value.pattern;
    size_t end = first + 1U;

    if (pattern->selector_id == 0U || pattern->sort || pattern->common) {
        return 1U;
    }
    while (end < output->item_count) {
        const MiniLdScriptSectionItem *item = &output->items[end];
        if (item->kind != MINILD_SCRIPT_SECTION_PATTERN ||
            item->value.pattern.selector_id != pattern->selector_id ||
            item->value.pattern.sort || item->value.pattern.common) {
            break;
        }
        ++end;
    }
    return end - first;
}

static bool script_layout_place_patterns(
    MiniLdScriptLayoutState *script_state,
    const MiniLdScriptSectionItem *items,
    size_t item_count,
    size_t command_index,
    bool discard) {
    const MiniLdScriptPattern *first = &items[0].value.pattern;
    size_t *matches;
    size_t count = 0U;
    size_t i;

    if (first->common) {
        return true;
    }

    matches = malloc((script_state->state->work.count == 0U
                          ? 1U
                          : script_state->state->work.count) *
                     sizeof(*matches));
    if (matches == NULL) {
        fprintf(script_state->state->diagnostics,
                "minic-ld: out-of-memory:linker-script-pattern\n");
        return false;
    }

    for (i = 0U; i < script_state->state->work.count; ++i) {
        if (script_state->section_placed[i] ||
            script_state->state->work.sections[i].gc_discarded ||
            script_state->layout->work[i].discarded) {
            continue;
        }
        bool selected = false;
        size_t item_index;

        for (item_index = 0U; item_index < item_count; ++item_index) {
            if (minild_script_pattern_matches_input(
                    &items[item_index].value.pattern,
                    script_section_source_name(
                        &script_state->state->work.sections[i]),
                    script_state->state->work.sections[i].input_path)) {
                selected = true;
                break;
            }
        }
        if (selected) {
            matches[count++] = i;
        }
    }

    if (first->sort && count > 1U) {
        MiniLdNamedMatch *sorted;
        if (count > SIZE_MAX / sizeof(*sorted)) {
            free(matches);
            fprintf(script_state->state->diagnostics,
                    "minic-ld: linker-script-pattern-sort-overflow\n");
            return false;
        }
        sorted = malloc(count * sizeof(*sorted));
        if (sorted == NULL) {
            free(matches);
            fprintf(script_state->state->diagnostics,
                    "minic-ld: out-of-memory:linker-script-pattern-sort\n");
            return false;
        }
        for (i = 0U; i < count; ++i) {
            sorted[i].index = matches[i];
            sorted[i].name =
                script_section_source_name(
                    &script_state->state->work.sections[matches[i]]);
        }
        qsort(sorted, count, sizeof(*sorted), minild_compare_named_match);
        for (i = 0U; i < count; ++i) {
            matches[i] = sorted[i].index;
        }
        free(sorted);
    }

    for (i = 0U; i < count; ++i) {
        size_t section_index = matches[i];

        if (discard) {
            script_state->layout->work[section_index].discarded = true;
            script_state->section_placed[section_index] = true;
            continue;
        }
        if (!script_layout_place_section(script_state,
                                         section_index,
                                         command_index)) {
            free(matches);
            return false;
        }
    }

    free(matches);
    return true;
}

static size_t script_layout_output_anchor(
    const MiniLdScriptLayoutState *script_state, size_t command_index) {
    size_t anchor = SIZE_MAX;
    size_t i;

    for (i = 0U; i < script_state->state->work.count; ++i) {
        MiniLdSection *section = &script_state->state->work.sections[i];

        if (script_state->layout->work[i].owner_command == command_index &&
            !script_state->layout->work[i].discarded &&
            (section->flags & SHF_ALLOC) != 0U &&
            (anchor == SIZE_MAX ||
             script_state->layout->work[i].vaddr <
                 script_state->layout->work[anchor].vaddr)) {
            anchor = i;
        }
    }
    return anchor;
}

/* GNU BFD's section_for_dot() excludes TLS when selecting a location-counter
 * section. If the next live output is TLS, prefer the last preceding ALLOC
 * non-TLS section; with none before it, use the following non-TLS output.
 * Numeric VMA ordering cannot select provenance (the next .data..percpu
 * output in x86 Linux intentionally has a zero VMA). */
static size_t script_layout_dot_anchor_after(
    const MiniLdScriptLayoutState *script_state, size_t first_command) {
    size_t i, j;

    for (i = first_command; i < script_state->script->command_count; ++i) {
        size_t anchor;

        if (script_state->script->commands[i].kind !=
                MINILD_SCRIPT_OUTPUT_SECTION) {
            continue;
        }
        anchor = script_layout_output_anchor(script_state, i);
        if (anchor != SIZE_MAX) {
            if ((script_state->state->work.sections[anchor].flags &
                 SHF_TLS) == 0U) {
                return anchor;
            }
            break;
        }
    }
    for (j = i; j > 0U; --j) {
        size_t anchor;

        if (script_state->script->commands[j - 1U].kind !=
                MINILD_SCRIPT_OUTPUT_SECTION) {
            continue;
        }
        anchor = script_layout_output_anchor(script_state, j - 1U);
        if (anchor != SIZE_MAX &&
            (script_state->state->work.sections[anchor].flags & SHF_TLS) == 0U) {
            return anchor;
        }
    }
    for (j = i + (i < script_state->script->command_count ? 1U : 0U);
         j < script_state->script->command_count; ++j) {
        size_t anchor;

        if (script_state->script->commands[j].kind !=
                MINILD_SCRIPT_OUTPUT_SECTION) {
            continue;
        }
        anchor = script_layout_output_anchor(script_state, j);
        if (anchor != SIZE_MAX &&
            (script_state->state->work.sections[anchor].flags & SHF_TLS) == 0U) {
            return anchor;
        }
    }
    return SIZE_MAX;
}

static bool script_layout_bind_output_symbols(
    MiniLdScriptLayoutState *script_state,
    const MiniLdScriptOutputSection *output,
    size_t command_index) {
    size_t anchor = script_layout_output_anchor(script_state, command_index);
    size_t i;

    if (anchor == SIZE_MAX) {
        return true;
    }
    for (i = 0U; i < output->item_count; ++i) {
        const MiniLdScriptSectionItem *item = &output->items[i];
        MiniLdResolvedSymbol *symbol;
        size_t symbol_index;

        if (item->kind != MINILD_SCRIPT_SECTION_DEFINE_SYMBOL) {
            continue;
        }
        if (item->value.symbol.expression >=
                script_state->script->expression_count ||
            script_state->script->expressions[
                item->value.symbol.expression].kind ==
                MINILD_SCRIPT_EXPR_ABSOLUTE) {
            continue;
        }
        symbol_index = symbol_resolver_lookup(&script_state->state->resolver,
                                          item->value.symbol.name);
        if (symbol_index == SIZE_MAX) {
            if (item->value.symbol.provide) continue;
            fprintf(script_state->state->diagnostics,
                    "minic-ld: linker-script-missing-defined-symbol:%s\n",
                    item->value.symbol.name);
            return false;
        }
        symbol = &script_state->state->resolver.entries[symbol_index];
        /* A GNU PROVIDE does not take ownership of a strong input symbol,
         * including an input SHN_ABS symbol that happens to numerically
         * match an output section's dot. */
        if (item->value.symbol.provide &&
            symbol->source_symbol != SIZE_MAX) {
            continue;
        }
        if (symbol->section != MINILD_SECTION_ABS) {
            continue;
        }
        if (!script_layout_bind_symbol_to_anchor(
                script_state, symbol, anchor, symbol->value)) {
            return false;
        }
    }
    return true;
}

/* A linker-script output consisting solely of an advancing dot expression
 * has no input fragments, but GNU still materializes a real SHT_NOBITS
 * section. Its load/write attributes derive from the inherited PHDRS
 * assignment (e.g. x86 Linux .orc_lookup inherits :data).
 *
 * Conversely, an output containing a script data directive but no input
 * fragment is real SHT_PROGBITS and normally defaults to SHF_ALLOC,
 * independent of PT_LOAD p_flags. GNU BFD uses SHF_WRITE if the first
 * content-producing operation follows a symbol assignment: the synthetic
 * section has already been opened as writable by that assignment (e.g.
 * x86 boot's setup_sig = .; LONG(...)). Assignments following data do not
 * retroactively change the section's flags. */
static uint64_t script_layout_synthetic_flags(
    const MiniLdScript *script, size_t command_index, bool has_bytes) {
    size_t selected_load = SIZE_MAX;
    size_t i;

    for (i = 0U; i <= command_index; ++i) {
        const MiniLdScriptCommand *command = &script->commands[i];
        const MiniLdScriptOutputSection *output;
        size_t j;

        if (command->kind != MINILD_SCRIPT_OUTPUT_SECTION) {
            continue;
        }
        output = &command->value.section;
        if (output->phdr_name_count == 0U) {
            continue;
        }
        selected_load = SIZE_MAX;
        for (j = 0U; j < output->phdr_name_count; ++j) {
            size_t p;
            for (p = 0U; p < script->phdr_count; ++p) {
                if (strcmp(output->phdr_names[j],
                           script->phdrs[p].name) == 0 &&
                    strcmp(script->phdrs[p].type_name,
                           "PT_LOAD") == 0) {
                    selected_load = p;
                    break;
                }
            }
        }
    }
    if (script->commands[command_index].value.section.info) {
        return 0U;
    }
    if (has_bytes) {
        const MiniLdScriptOutputSection *output =
            &script->commands[command_index].value.section;
        for (i = 0U; i < output->item_count; ++i) {
            MiniLdScriptSectionItemKind kind = output->items[i].kind;
            if (kind == MINILD_SCRIPT_SECTION_DEFINE_SYMBOL) {
                return SHF_ALLOC | SHF_WRITE;
            }
            if (kind == MINILD_SCRIPT_SECTION_BYTE ||
                kind == MINILD_SCRIPT_SECTION_SET_DOT) {
                break;
            }
        }
        return SHF_ALLOC;
    }
    if (selected_load != SIZE_MAX &&
        script->phdrs[selected_load].have_flags &&
        (script->phdrs[selected_load].flags & PF_W) != 0U) {
        return SHF_ALLOC | SHF_WRITE;
    }
    return SHF_ALLOC;
}

static bool script_layout_process_output(MiniLdScriptLayoutState *script_state,
                                         const MiniLdScriptOutputSection *output,
                                         size_t command_index) {
    uint64_t output_start;
    uint64_t lma_start;
    size_t byte_start = script_state->layout->script_byte_count;
    size_t i;

    script_state->layout->script_outputs[command_index].name = output->name;
    script_state->layout->script_outputs[command_index].informational =
        output->info;

    if (output->fill != MINILD_SCRIPT_EXPR_NONE) {
        uint64_t fill;

        if (!script_layout_evaluate(script_state, output->fill, &fill)) {
            return false;
        }
        script_state->layout->script_outputs[command_index].fill =
            fill;
        script_state->layout->script_outputs[command_index].fill_width =
            minild_script_fill_width(fill);
        script_state->layout->script_outputs[command_index].have_fill = true;
    }

    if (output->discard) {
        for (i = 0U; i < output->item_count; ++i) {
            const MiniLdScriptSectionItem *item = &output->items[i];

            if (item->kind == MINILD_SCRIPT_SECTION_PATTERN) {
                size_t span = script_layout_selector_span(output, i);
                if (!script_layout_place_patterns(script_state, item, span,
                                                  command_index, true)) {
                    return false;
                }
                i += span - 1U;
            }
        }
        return true;
    }

    if (output->address != MINILD_SCRIPT_EXPR_NONE) {
        if (!script_layout_evaluate(script_state,
                                    output->address,
                                    &script_state->dot)) {
            return false;
        }
    }
    if (output->align != MINILD_SCRIPT_EXPR_NONE) {
        uint64_t alignment;

        if (!script_layout_evaluate(script_state,
                                    output->align,
                                    &alignment) ||
            !static_align_up_u64(script_state->dot,
                                 alignment == 0U ? 1U : alignment,
                                 &script_state->dot)) {
            fprintf(script_state->state->diagnostics,
                    "minic-ld: linker-script-output-alignment:%s\n",
                    output->name);
            return false;
        }
        script_state->layout->script_outputs[command_index].align =
            alignment == 0U ? 1U : alignment;
    }

    /*
     * GNU ld aligns an output section to the strictest participating
     * input-section alignment BEFORE evaluating leading symbol
     * assignments. Without this, __start___param may precede the first
     * aligned __param input by two bytes, causing parse_args() to read
     * a shifted pointer and page-fault during Linux boot.
     *
     * Respect an explicit output VMA; no implicit VMA alignment may
     * override the address selected by the linker script.
     */
    if (output->address == MINILD_SCRIPT_EXPR_NONE) {
        uint64_t input_alignment = 1U;
        size_t item_index;

        for (item_index = 0U; item_index < output->item_count;
             ++item_index) {
            const MiniLdScriptSectionItem *item = &output->items[item_index];
            size_t section_index;

            if (item->kind != MINILD_SCRIPT_SECTION_PATTERN ||
                item->value.pattern.common) {
                continue;
            }
            for (section_index = 0U;
                 section_index < script_state->state->work.count;
                 ++section_index) {
                const MiniLdSection *section =
                    &script_state->state->work.sections[section_index];
                uint64_t align = section->align == 0U ? 1U : section->align;

                if (script_state->section_placed[section_index] ||
                    section->gc_discarded ||
                    script_state->layout->work[section_index].discarded ||
                    (section->flags & SHF_ALLOC) == 0U ||
                    !minild_script_pattern_matches_input(
                        &item->value.pattern,
                        script_section_source_name(section),
                        section->input_path)) {
                    continue;
                }
                if (align > input_alignment) {
                    input_alignment = align;
                }
            }
        }
        if (!static_align_up_u64(script_state->dot, input_alignment,
                                 &script_state->dot)) {
            fprintf(script_state->state->diagnostics,
                    "minic-ld: linker-script-input-alignment:%s\n",
                    output->name);
            return false;
        }
        if (script_state->layout->script_outputs[command_index].align <
            input_alignment) {
            script_state->layout->script_outputs[command_index].align =
                input_alignment;
        }
    }

    output_start = script_state->dot;
    script_state->layout->script_outputs[command_index].defined = true;
    script_state->layout->script_outputs[command_index].start = output_start;

    for (i = 0U; i < output->item_count; ++i) {
        const MiniLdScriptSectionItem *item = &output->items[i];
        uint64_t value;

        switch (item->kind) {
        case MINILD_SCRIPT_SECTION_PATTERN: {
            size_t span = script_layout_selector_span(output, i);
            if (!script_layout_place_patterns(script_state, item, span,
                                              command_index, false)) {
                return false;
            }
            i += span - 1U;
            break;
        }
        case MINILD_SCRIPT_SECTION_DEFINE_SYMBOL:
            if (item->value.symbol.provide) {
                size_t index = symbol_resolver_lookup(
                    &script_state->state->resolver, item->value.symbol.name);

                if (!script_layout_provide_needed(
                        script_state->state, script_state->script,
                        item->value.symbol.name) ||
                    (index != SIZE_MAX &&
                     script_state->state->resolver.entries[index].section !=
                         MINILD_SECTION_UNDEF)) {
                    break;
                }
            }
            if (script_layout_expr_unplaced(
                    script_state, item->value.symbol.expression, 0U)) {
                if (!script_layout_defer_output_symbol(
                        script_state, item->value.symbol.name,
                        item->value.symbol.expression, command_index,
                        item->value.symbol.hidden)) {
                    return false;
                }
                break;
            }
            if (!script_layout_evaluate(script_state,
                                        item->value.symbol.expression,
                                        &value) ||
                !script_layout_define_symbol(script_state,
                                             item->value.symbol.name,
                                             value) ||
                (item->value.symbol.hidden &&
                 !script_layout_hide_symbol(script_state,
                                            item->value.symbol.name))) {
                return false;
            }
            break;
        case MINILD_SCRIPT_SECTION_SET_DOT:
            if (!script_layout_evaluate_placement(script_state,
                                                  item->value.expression,
                                                  &value)) {
                return false;
            }
            /* GNU's `. = ASSERT(condition, "message");` is a validation
             * idiom, not a request to move the location counter to 1. */
            if (script_state->script->expressions[
                    item->value.expression].kind == MINILD_SCRIPT_EXPR_ASSERT) {
                if (value == 0U) {
                    return false;
                }
                break;
            }
            if (value < script_state->dot) {
                fprintf(script_state->state->diagnostics,
                        "minic-ld: linker-script-dot-moved-backward:%s\n",
                        output->name);
                return false;
            }
            script_state->dot = value;
            break;
        case MINILD_SCRIPT_SECTION_BYTE:
            if (script_layout_expr_unplaced(script_state,
                                            item->value.expression, 0U)) {
                if (!script_layout_defer_byte(script_state,
                                              item->value.expression,
                                              item->data_width)) {
                    fprintf(script_state->state->diagnostics,
                            "minic-ld: out-of-memory:deferred-script-data\n");
                    return false;
                }
                value = 0U;
            } else if (!script_layout_evaluate(script_state,
                                                item->value.expression,
                                                &value)) {
                return false;
            }
            if ((item->data_width != 1U && item->data_width != 2U &&
                 item->data_width != 4U && item->data_width != 8U) ||
                script_state->dot > UINT64_MAX -
                                         (uint64_t)item->data_width) {
                fprintf(script_state->state->diagnostics,
                        "minic-ld: linker-script-data-overflow:%s\n",
                        output->name);
                return false;
            }
            {
                MiniLdLayout *layout = script_state->layout;
                size_t count = (size_t)item->data_width;
                size_t n;
                if (layout->script_byte_count > SIZE_MAX - count) {
                    fprintf(script_state->state->diagnostics,
                            "minic-ld: script-data-count-overflow\n");
                    return false;
                }
                if (layout->script_byte_capacity -
                        layout->script_byte_count < count) {
                    size_t needed = layout->script_byte_count + count;
                    size_t capacity = layout->script_byte_capacity == 0U
                                          ? 16U : layout->script_byte_capacity;
                    MiniLdScriptBytePatch *patches;
                    while (capacity < needed &&
                           capacity <= SIZE_MAX / 2U) {
                        capacity *= 2U;
                    }
                    if (capacity < needed ||
                        capacity > SIZE_MAX / sizeof(*patches)) {
                        fprintf(script_state->state->diagnostics,
                                "minic-ld: script-data-count-overflow\n");
                        return false;
                    }
                    patches = realloc(layout->script_bytes,
                                      capacity * sizeof(*patches));
                    if (patches == NULL) {
                        fprintf(script_state->state->diagnostics,
                                "minic-ld: out-of-memory:script-data\n");
                        return false;
                    }
                    layout->script_bytes = patches;
                    layout->script_byte_capacity = capacity;
                }
                for (n = 0U; n < count; ++n) {
                    unsigned byte_index =
                        script_state->state->target->data_encoding ==
                                ELFDATA2MSB
                            ? (unsigned)(count - 1U - n) : (unsigned)n;
                    MiniLdScriptBytePatch *patch =
                        &layout->script_bytes[layout->script_byte_count++];
                    patch->command = command_index;
                    patch->address = script_state->dot + (uint64_t)n;
                    patch->value =
                        (unsigned char)(value >> (8U * byte_index));
                }
            }
            script_state->dot += (uint64_t)item->data_width;
            break;
        case MINILD_SCRIPT_SECTION_CONSTRUCTORS:
            break;
        }
    }

    script_state->layout->script_outputs[command_index].end =
        script_state->dot;

    /* A script output with no input fragments still needs a real WorkSection
     * when its dot grows: pure BYTE becomes PROGBITS; pure dot growth is
     * GNU SHT_NOBITS (Linux's .orc_lookup), not a vanished output. */
    if (script_state->dot > output_start) {
        bool has_input = false;
        bool has_bytes =
            script_state->layout->script_byte_count != byte_start;

        for (i = 0U; i < script_state->state->work.count; ++i) {
            if (script_state->layout->work[i].owner_command == command_index &&
                !script_state->layout->work[i].discarded) {
                has_input = true;
                break;
            }
        }
        if (!has_input) {
            MiniLdState *state = script_state->state;
            MiniLdSection *section;
            size_t synthetic_index;
            size_t length;

            if (script_state->dot < output_start ||
                script_state->dot - output_start > SIZE_MAX ||
                find_section_index(state, output->name,
                                   hash_text(output->name)) != SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-script-generated-section:%s\n",
                        output->name);
                return false;
            }
            length = (size_t)(script_state->dot - output_start);
            if (!find_or_add_section(state, output->name,
                                     has_bytes ? SHT_PROGBITS : SHT_NOBITS,
                                     script_layout_synthetic_flags(
                                         script_state->script, command_index,
                                         has_bytes),
                                     1U, 0U,
                                     &synthetic_index)) {
                return false;
            }
            section = &state->work.sections[synthetic_index];
            if (!section_append_zero(section, length)) {
                fprintf(state->diagnostics,
                        "minic-ld: script-generated-section-overflow:%s\n",
                        output->name);
                return false;
            }
            /* The generated work fragment is copied into the finalized
             * OutputSection after its fill pattern is applied.  Its holes
             * must carry that same pattern or the copy would overwrite
             * the script's requested fill with zero bytes. */
            if (has_bytes && length != 0U &&
                script_state->layout->script_outputs[command_index].have_fill) {
                const MiniLdLayoutScriptOutput *script_output =
                    &script_state->layout->script_outputs[command_index];
                minild_script_fill_bytes(section->data, length,
                                         script_output->fill,
                                         script_output->fill_width, 0U);
            }
            for (i = byte_start;
                 i < script_state->layout->script_byte_count; ++i) {
                const MiniLdScriptBytePatch *patch =
                    &script_state->layout->script_bytes[i];
                uint64_t off = patch->address - output_start;

                if (off >= length) {
                    fprintf(state->diagnostics,
                            "minic-ld: invalid-script-BYTE-address:%s\n",
                            output->name);
                    return false;
                }
                section->data[(size_t)off] = patch->value;
            }
            script_state->layout->work[synthetic_index].vaddr =
                output_start;
            script_state->layout->work[synthetic_index].owner_command = command_index;
            script_state->section_placed[synthetic_index] = true;
        }
    }

    if (output->at != MINILD_SCRIPT_EXPR_NONE) {
        if (!script_layout_evaluate(script_state,
                                    output->at,
                                    &lma_start)) {
            return false;
        }
        script_state->load_bias = output_start - lma_start;
        script_state->have_load_bias = true;
    } else if (script_state->have_load_bias) {
        lma_start = output_start - script_state->load_bias;
    } else {
        lma_start = output_start;
    }

    for (i = 0U; i < script_state->state->work.count; ++i) {
        if (script_state->layout->work[i].owner_command == command_index) {
            script_state->layout->work[i].laddr =
                lma_start +
                (script_state->layout->work[i].vaddr - output_start);
        }
    }
    if (!script_layout_bind_output_symbols(script_state,
                                           output,
                                           command_index)) {
        return false;
    }
    /* An empty output definition ends the previous section's dot ownership.
     * BFD gives symbols that follow the unmaterialized output to the next
     * materialized section, even when that successor has an explicit VMA. */
    script_state->dot_output_command =
        script_layout_output_anchor(script_state, command_index) != SIZE_MAX
            ? command_index : SIZE_MAX;
    return true;
}

static bool script_layout_finalize_top_symbol_provenance(
    MiniLdScriptLayoutState *script_state) {
    size_t i;

    for (i = 0U; i < script_state->symbol_provenance_count; ++i) {
        const MiniLdScriptSymbolProvenance *binding =
            &script_state->symbol_provenance[i];
        MiniLdResolvedSymbol *symbol;
        size_t owner = binding->output_command;
        size_t anchor;
        if (!binding->active) {
            continue;
        }
        if (owner == SIZE_MAX) {
            /* Top-level forward dot jumps transfer section provenance to
             * the next materialized output even when ALIGN leaves a hole
             * before its start.  GNU x86 Linux uses this for __end_rodata
             * preceding a 2 MiB boundary; its symbol remains section-
             * relative to the subsequent .data output. */
            anchor = script_layout_dot_anchor_after(
                script_state, binding->command_index + 1U);
        } else {
            anchor = script_layout_output_anchor(script_state, owner);
            if (anchor != SIZE_MAX &&
                (script_state->state->work.sections[anchor].flags & SHF_TLS) != 0U) {
                anchor = script_layout_dot_anchor_after(script_state, owner);
            }
        }
        symbol = &script_state->state->resolver.entries[
            binding->resolver_index];
        if (anchor == SIZE_MAX ||
            symbol->section != MINILD_SECTION_ABS ||
            symbol->value != binding->value) {
            fprintf(script_state->state->diagnostics,
                    "minic-ld: unsupported-script-symbol-provenance:%s\n",
                    symbol->name);
            return false;
        }
        if (!script_layout_bind_symbol_to_anchor(
                script_state, symbol, anchor, binding->value)) {
            return false;
        }
    }

    /* BFD does not materialize a section that contains only assignments
     * and no emitted data. Its dot-relative symbols belong to the next
     * materialized output, regardless of that output's explicitly selected
     * VMA (x86 Linux's zero-VMA .data..percpu follows .init.begin). With
     * no successor, BFD retains the preceding section as their owner.
     * Resolve this after placement, when both anchors are known. */
    for (i = 0U; i < script_state->script->command_count; ++i) {
        const MiniLdScriptCommand *command = &script_state->script->commands[i];
        size_t j;

        if (command->kind != MINILD_SCRIPT_OUTPUT_SECTION ||
            script_layout_output_anchor(script_state, i) != SIZE_MAX) {
            continue;
        }
        for (j = 0U; j < command->value.section.item_count; ++j) {
            const MiniLdScriptSectionItem *item =
                &command->value.section.items[j];
            MiniLdResolvedSymbol *symbol;
            size_t symbol_index, anchor;

            if (item->kind != MINILD_SCRIPT_SECTION_DEFINE_SYMBOL ||
                item->value.symbol.expression >=
                    script_state->script->expression_count ||
                script_state->script->expressions[
                    item->value.symbol.expression].kind ==
                    MINILD_SCRIPT_EXPR_ABSOLUTE) {
                continue;
            }
            symbol_index = symbol_resolver_lookup(
                &script_state->state->resolver, item->value.symbol.name);
            if (symbol_index == SIZE_MAX) {
                if (item->value.symbol.provide) continue;
                return false;
            }
            symbol = &script_state->state->resolver.entries[symbol_index];
            if (item->value.symbol.provide &&
                symbol->source_symbol != SIZE_MAX) {
                continue;
            }
            if (symbol->section != MINILD_SECTION_ABS) {
                continue;
            }
            anchor = script_layout_dot_anchor_after(script_state, i + 1U);
            if (anchor != SIZE_MAX &&
                !script_layout_bind_symbol_to_anchor(
                    script_state, symbol, anchor, symbol->value)) {
                return false;
            }
        }
    }
    return true;
}

/*
 * The old layout scanned all sections to find the next lowest LMA on EVERY
 * iteration (quadratic in alloc section count). Linux emits many section
 * fragments. Sorting stable keys once preserves the original first-minimum
 * selection exactly while reducing this operation to O(S log S).
 */
typedef struct MiniLdFileOrder {
    uint64_t laddr;
    uint64_t vaddr;
    size_t index;
} MiniLdFileOrder;

static int minild_compare_file_order(const void *lhs, const void *rhs) {
    const MiniLdFileOrder *a = lhs;
    const MiniLdFileOrder *b = rhs;
    if (a->laddr != b->laddr) {
        return a->laddr < b->laddr ? -1 : 1;
    }
    if (a->vaddr != b->vaddr) {
        return a->vaddr < b->vaddr ? -1 : 1;
    }
    if (a->index != b->index) {
        return a->index < b->index ? -1 : 1;
    }
    return 0;
}

static bool static_build_script_layout(MiniLdState *state,
                                       const MiniLdScript *script,
                                       MiniLdLayout *layout,
                                       size_t shared_headers_size) {
    MiniLdScriptLayoutState script_state;
    MiniLdFileOrder *file_order = NULL;
    bool *command_deferred = NULL;
    uint64_t *command_deferred_dot = NULL;
    size_t *command_deferred_owner = NULL;
    size_t i;
    size_t alloc_count = 0U;
    bool have_alloc_tls = false;
    size_t last_file_backed = SIZE_MAX;
    /* The implicit no-separate-code writable PT_LOAD may contain later
     * read-only or executable sections.  Remember the whole load's
     * permissions, not merely the immediately preceding work member. */
    bool implicit_load_writable = false;
    uint64_t *owner_max_align = NULL;
    size_t *owner_remaining = NULL;
    bool *owner_file_backed = NULL;
    size_t *command_load = NULL;
    uint64_t *load_first_vaddr = NULL;
    size_t *load_first_file = NULL;
    size_t storage_count;
    size_t file_cursor;
    size_t header_size;
    bool ok = false;

    memset(layout, 0, sizeof(*layout));
    memset(&script_state, 0, sizeof(script_state));
    /* Reserve one additional generated section per script command. */
    if (script->command_count > SIZE_MAX - state->work.count) {
        fprintf(state->diagnostics,
                "minic-ld: script-section-count-overflow\n");
        return false;
    }
    storage_count = state->work.count + script->command_count;
    if (storage_count == 0U) {
        storage_count = 1U;
    }

    layout->work = calloc(storage_count, sizeof(*layout->work));
    layout->script_outputs =
        calloc(script->command_count == 0U ? 1U : script->command_count,
               sizeof(*layout->script_outputs));
    script_state.section_placed =
        calloc(storage_count,
               sizeof(*script_state.section_placed));
    command_deferred =
        calloc(script->command_count == 0U ? 1U : script->command_count,
               sizeof(*command_deferred));
    command_deferred_dot =
        calloc(script->command_count == 0U ? 1U : script->command_count,
               sizeof(*command_deferred_dot));
    command_deferred_owner =
        calloc(script->command_count == 0U ? 1U : script->command_count,
               sizeof(*command_deferred_owner));
    owner_max_align = calloc(script->command_count == 0U ? 1U :
                              script->command_count, sizeof(*owner_max_align));
    owner_remaining = calloc(script->command_count == 0U ? 1U :
                              script->command_count, sizeof(*owner_remaining));
    owner_file_backed = calloc(script->command_count == 0U ? 1U :
                               script->command_count,
                               sizeof(*owner_file_backed));
    /* Explicit PHDR ownership constrains each load's byte/VMA distance
     * whether GNU -n was requested or not.  x86 Linux's 2MiB-aligned
     * PT_LOADs are not a special case of nmagic's dense physical layout. */
    if (script->phdr_count != 0U) {
        size_t active_load = SIZE_MAX;
        size_t command;
        size_t j;

        command_load = malloc((script->command_count == 0U ? 1U :
                               script->command_count) * sizeof(*command_load));
        load_first_vaddr = calloc(script->phdr_count,
                                  sizeof(*load_first_vaddr));
        load_first_file = malloc(script->phdr_count * sizeof(*load_first_file));
        if (command_load == NULL || load_first_vaddr == NULL ||
            load_first_file == NULL) {
            fprintf(state->diagnostics,
                    "minic-ld: out-of-memory:script-PHDR-file-layout\n");
            goto done;
        }
        for (j = 0U; j < script->phdr_count; ++j) {
            load_first_file[j] = SIZE_MAX;
            if (active_load == SIZE_MAX &&
                strcmp(script->phdrs[j].type_name, "PT_LOAD") == 0) {
                active_load = j;
            }
        }
        for (command = 0U; command < script->command_count; ++command) {
            const MiniLdScriptCommand *cmd = &script->commands[command];
            command_load[command] = active_load;
            if (cmd->kind != MINILD_SCRIPT_OUTPUT_SECTION) {
                continue;
            }
            if (cmd->value.section.phdr_name_count != 0U) {
                active_load = SIZE_MAX;
                for (j = 0U; j < cmd->value.section.phdr_name_count; ++j) {
                    const char *name = cmd->value.section.phdr_names[j];
                    size_t k;

                    for (k = 0U; k < script->phdr_count; ++k) {
                        if (strcmp(name, script->phdrs[k].name) == 0 &&
                            strcmp(script->phdrs[k].type_name,
                                   "PT_LOAD") == 0) {
                            if (active_load != SIZE_MAX && active_load != k) {
                                fprintf(state->diagnostics,
                                        "minic-ld: unsupported-script-multiple-PHDR-LOADs\n");
                                goto done;
                            }
                            active_load = k;
                        }
                    }
                }
            }
            command_load[command] = active_load;
        }
    }

    if (layout->work == NULL || layout->script_outputs == NULL ||
        script_state.section_placed == NULL ||
        command_deferred == NULL || command_deferred_dot == NULL ||
        command_deferred_owner == NULL || owner_max_align == NULL ||
        owner_remaining == NULL || owner_file_backed == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:linker-script-layout\n");
        goto done;
    }

    for (i = 0U; i < storage_count; ++i) {
        layout->work[i].file_offset = SIZE_MAX;
        layout->work[i].owner_command = SIZE_MAX;
    }
    for (i = 0U; i < script->command_count; ++i) {
        layout->script_outputs[i].file_base = SIZE_MAX;
    }
    script_state.state = state;
    script_state.layout = layout;
    script_state.script = script;
    script_state.dot_output_command = SIZE_MAX;
    script_state.have_headers_size = shared_headers_size != 0U;
    script_state.headers_size = shared_headers_size;

    for (i = 0U; i < script->command_count; ++i) {
        const MiniLdScriptCommand *command = &script->commands[i];
        uint64_t value;

        switch (command->kind) {
        case MINILD_SCRIPT_ASSERT:
            if (!script_layout_evaluate(&script_state,
                                        command->value.expression,
                                        &value) || value == 0U) {
                fprintf(state->diagnostics,
                        "minic-ld: linker-script-assertion-failed\n");
                goto done;
            }
            break;
        case MINILD_SCRIPT_SET_DOT:
            if (!script_layout_evaluate_placement(&script_state,
                                                  command->value.expression,
                                                  &value)) {
                goto done;
            }
            if (script->expressions[command->value.expression].kind ==
                MINILD_SCRIPT_EXPR_ASSERT) {
                if (value == 0U) {
                    goto done;
                }
                break;
            }
            if (value < script_state.dot) {
                fprintf(state->diagnostics,
                        "minic-ld: linker-script-top-level-dot-moved-backward\n");
                goto done;
            }
            if (script_state.dot != value) {
                /* Moving top-level dot may enter the following output
                 * section, which is not constructed until later. */
                script_state.dot_output_command = SIZE_MAX;
            }
            script_state.dot = value;
            break;
        case MINILD_SCRIPT_DEFINE_SYMBOL:
            if (command->value.symbol.provide) {
                size_t sym = symbol_resolver_lookup(&state->resolver,
                                                command->value.symbol.name);
                if (!script_layout_provide_needed(
                        state, script, command->value.symbol.name) ||
                    (sym != SIZE_MAX &&
                     state->resolver.entries[sym].section !=
                         MINILD_SECTION_UNDEF)) {
                    break;
                }
            }
            if (script_layout_expr_unplaced(
                    &script_state, command->value.symbol.expression, 0U)) {
                command_deferred[i] = true;
                command_deferred_dot[i] = script_state.dot;
                command_deferred_owner[i] = script_state.dot_output_command;
                break;
            }
            if (!script_layout_evaluate(&script_state,
                                        command->value.symbol.expression,
                                        &value) ||
                !script_layout_define_top_symbol(
                    &script_state, i, command->value.symbol.name,
                    command->value.symbol.expression, value) ||
                (command->value.symbol.hidden &&
                 !script_layout_hide_symbol(&script_state,
                                            command->value.symbol.name))) {
                goto done;
            }
            break;
        case MINILD_SCRIPT_OUTPUT_SECTION:
            if (!script_layout_process_output(&script_state,
                                             &command->value.section,
                                             i)) {
                goto done;
            }
            break;
        }
    }

    {
        size_t remaining = script_state.deferred_output_count;
        size_t pass = 0U;

        for (i = 0U; i < script->command_count; ++i) {
            if (command_deferred[i]) ++remaining;
        }
        /* Acyclic forward references may span several symbol assignments.
         * Top-level and output-section definitions share one dependency
         * fixed point. No unresolved name is substituted with zero. */
        while (remaining != 0U) {
            bool progress = false;
            size_t k;

            for (k = 0U; k < script_state.deferred_output_count; ++k) {
                MiniLdScriptDeferredOutputSymbol *entry =
                    &script_state.deferred_outputs[k];
                uint64_t value, saved_dot;

                if (!entry->pending || script_layout_expr_unplaced(
                        &script_state, entry->expression, 0U)) {
                    continue;
                }
                saved_dot = script_state.dot;
                script_state.dot = entry->dot;
                if (!script_layout_evaluate(&script_state,
                                            entry->expression, &value) ||
                    !script_layout_define_symbol(&script_state,
                                                 entry->name, value) ||
                    (entry->hidden &&
                     !script_layout_hide_symbol(&script_state,
                                                entry->name))) {
                    goto done;
                }
                script_state.dot = saved_dot;
                entry->pending = false;
                --remaining;
                progress = true;
            }
            for (k = 0U; k < script->command_count; ++k) {
                const MiniLdScriptCommand *command = &script->commands[k];
                uint64_t value, saved_dot;
                size_t saved_owner;

                if (!command_deferred[k] ||
                    command->kind != MINILD_SCRIPT_DEFINE_SYMBOL) {
                    continue;
                }
                if (command->value.symbol.provide) {
                    size_t sym = symbol_resolver_lookup(
                        &state->resolver, command->value.symbol.name);
                    if (!script_layout_provide_needed(
                            state, script, command->value.symbol.name) ||
                        (sym != SIZE_MAX &&
                         state->resolver.entries[sym].section !=
                             MINILD_SECTION_UNDEF)) {
                        command_deferred[k] = false;
                        --remaining;
                        progress = true;
                        continue;
                    }
                }
                if (script_layout_expr_unplaced(
                        &script_state, command->value.symbol.expression,
                        0U)) {
                    continue;
                }
                saved_dot = script_state.dot;
                saved_owner = script_state.dot_output_command;
                script_state.dot = command_deferred_dot[k];
                script_state.dot_output_command =
                    command_deferred_owner[k];
                if (!script_layout_evaluate(
                        &script_state, command->value.symbol.expression,
                        &value) ||
                    !script_layout_define_top_symbol(
                        &script_state, k, command->value.symbol.name,
                        command->value.symbol.expression, value) ||
                    (command->value.symbol.hidden &&
                     !script_layout_hide_symbol(&script_state,
                                                command->value.symbol.name))) {
                    goto done;
                }
                script_state.dot = saved_dot;
                script_state.dot_output_command = saved_owner;
                command_deferred[k] = false;
                --remaining;
                progress = true;
            }
            if (!progress || ++pass > script->command_count +
                                      script_state.deferred_output_count) {
                const char *name = "<unknown>";

                for (k = 0U; k < script_state.deferred_output_count; ++k) {
                    if (script_state.deferred_outputs[k].pending) {
                        name = script_state.deferred_outputs[k].name;
                        break;
                    }
                }
                if (strcmp(name, "<unknown>") == 0) {
                    for (k = 0U; k < script->command_count; ++k) {
                        if (command_deferred[k]) {
                            name = script->commands[k].value.symbol.name;
                            break;
                        }
                    }
                }
                fprintf(state->diagnostics,
                        "minic-ld: linker-script-unresolved-forward-symbol:%s\n",
                        name);
                goto done;
            }
        }
    }

    /* Script BYTE/SHORT/LONG/QUAD expressions may reference symbols defined
     * later in the same script.  Their byte slots were reserved at the
     * original location counter; evaluate the final value only after the
     * symbol fixed point, preserving both dot and source section placement. */
    for (i = 0U; i < script_state.deferred_byte_count; ++i) {
        const MiniLdScriptDeferredByte *entry = &script_state.deferred_bytes[i];
        uint64_t value, saved_dot;
        unsigned n;

        if (entry->patch_begin > layout->script_byte_count ||
            entry->width > layout->script_byte_count - entry->patch_begin ||
            script_layout_expr_unplaced(&script_state, entry->expression,
                                        0U)) {
            fprintf(state->diagnostics,
                    "minic-ld: linker-script-unresolved-forward-data\n");
            goto done;
        }
        saved_dot = script_state.dot;
        script_state.dot = entry->dot;
        if (!script_layout_evaluate(&script_state, entry->expression, &value)) {
            script_state.dot = saved_dot;
            goto done;
        }
        script_state.dot = saved_dot;
        for (n = 0U; n < entry->width; ++n) {
            unsigned byte_index = state->target->data_encoding == ELFDATA2MSB
                                ? entry->width - 1U - n : n;
            MiniLdScriptBytePatch *patch =
                &layout->script_bytes[entry->patch_begin + (size_t)n];
            size_t j;

            patch->value = (unsigned char)(value >> (8U * byte_index));
            for (j = 0U; j < state->work.count; ++j) {
                MiniLdSection *section = &state->work.sections[j];
                uint64_t offset;

                if (layout->work[j].owner_command != patch->command ||
                    patch->address < layout->work[j].vaddr) {
                    continue;
                }
                offset = patch->address - layout->work[j].vaddr;
                if (offset < section->size) {
                    section->data[(size_t)offset] = patch->value;
                }
            }
        }
    }

    /* Deferred section assignments are now materialized, so perform their
     * section-relative binding using the same owner as immediate ones. */
    for (i = 0U; i < script->command_count; ++i) {
        if (script->commands[i].kind == MINILD_SCRIPT_OUTPUT_SECTION &&
            !script->commands[i].value.section.discard &&
            !script_layout_bind_output_symbols(
                &script_state, &script->commands[i].value.section, i)) {
            goto done;
        }
    }
    if (!script_layout_finalize_top_symbol_provenance(&script_state)) {
        goto done;
    }

    for (i = 0U; i < state->script_forward_guess_count; ++i) {
        MiniLdScriptForwardGuess *guess =
            &state->script_forward_guesses[i];
        uint64_t actual;

        if (!script_layout_symbol_value(&script_state,
                                        guess->name, &actual)) {
            fprintf(state->diagnostics,
                    "minic-ld: linker-script-forward-placement-unresolved:%s\n",
                    guess->name);
            goto done;
        }
        if (guess->value != actual) {
            guess->value = actual;
            state->script_forward_changed = true;
        }
    }

    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];

        if (script_state.section_placed[i] ||
            layout->work[i].discarded) {
            continue;
        }
        if ((section->flags & SHF_ALLOC) != 0U && section->size != 0U) {
            fprintf(state->diagnostics,
                    "minic-ld: linker-script-unplaced-alloc-section:%s:size=%zu\n",
                    section->name,
                    section->size);
            goto done;
        }
        layout->work[i].discarded = true;
        script_state.section_placed[i] = true;
    }

    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];
        size_t owner = layout->work[i].owner_command;

        if (layout->work[i].discarded ||
            (section->flags & SHF_ALLOC) == 0U ||
            section->size == 0U) {
            continue;
        }
        if (owner != SIZE_MAX && owner < script->command_count &&
            section->align > owner_max_align[owner]) {
            owner_max_align[owner] = section->align;
        }
        if (owner != SIZE_MAX && owner < script->command_count) {
            ++owner_remaining[owner];
            if (section->type != SHT_NOBITS) {
                owner_file_backed[owner] = true;
            }
        }
        ++alloc_count;
        if ((section->flags & SHF_TLS) != 0U) {
            have_alloc_tls = true;
        }
        if ((section->flags & SHF_WRITE) != 0U) {
            layout->have_rw = true;
        } else {
            layout->have_rx = true;
        }
    }

    {
        MiniLdElfAbiSizes abi =
            minild_elf_abi_sizes(state->target->elf_class);

        size_t header_phdr_count = script->phdr_count != 0U
                                       ? script->phdr_count +
                                             (have_alloc_tls ? 1U : 0U)
                                       : alloc_count;

        if (abi.phdr == 0U ||
            header_phdr_count < script->phdr_count ||
            header_phdr_count > (SIZE_MAX - abi.ehdr) / abi.phdr ||
            !align_up_size(abi.ehdr + header_phdr_count * abi.phdr,
                           state->nmagic
                               ? state->target->nmagic_load_alignment
                               : minild_static_page_size(state), &header_size)) {
            fprintf(state->diagnostics,
                    "minic-ld: linker-script-header-overflow\n");
            goto done;
        }
    }
    file_cursor = header_size;
    if (alloc_count > SIZE_MAX / sizeof(*file_order)) {
        fprintf(state->diagnostics,
                "minic-ld: linker-script-file-order-overflow\n");
        goto done;
    }
    file_order = malloc((alloc_count == 0U ? 1U : alloc_count) *
                        sizeof(*file_order));
    if (file_order == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:linker-script-file-order\n");
        goto done;
    }
    {
        size_t count = 0U;
        for (i = 0U; i < state->work.count; ++i) {
            const MiniLdSection *section = &state->work.sections[i];
            if (layout->work[i].discarded ||
                (section->flags & SHF_ALLOC) == 0U || section->size == 0U) {
                continue;
            }
            file_order[count].laddr = layout->work[i].laddr;
            file_order[count].vaddr = layout->work[i].vaddr;
            file_order[count].index = i;
            ++count;
        }
        if (count != alloc_count) {
            fprintf(state->diagnostics,
                    "minic-ld: linker-script-file-order-count-mismatch\n");
            goto done;
        }
    }
    qsort(file_order, alloc_count, sizeof(*file_order),
          minild_compare_file_order);
    for (i = 0U; i < alloc_count; ++i) {
        size_t best = file_order[i].index;
        const MiniLdSection *section = &state->work.sections[best];
        size_t owner = layout->work[best].owner_command;
        size_t load = SIZE_MAX;
        size_t desired_load_file = SIZE_MAX;
        size_t desired_same_page_file = SIZE_MAX;
        bool continues_implicit_load = false;
        size_t aligned;

        if (command_load != NULL) {
            if (owner < script->command_count) {
                load = command_load[owner];
            }
            if (load < script->phdr_count &&
                load_first_file[load] != SIZE_MAX) {
                uint64_t delta;

                if (layout->work[best].vaddr < load_first_vaddr[load] ||
                    (delta = layout->work[best].vaddr -
                             load_first_vaddr[load]) >
                        SIZE_MAX - load_first_file[load]) {
                    fprintf(state->diagnostics,
                            "minic-ld: unsupported-script-PHDR-address-order:%s\n",
                            section->name);
                    goto done;
                }
                desired_load_file = load_first_file[load] + (size_t)delta;
                if (file_cursor > desired_load_file) {
                    fprintf(state->diagnostics,
                            "minic-ld: unsupported-script-PHDR-file-order:%s\n",
                            section->name);
                    goto done;
                }
                file_cursor = desired_load_file;
            }
        }

        /* A pair of output sections may occupy the same ELF load page even
         * though their names/flags differ.  Preserve the VMA/LMA distance in
         * their file positions: otherwise no single PT_LOAD can describe
         * both without mapping unrelated file bytes over the first section.
         * Do this before choosing a new output-section file base.  Members
         * of an existing output already get exact dot-hole offsets below. */
        if (!state->nmagic && minild_static_page_size(state) != 0U &&
            last_file_backed != SIZE_MAX &&
            section->type != SHT_NOBITS) {
            size_t previous = last_file_backed;
            const MiniLdSection *prior = &state->work.sections[previous];
            const MiniLdLayoutWorkPlacement *prior_place =
                &layout->work[previous];
            const MiniLdLayoutWorkPlacement *this_place =
                &layout->work[best];
            uint64_t previous_end;

            if (prior->size <= UINT64_MAX - prior_place->vaddr &&
                prior->size <= UINT64_MAX - prior_place->laddr) {
                previous_end = prior_place->vaddr + (uint64_t)prior->size;
                uint64_t page_size = minild_static_page_size(state);
                uint64_t page_padding =
                    (page_size - previous_end % page_size) % page_size;
                uint64_t address_gap = this_place->vaddr >= previous_end
                                   ? this_place->vaddr - previous_end
                                   : UINT64_MAX;
                bool same_page = previous_end != 0U &&
                    (previous_end - 1U) / page_size ==
                        this_place->vaddr / page_size;
                /* GNU BFD -z noseparate-code carries a writable LOAD
                 * through an ALIGN(page) and a subsequent sub-page dot
                 * reservation.  Although the total VMA hole may exceed
                 * a page, the missing bytes must remain in the ELF file
                 * to keep the segment's file/VMA mapping affine.  A
                 * reservation of a whole additional page starts another
                 * LOAD instead.  Explicit PHDRS retain their own affine
                 * mapping and never take this implicit grouping policy. */
                bool nearby_writable_page =
                    script->phdr_count == 0U &&
                    !state->separate_code &&
                    implicit_load_writable &&
                    address_gap >= page_padding &&
                    address_gap - page_padding < page_size;
                if (previous_end != 0U &&
                    previous_end <= this_place->vaddr &&
                    (same_page || nearby_writable_page) &&
                    this_place->laddr >=
                        prior_place->laddr + (uint64_t)prior->size &&
                    this_place->laddr -
                        (prior_place->laddr + (uint64_t)prior->size) ==
                        this_place->vaddr - previous_end) {
                    uint64_t gap = this_place->vaddr - previous_end;
                    size_t desired;

                    if (prior_place->file_offset == SIZE_MAX ||
                        gap > SIZE_MAX - prior_place->file_offset ||
                        prior->size > SIZE_MAX - prior_place->file_offset -
                                      (size_t)gap) {
                        fprintf(state->diagnostics,
                                "minic-ld: script-same-page-file-overflow:%s\n",
                                section->name);
                        goto done;
                    }
                    desired = prior_place->file_offset + prior->size +
                              (size_t)gap;
                    if (file_cursor > desired) {
                        fprintf(state->diagnostics,
                                "minic-ld: unsupported-script-same-page-file-order:%s\n",
                                section->name);
                        goto done;
                    }
                    desired_same_page_file = desired;
                    file_cursor = desired;
                    continues_implicit_load = true;
                }
            }
        }

        if (!align_up_size(file_cursor,
                           section->align == 0U ? 1U : section->align,
                           &aligned)) {
            fprintf(state->diagnostics,
                    "minic-ld: linker-script-file-layout-overflow:%s\n",
                    section->name);
            goto done;
        }
        if (owner != SIZE_MAX && owner < script->command_count &&
            layout->script_outputs[owner].file_base != SIZE_MAX) {
            uint64_t delta;
            size_t expected;

            if (layout->work[best].vaddr <
                    layout->script_outputs[owner].start ||
                layout->work[best].vaddr -
                    layout->script_outputs[owner].start >
                    SIZE_MAX - layout->script_outputs[owner].file_base) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-script-output-order:%s\n",
                        section->name);
                goto done;
            }
            delta = layout->work[best].vaddr -
                    layout->script_outputs[owner].start;
            expected = layout->script_outputs[owner].file_base + (size_t)delta;
            /* An ELF output section has one sh_offset and one sh_addr.
             * Every member therefore needs the SAME file/VMA displacement.
             * Preserve script dot holes as real zero-filled file gaps. */
            if (expected < aligned ||
                expected % (section->align == 0U ? 1U : section->align) != 0U) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-script-output-file-order:%s\n",
                        section->name);
                goto done;
            }
            file_cursor = expected;
        } else {
            file_cursor = aligned;
            if (owner != SIZE_MAX && owner < script->command_count) {
                uint64_t leading = layout->work[best].vaddr -
                                   layout->script_outputs[owner].start;

                if (desired_load_file != SIZE_MAX ||
                    desired_same_page_file != SIZE_MAX) {
                    /* This output is already inside a PT_LOAD whose first
                     * fragment fixed an affine VMA->file mapping. The
                     * target member's desired offset INCLUDES the output
                     * prefix. The same is true for a consecutive member
                     * on the previous section's load page: its already
                     * computed file offset includes the inter-section gap.
                     * Adding leading again and rounding by max page size
                     * would insert a spurious whole page. */
                    size_t desired = desired_load_file != SIZE_MAX
                                         ? desired_load_file
                                         : desired_same_page_file;
                    if ((desired_load_file != SIZE_MAX &&
                         desired_same_page_file != SIZE_MAX &&
                         desired_load_file != desired_same_page_file) ||
                        leading > desired ||
                        desired %
                            (section->align == 0U ? 1U :
                             (size_t)section->align) != 0U) {
                        fprintf(state->diagnostics,
                                "minic-ld: invalid-script-affine-output-prefix:%s\n",
                                section->name);
                        goto done;
                    }
                    layout->script_outputs[owner].file_base =
                        desired - (size_t)leading;
                    file_cursor = desired;
                } else {
                /* Later members may be more strictly aligned than the
                 * first. Match the owner's maximum alignment modulo its
                 * VMA now, so later VMA gaps also have correct file gaps.
                 * Each scripted PT_LOAD is mmap'ed at host page granularity:
                 * a new output section must additionally satisfy
                 * p_offset % page_size == p_vaddr % page_size even when
                 * its ELF sh_addralign is only 1 or 8.  A shared vDSO script
                 * subsequently installs its explicit FILEHDR/PHDRS offsets. */
                size_t alignment = (size_t)(owner_max_align[owner] == 0U
                                               ? 1U : owner_max_align[owner]);
                if (!state->nmagic &&
                    alignment < minild_static_page_size(state)) {
                    alignment = minild_static_page_size(state);
                }
                size_t wanted = (size_t)(layout->work[best].vaddr % alignment);
                size_t have;
                size_t minimum;
                if (leading > SIZE_MAX - file_cursor) {
                    fprintf(state->diagnostics,
                            "minic-ld: script-output-prefix-overflow:%s\n",
                            section->name);
                    goto done;
                }
                minimum = file_cursor + (size_t)leading;
                if (minimum > aligned) {
                    file_cursor = minimum;
                }
                have = file_cursor % alignment;
                size_t padding = wanted >= have ? wanted - have
                                                : alignment - (have - wanted);

                if (!add_size(file_cursor, padding, &file_cursor)) {
                    fprintf(state->diagnostics,
                            "minic-ld: script-output-alignment-overflow:%s\n",
                            section->name);
                    goto done;
                }
                layout->script_outputs[owner].file_base =
                    file_cursor - (size_t)leading;
                }
            }
        }
        layout->work[best].file_offset = file_cursor;
        if (load < script->phdr_count) {
            if (load_first_file[load] == SIZE_MAX) {
                load_first_file[load] = file_cursor;
                load_first_vaddr[load] = layout->work[best].vaddr;
            } else if (file_cursor != desired_load_file) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-script-PHDR-file-alignment:%s"
                        ":phdr=%zu:actual=0x%zx:expected=0x%zx"
                        ":vaddr=0x%llx:owner=%zu\n",
                        section->name, load, file_cursor,
                        desired_load_file,
                        (unsigned long long)layout->work[best].vaddr,
                        owner);
                goto done;
            }
        }
        if (section->type != SHT_NOBITS &&
            !add_size(file_cursor, section->size, &file_cursor)) {
            fprintf(state->diagnostics,
                    "minic-ld: linker-script-file-layout-overflow:%s\n",
                    section->name);
            goto done;
        }
        /* A mixed PROGBITS/NOBITS script output is itself PROGBITS. Reserve
         * file bytes through the trailing BSS even when its last member has
         * no payload; the next output must not reuse that zero-filled span.
         * Pure NOBITS outputs still consume no file bytes. */
        if (owner != SIZE_MAX && owner < script->command_count &&
            --owner_remaining[owner] == 0U && owner_file_backed[owner]) {
            uint64_t span = layout->script_outputs[owner].end -
                            layout->script_outputs[owner].start;
            size_t end;
            if (span > SIZE_MAX - layout->script_outputs[owner].file_base) {
                fprintf(state->diagnostics,
                        "minic-ld: script-output-tail-overflow:%s\n",
                        section->name);
                goto done;
            }
            end = layout->script_outputs[owner].file_base + (size_t)span;
            if (end > file_cursor) {
                file_cursor = end;
            }
        }
        if (section->type != SHT_NOBITS) {
            if (!continues_implicit_load) {
                implicit_load_writable =
                    (section->flags & SHF_WRITE) != 0U;
            } else if ((section->flags & SHF_WRITE) != 0U) {
                implicit_load_writable = true;
            }
            last_file_backed = best;
        }
    }

    layout->scripted = true;
    layout->script_command_count = script->command_count;
    ok = true;

done:
    free(file_order);
    free(owner_max_align);
    free(owner_remaining);
    free(owner_file_backed);
    free(command_load);
    free(load_first_vaddr);
    free(load_first_file);
    free(command_deferred);
    free(command_deferred_dot);
    free(command_deferred_owner);
    free(script_state.deferred_outputs);
    free(script_state.symbol_provenance);
    free(script_state.deferred_bytes);
    free(script_state.section_placed);
    if (!ok) {
        free(layout->work);
        free(layout->script_outputs);
        free(layout->script_bytes);
        memset(layout, 0, sizeof(*layout));
    }
    return ok;
}
