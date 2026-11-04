typedef struct MiniLdScriptSymbolProvenance {
    size_t resolver_index;
    size_t command_index;
    size_t output_command;
    uint64_t value;
    bool active;
} MiniLdScriptSymbolProvenance;

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
} MiniLdScriptLayoutState;

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

static bool script_layout_symbol_value(void *context,
                                       const char *name,
                                       uint64_t *value_out) {
    MiniLdScriptLayoutState *script_state = context;
    size_t index = symbol_resolver_lookup(&script_state->state->resolver, name);
    MiniLdResolvedSymbol *symbol;

    if (index == SIZE_MAX) {
        return false;
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
        return false;
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
    context.user = script_state;
    return minild_script_evaluate(script_state->script,
                                  expression,
                                  &context,
                                  value_out,
                                  script_state->state->diagnostics);
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
        return script_layout_is_literal_expr(script, expr->left, depth + 1U);
    case MINILD_SCRIPT_EXPR_ADD:
    case MINILD_SCRIPT_EXPR_SUBTRACT:
    case MINILD_SCRIPT_EXPR_MULTIPLY:
    case MINILD_SCRIPT_EXPR_DIVIDE:
    case MINILD_SCRIPT_EXPR_SHIFT_LEFT:
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

static bool script_layout_define_top_symbol(
    MiniLdScriptLayoutState *script_state, size_t command_index,
    const char *name, MiniLdScriptExprId expr_id, uint64_t value) {
    MiniLdScriptSymbolProvenance *entry;
    size_t index;

    if (!script_layout_define_symbol(script_state, name, value)) {
        return false;
    }
    if (!script_state->have_headers_size ||
        !script_layout_is_dot_relative_expr(script_state->script, expr_id,
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

/* Register future shared-script definitions in the ONE canonical resolver
 * before dynsym/PLT/GOT sizing.  This pass deliberately does not evaluate
 * expressions: the value of `.` and section membership belong to layout.
 * The synthetic slot is a real UND placeholder, not an invented ABS zero.
 * PROVIDE is active only for an original, unresolved input reference;
 * artificially reserved forced names must not trigger it. */
static bool minild_script_declare_shared_name(MiniLdState *state,
                                               const char *name,
                                               bool provide,
                                               bool provably_absolute) {
    size_t index = symbol_resolver_lookup(&state->resolver, name);

    if (provide) {
        if (index != SIZE_MAX &&
            state->resolver.entries[index].section == MINILD_SECTION_UNDEF &&
            !state->resolver.entries[index].script_placeholder) {
            if (!state->resolver.entries[index].script_planned_defined) {
                state->resolver.entries[index].script_planned_absolute =
                    provably_absolute;
            } else {
                state->resolver.entries[index].script_planned_absolute &=
                    provably_absolute;
            }
            state->resolver.entries[index].script_planned_defined = true;
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
                    state,
                    command->value.symbol.name,
                    command->value.symbol.provide,
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
                    state, item->value.symbol.name, false,
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
    uint64_t aligned;

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

static bool script_layout_bind_output_symbols(
    MiniLdScriptLayoutState *script_state,
    const MiniLdScriptOutputSection *output,
    size_t command_index) {
    size_t anchor = script_layout_output_anchor(script_state, command_index);
    uint64_t anchor_vaddr;
    size_t i;

    if (anchor == SIZE_MAX) {
        return true;
    }
    anchor_vaddr = script_state->layout->work[anchor].vaddr;

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
            fprintf(script_state->state->diagnostics,
                    "minic-ld: linker-script-missing-defined-symbol:%s\n",
                    item->value.symbol.name);
            return false;
        }
        symbol = &script_state->state->resolver.entries[symbol_index];
        if (symbol->section != MINILD_SECTION_ABS ||
            symbol->value < anchor_vaddr) {
            continue;
        }
        symbol->section = (int)anchor;
        symbol->value -= anchor_vaddr;
    }
    return true;
}

static bool script_layout_process_output(MiniLdScriptLayoutState *script_state,
                                         const MiniLdScriptOutputSection *output,
                                         size_t command_index) {
    uint64_t output_start;
    uint64_t lma_start;
    size_t byte_start = script_state->layout->script_byte_count;
    size_t i;

    script_state->layout->script_outputs[command_index].name = output->name;

    if (output->fill != MINILD_SCRIPT_EXPR_NONE) {
        uint64_t fill;

        if (!script_layout_evaluate(script_state, output->fill, &fill)) {
            return false;
        }
        if (fill > UINT8_MAX) {
            fprintf(script_state->state->diagnostics,
                    "minic-ld: unsupported-script-fill-pattern:%s\n",
                    output->name);
            return false;
        }
        script_state->layout->script_outputs[command_index].fill =
            (unsigned char)fill;
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
            if (!script_layout_evaluate(script_state,
                                        item->value.symbol.expression,
                                        &value) ||
                !script_layout_define_symbol(script_state,
                                             item->value.symbol.name,
                                             value)) {
                return false;
            }
            break;
        case MINILD_SCRIPT_SECTION_SET_DOT:
            if (!script_layout_evaluate(script_state,
                                        item->value.expression,
                                        &value)) {
                return false;
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
            if (!script_layout_evaluate(script_state,
                                        item->value.expression,
                                        &value)) {
                return false;
            }
            if (script_state->dot == UINT64_MAX) {
                fprintf(script_state->state->diagnostics,
                        "minic-ld: linker-script-BYTE-overflow:%s\n",
                        output->name);
                return false;
            }
            {
                MiniLdLayout *layout = script_state->layout;
                if (layout->script_byte_count == layout->script_byte_capacity) {
                    size_t capacity = layout->script_byte_capacity == 0U
                                          ? 16U : layout->script_byte_capacity * 2U;
                    MiniLdScriptBytePatch *patches;
                    if (capacity < layout->script_byte_capacity ||
                        capacity > SIZE_MAX / sizeof(*patches)) {
                        fprintf(script_state->state->diagnostics,
                                "minic-ld: script-BYTE-count-overflow\n");
                        return false;
                    }
                    patches = realloc(layout->script_bytes,
                                      capacity * sizeof(*patches));
                    if (patches == NULL) {
                        fprintf(script_state->state->diagnostics,
                                "minic-ld: out-of-memory:script-BYTE\n");
                        return false;
                    }
                    layout->script_bytes = patches;
                    layout->script_byte_capacity = capacity;
                }
                layout->script_bytes[layout->script_byte_count].command =
                    command_index;
                layout->script_bytes[layout->script_byte_count].address =
                    script_state->dot;
                layout->script_bytes[layout->script_byte_count].value =
                    (unsigned char)value;
                ++layout->script_byte_count;
            }
            ++script_state->dot;
            break;
        case MINILD_SCRIPT_SECTION_CONSTRUCTORS:
            break;
        }
    }

    script_state->layout->script_outputs[command_index].end =
        script_state->dot;

    /* A pure BYTE output needs a working section to anchor its layout.
     * Mixed input/BYTE outputs instead keep their real work fragments and
     * materialize byte members at their output-relative file addresses. */
    if (script_state->layout->script_byte_count != byte_start) {
        bool has_input = false;

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
                                     SHT_PROGBITS, SHF_ALLOC, 1U, 0U,
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
            if (length != 0U &&
                script_state->layout->script_outputs[command_index].have_fill) {
                memset(section->data,
                       script_state->layout->script_outputs[command_index].fill,
                       length);
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
    if (script_layout_output_anchor(script_state, command_index) != SIZE_MAX) {
        script_state->dot_output_command = command_index;
    }
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
        size_t j;

        if (!binding->active) {
            continue;
        }
        if (owner == SIZE_MAX) {
            /* Before the first section, or after an explicit dot jump,
             * GNU associates a matching output-section start with `.`.
             * A different placement cannot be inferred safely. */
            for (j = binding->command_index + 1U;
                 j < script_state->script->command_count; ++j) {
                const MiniLdScriptCommand *cmd =
                    &script_state->script->commands[j];

                if (cmd->kind == MINILD_SCRIPT_OUTPUT_SECTION &&
                    script_state->layout->script_outputs[j].defined &&
                    script_state->layout->script_outputs[j].start ==
                        binding->value &&
                    script_layout_output_anchor(script_state, j) != SIZE_MAX) {
                    owner = j;
                    break;
                }
            }
        }
        anchor = owner == SIZE_MAX ? SIZE_MAX :
                     script_layout_output_anchor(script_state, owner);
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
        symbol->section = (int)anchor;
        symbol->value = binding->value -
                        script_state->layout->work[anchor].vaddr;
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
    size_t i;
    size_t alloc_count = 0U;
    size_t last_file_backed = SIZE_MAX;
    uint64_t *owner_max_align = NULL;
    size_t *owner_remaining = NULL;
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
    owner_max_align = calloc(script->command_count == 0U ? 1U :
                              script->command_count, sizeof(*owner_max_align));
    owner_remaining = calloc(script->command_count == 0U ? 1U :
                              script->command_count, sizeof(*owner_remaining));

    if (layout->work == NULL || layout->script_outputs == NULL ||
        script_state.section_placed == NULL ||
        command_deferred == NULL || owner_max_align == NULL ||
        owner_remaining == NULL) {
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
        case MINILD_SCRIPT_SET_DOT:
            if (!script_layout_evaluate(&script_state,
                                        command->value.expression,
                                        &value)) {
                goto done;
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
                if (sym == SIZE_MAX ||
                    state->resolver.entries[sym].section != MINILD_SECTION_UNDEF ||
                    state->resolver.entries[sym].script_placeholder) {
                    break;
                }
            }
            if (script->expressions[command->value.symbol.expression].kind ==
                    MINILD_SCRIPT_EXPR_SYMBOL &&
                !script_layout_symbol_value(
                    &script_state,
                    script->expressions[command->value.symbol.expression].name,
                    &value)) {
                command_deferred[i] = true;
                break;
            }
            if (!script_layout_evaluate(&script_state,
                                        command->value.symbol.expression,
                                        &value) ||
                !script_layout_define_top_symbol(
                    &script_state, i, command->value.symbol.name,
                    command->value.symbol.expression, value)) {
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

    for (i = 0U; i < script->command_count; ++i) {
        const MiniLdScriptCommand *command = &script->commands[i];
        uint64_t value;

        if (command->kind != MINILD_SCRIPT_DEFINE_SYMBOL ||
            !command_deferred[i]) {
            continue;
        }
        if (command->value.symbol.provide) {
            size_t sym = symbol_resolver_lookup(&state->resolver,
                                            command->value.symbol.name);
            if (sym == SIZE_MAX ||
                state->resolver.entries[sym].section != MINILD_SECTION_UNDEF ||
                state->resolver.entries[sym].script_placeholder) {
                continue;
            }
        }
        if (!script_layout_evaluate(&script_state,
                                    command->value.symbol.expression,
                                    &value) ||
            !script_layout_define_top_symbol(
                &script_state, i, command->value.symbol.name,
                command->value.symbol.expression, value)) {
            goto done;
        }
    }

    if (!script_layout_finalize_top_symbol_provenance(&script_state)) {
        goto done;
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
        }
        ++alloc_count;
        if ((section->flags & SHF_WRITE) != 0U) {
            layout->have_rw = true;
        } else {
            layout->have_rx = true;
        }
    }

    {
        MiniLdElfAbiSizes abi =
            minild_elf_abi_sizes(state->target->elf_class);

        if (abi.phdr == 0U ||
            alloc_count > (SIZE_MAX - abi.ehdr) / abi.phdr ||
            !align_up_size(abi.ehdr + alloc_count * abi.phdr,
                           state->nmagic ? 1U :
                           state->target->page_size, &header_size)) {
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
        size_t aligned;

        /* A pair of output sections may occupy the same ELF load page even
         * though their names/flags differ.  Preserve the VMA/LMA distance in
         * their file positions: otherwise no single PT_LOAD can describe
         * both without mapping unrelated file bytes over the first section.
         * Do this before choosing a new output-section file base.  Members
         * of an existing output already get exact dot-hole offsets below. */
        if (!state->nmagic && state->target->page_size != 0U &&
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
                if (previous_end != 0U &&
                    previous_end <= this_place->vaddr &&
                    (previous_end - 1U) / state->target->page_size ==
                        this_place->vaddr / state->target->page_size &&
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
                    file_cursor = desired;
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
                    alignment < state->target->page_size) {
                    alignment = state->target->page_size;
                }
                uint64_t leading = layout->work[best].vaddr -
                                   layout->script_outputs[owner].start;
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
        layout->work[best].file_offset = file_cursor;
        if (section->type != SHT_NOBITS &&
            !add_size(file_cursor, section->size, &file_cursor)) {
            fprintf(state->diagnostics,
                    "minic-ld: linker-script-file-layout-overflow:%s\n",
                    section->name);
            goto done;
        }
        if (owner != SIZE_MAX && owner < script->command_count &&
            --owner_remaining[owner] == 0U && section->type != SHT_NOBITS) {
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
    free(command_deferred);
    free(script_state.symbol_provenance);
    free(script_state.section_placed);
    if (!ok) {
        free(layout->work);
        free(layout->script_outputs);
        free(layout->script_bytes);
        memset(layout, 0, sizeof(*layout));
    }
    return ok;
}
