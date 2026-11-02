#include "minias_elf_object_view.h"
#include "minielf.h"

#include <elf.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void elf_object_error(MiniAsElfObjectView *object,const char *format,...) {
    va_list ap;
    if(object->error[0]!='\0')return;
    va_start(ap,format);
    (void)vsnprintf(object->error,sizeof(object->error),format,ap);
    va_end(ap);
}

static const MiniAsSymbol *elf_find_symbol(const MiniAsElfObjectView *object,
                                           const char *name) {
    return object->model.find_symbol(object->model.reader,name);
}

/* Generic ELF STT_SECTION relocation for a target-selected, defined local
 * reference. GNU uses section+addend for eligible absolute relocations even
 * when source and destination are in the same section. No machine-specific
 * ELF logic lives here. */
static bool section_symbol_fixup(const MiniAsElfObjectView *as,const MiniAsFixup *fixup) {
    const MiniAsSymbol *sym;
    MiniAsSymbolView target_symbol;
    bool requested=fixup->prefer_section_symbol ||
        as->policy.fold_defined_local_relocation_to_section ||
        (as->policy.fold_local_pcrel_relocation_to_section &&
         fixup->origin==MINIAS_FIXUP_ORIGIN_FRAGMENT &&
         (fixup->kind==MINIAS_FIXUP_PREL32 ||
          fixup->kind==MINIAS_FIXUP_PREL64)) ||
        (as->policy.fold_synthetic_local_relocation_to_section &&
         fixup->origin==MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION) ||
        (as->policy.fold_local_abs_relocation_to_section &&
         fixup->kind==MINIAS_FIXUP_ABS);
    if(!requested || fixup->symbol_index>=as->model.symbol_count)
        return false;
    sym=&as->model.symbols[fixup->symbol_index];
    return sym->defined && sym->bind==MINIAS_STB_LOCAL &&
           sym->section>=0 && (size_t)sym->section<as->model.section_count &&
           (as->policy.allow_local_section_symbol_fixup==NULL ||
            (minias_project_symbol(sym,&target_symbol) &&
             as->policy.allow_local_section_symbol_fixup(fixup,&target_symbol)));
}

/* Symbol-table membership is an ELF ObjectModel decision, not a Target
 * encoding decision. Derive it once from the finalized fixup and COMDAT
 * graph: repeated scans for every local label were quadratic on genuine
 * compiler-generated Kbuild input with many temporary symbols. A relocation
 * whose symbol is folded to STT_SECTION does not keep its original .L label
 * alive, but a SHT_GROUP signature always does. */
typedef struct MiniAsELFSymbolState {
    bool referenced_by_relocation;
    bool group_signature;
    bool emit;
} MiniAsELFSymbolState;

/* Resolve the two kinds of references once before assigning ELF symbol
 * ordinals. A Target's STT_SECTION folding decision controls both whether
 * the original local SymbolID survives in .symtab and how the relocation is
 * serialized; recomputing it in those two passes risks divergent results.
 * Likewise, the group signature selected for symbol liveness is the exact
 * SymbolID later written into the SHT_GROUP reference. */
typedef struct MiniAsELFFixupPlan {
    bool use_section_symbol;
} MiniAsELFFixupPlan;

static bool plan_elf_symbols(MiniAsElfObjectView *as,MiniAsELFSymbolState *states,
                             MiniAsELFFixupPlan *relocation_plan,
                             const MiniAsObjectPlan *model_plan,
                             size_t *emitted_count) {
    size_t i;
    size_t count=0U;
    const size_t fixup_count=as->model.fixup_count;
    for(i=0U;i<as->model.section_count;++i) {
        const size_t id=model_plan->sections[i].signature_symbol;
        if(id==SIZE_MAX)continue;
        if(id>=as->model.symbol_count) {
            elf_object_error(as,"invalid-group-signature:%s",
                             as->model.sections[i].name);
            return false;
        }
        states[id].group_signature=true;
    }
    for(i=0U;i<fixup_count;++i) {
        const MiniAsFixup *fixup=&as->model.fixups[i];
        relocation_plan[i].use_section_symbol=section_symbol_fixup(as,fixup);
        if(fixup->symbol_index<as->model.symbol_count &&
           !relocation_plan[i].use_section_symbol)
            states[fixup->symbol_index].referenced_by_relocation=true;
    }
    for(i=0U;i<as->model.symbol_count;++i) {
        const MiniAsSymbol *symbol=&as->model.symbols[i];
        const bool temporary=symbol->bind==MINIAS_STB_LOCAL &&
            symbol->defined &&
            (strncmp(symbol->name,".L",2U)==0 ||
             minias_is_private_symbol_name(symbol->name));
        states[i].emit=!temporary || states[i].group_signature ||
            states[i].referenced_by_relocation;
        if(states[i].emit) {
            if(count==SIZE_MAX) {
                elf_object_error(as,"too-many-elf-symbols");
                return false;
            }
            ++count;
        }
    }
    *emitted_count=count;
    return true;
}

static bool minias_write_elf(MiniAsElfObjectView *as,
                             const char *path,
                             unsigned char elf_class) {
    const size_t fixup_count=as->model.fixup_count;
    MiniElfWriteSection *sections = NULL;
    MiniElfWriteSymbol *symbols = NULL;
    MiniElfWriteRela *relocations = NULL;
    size_t *symbol_map = NULL;
    MiniAsObjectPlan object_plan={0};
    MiniAsELFSymbolState *symbol_states = NULL;
    MiniAsELFFixupPlan *relocation_plan = NULL;
    char **private_names = NULL;
    unsigned char *image = NULL;
    size_t image_size = 0U;
    MiniElfWriteError write_error = MINIELF_WRITE_OK;
    MiniElfRelocatableSpec spec;
    size_t output_symbol_count = 0U;
    size_t expected_symbol_count;
    size_t section_count;
    MiniAsObjectAttribute attribute;
    bool emit_attribute;
    size_t i;
    size_t private_suffix=0U;
    bool ok = false;

    if(!minias_object_plan(&as->model,&object_plan,
                           as->error,sizeof(as->error)))return false;
    if(as->model.symbol_count>SIZE_MAX/sizeof(*symbol_states)) {
        elf_object_error(as,"too-many-elf-symbols");
        goto done;
    }
    symbol_states=calloc(as->model.symbol_count==0U ? 1U : as->model.symbol_count,
                         sizeof(*symbol_states));
    if(symbol_states==NULL) {
        elf_object_error(as,"out-of-memory:elf-symbol-plan");
        goto done;
    }
    if(fixup_count>SIZE_MAX/sizeof(*relocation_plan)) {
        elf_object_error(as,"elf-object-capacity-overflow");
        goto done;
    }
    relocation_plan=calloc(fixup_count==0U?1U:fixup_count,
                           sizeof(*relocation_plan));
    if(relocation_plan==NULL) {
        elf_object_error(as,"out-of-memory:elf-reference-plan");
        goto done;
    }
    if(!plan_elf_symbols(as,symbol_states,relocation_plan,&object_plan,
                         &expected_symbol_count))goto done;
    if(as->model.cu_file_count>SIZE_MAX-expected_symbol_count) {
        elf_object_error(as,"too-many-cu-file-symbols");
        goto done;
    }
    expected_symbol_count+=as->model.cu_file_count;
    memset(&attribute, 0, sizeof(attribute));
    if (as->policy.object_attributes != NULL) {
        as->policy.object_attributes(as->target_abi, elf_class,
                                      &attribute);
    }
    emit_attribute = attribute.name != NULL;
    if (emit_attribute && as->model.find_section(as->model.reader,attribute.name)>=0) {
        elf_object_error(as,"duplicate-target-attribute-section:%s",attribute.name);
        goto done;
    }
    if (as->model.section_count == SIZE_MAX) {
        elf_object_error(as, "too-many-elf-sections");
        goto done;
    }
    section_count = as->model.section_count + (emit_attribute ? 1U : 0U);
    /* The ObjectModel contains independently sized vectors. Guard every
     * multiplication before allocating: an overflowing plain malloc for
     * symbol_map would otherwise create an undersized ordinal map even
     * though the surrounding calloc arrays reject their own overflow. */
    if(section_count>SIZE_MAX/sizeof(*sections) ||
       expected_symbol_count>SIZE_MAX/sizeof(*symbols) ||
       fixup_count>SIZE_MAX/sizeof(*relocations) ||
       as->model.symbol_count>SIZE_MAX/sizeof(*symbol_map) ||
       as->model.symbol_count>SIZE_MAX/sizeof(*private_names)) {
        elf_object_error(as,"elf-object-capacity-overflow");
        goto done;
    }
    sections = calloc(section_count == 0U ? 1U : section_count,
                      sizeof(*sections));
    symbols = calloc(expected_symbol_count == 0U ? 1U : expected_symbol_count,
                     sizeof(*symbols));
    relocations = calloc(fixup_count == 0U ? 1U : fixup_count,
                         sizeof(*relocations));
    symbol_map = malloc((as->model.symbol_count == 0U ? 1U : as->model.symbol_count) *
                        sizeof(*symbol_map));
    private_names = calloc(as->model.symbol_count == 0U ? 1U : as->model.symbol_count,
                           sizeof(*private_names));
    if (sections == NULL || symbols == NULL || relocations == NULL ||
        symbol_map == NULL || private_names == NULL) {
        elf_object_error(as, "out-of-memory:elf");
        goto done;
    }
    for (i = 0U; i < as->model.symbol_count; ++i) {
        symbol_map[i] = SIZE_MAX;
    }
    /* STT_FILE is a real ELF local symbol with absolute section identity.
     * Keep it outside the user-label hash and the relocation SymbolID map;
     * duplicate source filenames must never alias a program label. */
    for(i=0U;i<as->model.cu_file_count;++i) {
        MiniElfWriteSymbol *output=&symbols[output_symbol_count++];
        output->name=as->model.cu_file(as->model.reader,i);
        output->info=(unsigned char)((MINIAS_STB_LOCAL<<4U)|MINIAS_STT_FILE);
        output->section_index=SHN_ABS;
    }

    for (i = 0U; i < as->model.section_count; ++i) {
        const MiniAsSection *input=&as->model.sections[i];
        const MiniAsObjectSectionPlan *planned=&object_plan.sections[i];
        MiniElfWriteSection *output = &sections[i];

        output->name = input->name;
        output->type = input->type;
        output->has_group = input->group_signature != NULL;
        output->group_flags = input->group_flags;
        output->flags = input->flags;
        if(planned->linked_section!=SIZE_MAX) {
            output->has_link_order_target=true;
            output->linked_target_section=planned->linked_section;
        }
        output->alignment = input->align == 0U ? 1U : input->align;
        output->entry_size = input->entry_size;
        output->data=planned->bytes;
        output->size=planned->size;
    }

    if (emit_attribute) {
        MiniElfWriteSection *attr = &sections[as->model.section_count];
        attr->name = attribute.name;
        attr->type = attribute.type;
        attr->flags = 0U;
        attr->alignment = 1U;
        attr->entry_size = 0U;
        attr->data = attribute.data;
        attr->size = attribute.size;
    }

    for (i = 0U; i < as->model.symbol_count; ++i) {
        const MiniAsSymbol *input = &as->model.symbols[i];
        MiniElfWriteSymbol *output;
        bool local_group_signature=false;

        if (!symbol_states[i].emit) {
            continue;
        }
        local_group_signature=input->bind==MINIAS_STB_LOCAL &&
            !input->defined && symbol_states[i].group_signature;
        if (input->bind == MINIAS_STB_LOCAL && !input->defined &&
            !local_group_signature) {
            elf_object_error(as,
                             "undefined-local-symbol:%s",
                             input->name);
            goto done;
        }
        if (output_symbol_count >= expected_symbol_count) {
            elf_object_error(as, "internal:symbol-count");
            goto done;
        }
        symbol_map[i] = output_symbol_count;
        output = &symbols[output_symbol_count++];
        if(minias_is_private_symbol_name(input->name)) {
            char visible[96];
            int n;
            /* Source labels cannot address the private SymbolID; pick an
             * ELF spelling absent from the full user symbol table. */
            do {
                if(private_suffix==SIZE_MAX) {
                    elf_object_error(as,"private-symbol-name-overflow");
                    goto done;
                }
                n=snprintf(visible,sizeof(visible),
                           ".Lminias_private_%zu",private_suffix++);
                if(n<0 || (size_t)n>=sizeof(visible)) {
                    elf_object_error(as,"private-symbol-name-capacity");
                    goto done;
                }
            }while(elf_find_symbol(as,visible)!=NULL);
            private_names[i]=minias_strdup(visible);
            if(private_names[i]==NULL) {
                elf_object_error(as,"out-of-memory:elf-private-symbol");
                goto done;
            }
            output->name=private_names[i];
        }else output->name=input->name;
        output->info =
            (unsigned char)((input->bind << 4U) | (input->type & 0x0fU));
        output->other = input->visibility;
        if (!input->defined) {
            output->section_index = SHN_UNDEF;
            output->group_signature_section=local_group_signature;
        } else if (input->section == MINIAS_SECTION_ABS) {
            output->section_index = SHN_ABS;
        } else if (input->section == MINIAS_SECTION_COMMON) {
            output->section_index = SHN_COMMON;
        } else if (input->section < 0 ||
                   (size_t)input->section >= as->model.section_count ||
                   (size_t)input->section + 1U > UINT16_MAX) {
            elf_object_error(as,
                             "internal:bad-symbol-section:%s",
                             input->name);
            goto done;
        } else {
            output->section_index =
                (uint16_t)((size_t)input->section + 1U);
        }
        output->value = input->defined ? input->value : 0U;
        output->size = input->size;
    }
    if (output_symbol_count != expected_symbol_count) {
        elf_object_error(as, "internal:symbol-count");
        goto done;
    }
    for(i=0U;i<as->model.section_count;++i) {
        const MiniAsSection *sec=&as->model.sections[i];
        const size_t idx=object_plan.sections[i].signature_symbol;
        if(idx!=SIZE_MAX) {
            if(idx>=as->model.symbol_count || symbol_map[idx]==SIZE_MAX) {
                elf_object_error(as,"invalid-group-signature:%s",sec->name);
                goto done;
            }
            sections[i].group_signature_symbol=symbol_map[idx];
        }
    }

    for (i = 0U; i < fixup_count; ++i) {
        const MiniAsFixup *input = &as->model.fixups[i];
        MiniElfWriteRela *output = &relocations[i];
        const uint64_t final_offset=object_plan.fixups[i].final_offset;
        const bool section_reloc=relocation_plan[i].use_section_symbol;
        if (input->symbol_index != SIZE_MAX && !section_reloc &&
            symbol_map[input->symbol_index] == SIZE_MAX) {
            elf_object_error(as, "internal:bad-relocation-symbol");
            goto done;
        }
        /* The encoded ELF relocation number is an opaque Target value,
         * not a predicate. R_*_NONE may legitimately be zero; only the
         * semantic capability contract can reject an unsupported fixup.
         * A raw NONE request is never allowed to masquerade as an actual
         * relocation, regardless of a future target's numeric mapping. */
        if (input->kind == MINIAS_FIXUP_NONE ||
            as->policy.supports_fixup==NULL ||
            !as->policy.supports_fixup(as->target_abi,input->kind,input->width)) {
            elf_object_error(as, "unsupported-elf-fixup:kind=%u",
                             (unsigned)input->kind);
            goto done;
        }
        output->target_section = (size_t)input->section;
        output->offset = final_offset;
        output->symbol_index = input->symbol_index == SIZE_MAX ? SIZE_MAX :
                               symbol_map[input->symbol_index];
        output->type = as->policy.fixup_relocation_type(
            as->target_abi,input->kind,input->width);
        output->field_width = input->width;
        output->addend = input->addend;
        if(section_reloc) {
            const MiniAsSymbol *local=&as->model.symbols[input->symbol_index];
            if(local->value>(uint64_t)INT64_MAX ||
               (input->addend>0 &&
                (uint64_t)input->addend>(uint64_t)INT64_MAX-local->value)) {
                elf_object_error(as,"section-fixup-addend-overflow:%s",
                                 local->name);
                goto done;
            }
            output->use_section_symbol=true;
            output->section_symbol_index=(size_t)local->section;
            output->addend=(int64_t)local->value+input->addend;
        }
    }

    memset(&spec, 0, sizeof(spec));
    spec.elf_class = elf_class;
    spec.data_encoding = ELFDATA2LSB;
    /* GNU symbol-type and binding extensions require EI_OSABI=GNU so
     * external readelf/ld classify type 10 as IFUNC and bind 10 UNIQUE,
     * rather than unknown OS-specific symbol types. Derive from emitted
     * symbols; ordinary RV/other ELF objects retain System V. */
    spec.osabi = ELFOSABI_NONE;
    for (i = 0U; i < output_symbol_count; ++i) {
        if ((symbols[i].info & 0x0fU) == MINIAS_STT_GNU_IFUNC ||
            (symbols[i].info >> 4U) == MINIAS_STB_GNU_UNIQUE) {
            spec.osabi = ELFOSABI_GNU;
            break;
        }
    }
    spec.machine = as->object_machine != 0U ?
                   as->object_machine : as->policy.elf_machine;
    spec.relocation_format = as->output_rel_relocations ?
                             MINIELF_RELOC_FORMAT_REL :
                             MINIELF_RELOC_FORMAT_RELA;
    spec.flags = as->elf_flags;
    spec.sections = sections;
    spec.section_count = section_count;
    spec.symbols = symbols;
    spec.symbol_count = output_symbol_count;
    spec.relocations = relocations;
    spec.relocation_count = fixup_count;
    spec.emit_section_symbols = true;
    spec.emit_all_section_symbols = as->policy.emit_all_section_symbols;

    if (!minielf_build_relocatable(&spec,
                                   &image,
                                   &image_size,
                                   &write_error)) {
        elf_object_error(as,
                         "elf-write:%s",
                         minielf_write_error_string(write_error));
        goto done;
    }

    ok=minias_object_publish_image(path,image,image_size,
                                   as->error,sizeof(as->error));

done:
    if(private_names!=NULL) {
        for(i=0U;i<as->model.symbol_count;++i)free(private_names[i]);
    }
    free(private_names);
    free(image);
    free(symbol_map);
    minias_object_dispose_plan(&object_plan);
    free(symbol_states);
    free(relocation_plan);
    free(relocations);
    free(symbols);
    free(sections);
    return ok;
}

bool minias_write_elf64(MiniAsElfObjectView *as, const char *path) {
    return minias_write_elf(as, path, ELFCLASS64);
}

bool minias_write_elf32(MiniAsElfObjectView *as, const char *path) {
    return minias_write_elf(as, path, ELFCLASS32);
}
