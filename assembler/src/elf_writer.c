#include "minias_internal.h"
#include "minielf.h"

#include <elf.h>
#include <stdlib.h>
#include <string.h>

/* Generic ELF STT_SECTION relocation for a target-selected, defined local
 * reference. GNU uses section+addend for eligible absolute relocations even
 * when source and destination are in the same section. No machine-specific
 * ELF logic lives here. */
static bool section_symbol_fixup(const MiniAs *as,const MiniAsFixup *fixup) {
    const MiniAsSymbol *sym;
    bool requested=fixup->prefer_section_symbol ||
        (as->target->fold_local_abs_relocation_to_section &&
         fixup->kind==MINIAS_FIXUP_ABS);
    if(!requested || fixup->symbol_index>=as->symbol_count)
        return false;
    sym=&as->symbols[fixup->symbol_index];
    return sym->defined && sym->bind==MINIAS_STB_LOCAL &&
           sym->section>=0 && (size_t)sym->section<as->section_count;
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

/* A LINK_ORDER reference can name a section instead of an anchor symbol.
 * Linux objects may contain thousands of unique named sections. Resolving
 * every metadata section by scanning every input section becomes quadratic
 * even after the symbol/relocation liveness plan has become linear. Keep a
 * single name-to-SectionID index for this ELF serialization transaction;
 * sorting by input SectionID preserves GNU's first-match behavior and the
 * existing special choice of a same-COMDAT member on duplicate names. */
typedef struct MiniAsELFSectionName {
    const char *name;
    size_t section_id;
} MiniAsELFSectionName;

static int compare_elf_section_name(const void *lhs,const void *rhs) {
    const MiniAsELFSectionName *a=lhs,*b=rhs;
    int cmp=strcmp(a->name,b->name);
    if(cmp!=0)return cmp;
    return a->section_id<b->section_id?-1:
           a->section_id>b->section_id?1:0;
}

static size_t first_named_elf_section(const MiniAsELFSectionName *index,
                                      size_t count,const char *name) {
    size_t lo=0U,hi=count;
    while(lo<hi) {
        size_t mid=lo+(hi-lo)/2U;
        if(strcmp(index[mid].name,name)<0)lo=mid+1U;
        else hi=mid;
    }
    return lo;
}

static bool plan_elf_symbols(MiniAs *as,MiniAsELFSymbolState *states,
                             size_t *emitted_count) {
    size_t i;
    size_t count=0U;
    for(i=0U;i<as->section_count;++i) {
        const char *signature=as->sections[i].group_signature;
        const MiniAsSymbol *symbol;
        if(signature==NULL)continue;
        symbol=minias_get_symbol(as,signature,false);
        if(symbol==NULL) {
            minias_set_error(as,"missing-group-signature:%s",
                             as->sections[i].name);
            return false;
        }
        states[(size_t)(symbol-as->symbols)].group_signature=true;
    }
    for(i=0U;i<as->fixup_count;++i) {
        const MiniAsFixup *fixup=&as->fixups[i];
        if(fixup->symbol_index<as->symbol_count &&
           !section_symbol_fixup(as,fixup))
            states[fixup->symbol_index].referenced_by_relocation=true;
    }
    for(i=0U;i<as->symbol_count;++i) {
        const MiniAsSymbol *symbol=&as->symbols[i];
        const bool temporary=symbol->bind==MINIAS_STB_LOCAL &&
            symbol->defined &&
            (strncmp(symbol->name,".L",2U)==0 ||
             minias_is_private_symbol_name(symbol->name));
        states[i].emit=!temporary || states[i].group_signature ||
            states[i].referenced_by_relocation;
        if(states[i].emit) {
            if(count==SIZE_MAX) {
                minias_set_error(as,"too-many-elf-symbols");
                return false;
            }
            ++count;
        }
    }
    *emitted_count=count;
    return true;
}

/* GNU's `o' refers to an anchor *symbol*, although the final ELF sh_link
 * holds only the anchor's section-header index.  Resolve after all layout
 * passes: a forward .LPFE label has no section until its definition, and
 * an absolute anchor legitimately gives sh_link=0.  A section's name is
 * also accepted as a reference even if there is no same-named STT_SECTION
 * symbol (e.g. `.text.foo').  Numeric GAS test-only arguments emit zero. */
static bool resolve_link_order_target(MiniAs *as, size_t owner,
                                      const MiniAsELFSectionName *name_index,
                                      size_t name_count,size_t *target) {
    const MiniAsSection *section=&as->sections[owner];
    const char *reference=section->link_order_reference;
    const MiniAsSymbol *symbol;
    uint64_t numeric;
    size_t i,candidate=SIZE_MAX;

    *target=SIZE_MAX;
    if(reference==NULL || *reference=='\0')return true;
    if(minias_parse_u64(reference,&numeric)) {
        /* GNU as accepts section-index syntax and writes SHN_UNDEF when
         * no symbolic association is available.  Preserve that behavior
         * (including a deliberately supplied zero index). */
        return true;
    }
    symbol=minias_get_symbol(as,reference,false);
    if(symbol!=NULL && symbol->defined) {
        if(symbol->section==MINIAS_SECTION_ABS)return true;
        if(symbol->section>=0 &&
           (size_t)symbol->section<as->section_count) {
            *target=(size_t)symbol->section;
            return true;
        }
        minias_set_error(as,"invalid-link-order-target:%s:section=%s",
                         reference,section->name);
        return false;
    }
    /* A section-name reference is valid without defining a homonymous
     * symbol.  Prefer the owner's COMDAT key if its name is duplicated. */
    for(i=first_named_elf_section(name_index,name_count,reference);
        i<name_count && strcmp(name_index[i].name,reference)==0;++i) {
        const size_t section_id=name_index[i].section_id;
        const MiniAsSection *other=&as->sections[section_id];
        if(candidate==SIZE_MAX)candidate=section_id;
        else if(section->group_signature!=NULL &&
                other->group_signature!=NULL &&
                strcmp(section->group_signature,other->group_signature)==0) {
            candidate=section_id;
            break;
        } else if(section->group_signature==NULL) {
            minias_set_error(as,"ambiguous-link-order-section:%s:section=%s",
                             reference,section->name);
            return false;
        }
    }
    if(candidate!=SIZE_MAX) {
        *target=candidate;
        return true;
    }
    minias_set_error(as,"undefined-link-order-reference:%s:section=%s",
                     reference,section->name);
    return false;
}

static bool minias_write_elf(MiniAs *as,
                             const char *path,
                             unsigned char elf_class) {
    MiniElfWriteSection *sections = NULL;
    MiniElfWriteSymbol *symbols = NULL;
    MiniElfWriteRela *relocations = NULL;
    size_t *symbol_map = NULL;
    MiniAsELFSectionName *name_index = NULL;
    MiniAsELFSymbolState *symbol_states = NULL;
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
    FILE *file = NULL;
    bool ok = false;

    if(as->symbol_count>SIZE_MAX/sizeof(*symbol_states)) {
        minias_set_error(as,"too-many-elf-symbols");
        return false;
    }
    symbol_states=calloc(as->symbol_count==0U ? 1U : as->symbol_count,
                         sizeof(*symbol_states));
    if(symbol_states==NULL) {
        minias_set_error(as,"out-of-memory:elf-symbol-plan");
        return false;
    }
    if(!plan_elf_symbols(as,symbol_states,&expected_symbol_count))goto done;
    if(as->cu_file_count>SIZE_MAX-expected_symbol_count) {
        minias_set_error(as,"too-many-cu-file-symbols");
        goto done;
    }
    expected_symbol_count+=as->cu_file_count;
    memset(&attribute, 0, sizeof(attribute));
    if (as->target->object_attributes != NULL) {
        as->target->object_attributes(as, elf_class, &attribute);
    }
    emit_attribute = attribute.name != NULL;
    if (emit_attribute && minias_find_section(as, attribute.name) >= 0) {
        minias_set_error(as, "duplicate-target-attribute-section:%s", attribute.name);
        goto done;
    }
    if (as->section_count == SIZE_MAX) {
        minias_set_error(as, "too-many-elf-sections");
        goto done;
    }
    section_count = as->section_count + (emit_attribute ? 1U : 0U);
    /* The ObjectModel contains independently sized vectors. Guard every
     * multiplication before allocating: an overflowing plain malloc for
     * symbol_map would otherwise create an undersized ordinal map even
     * though the surrounding calloc arrays reject their own overflow. */
    if(section_count>SIZE_MAX/sizeof(*sections) ||
       as->section_count>SIZE_MAX/sizeof(*name_index) ||
       expected_symbol_count>SIZE_MAX/sizeof(*symbols) ||
       as->fixup_count>SIZE_MAX/sizeof(*relocations) ||
       as->symbol_count>SIZE_MAX/sizeof(*symbol_map) ||
       as->symbol_count>SIZE_MAX/sizeof(*private_names)) {
        minias_set_error(as,"elf-object-capacity-overflow");
        goto done;
    }
    sections = calloc(section_count == 0U ? 1U : section_count,
                      sizeof(*sections));
    symbols = calloc(expected_symbol_count == 0U ? 1U : expected_symbol_count,
                     sizeof(*symbols));
    relocations = calloc(as->fixup_count == 0U ? 1U : as->fixup_count,
                         sizeof(*relocations));
    symbol_map = malloc((as->symbol_count == 0U ? 1U : as->symbol_count) *
                        sizeof(*symbol_map));
    name_index = malloc((as->section_count == 0U ? 1U : as->section_count) *
                        sizeof(*name_index));
    private_names = calloc(as->symbol_count == 0U ? 1U : as->symbol_count,
                           sizeof(*private_names));
    if (sections == NULL || symbols == NULL || relocations == NULL ||
        symbol_map == NULL || name_index == NULL || private_names == NULL) {
        minias_set_error(as, "out-of-memory:elf");
        goto done;
    }
    for (i = 0U; i < as->symbol_count; ++i) {
        symbol_map[i] = SIZE_MAX;
    }
    for (i = 0U; i < as->section_count; ++i) {
        if (as->sections[i].name == NULL) {
            minias_set_error(as,"unnamed-elf-section");
            goto done;
        }
        name_index[i].name=as->sections[i].name;
        name_index[i].section_id=i;
    }
    qsort(name_index,as->section_count,sizeof(*name_index),
          compare_elf_section_name);

    /* STT_FILE is a real ELF local symbol with absolute section identity.
     * Keep it outside the user-label hash and the relocation SymbolID map;
     * duplicate source filenames must never alias a program label. */
    for(i=0U;i<as->cu_file_count;++i) {
        MiniElfWriteSymbol *output=&symbols[output_symbol_count++];
        output->name=as->cu_files[i];
        output->info=(unsigned char)((MINIAS_STB_LOCAL<<4U)|MINIAS_STT_FILE);
        output->section_index=SHN_ABS;
    }

    for (i = 0U; i < as->section_count; ++i) {
        MiniAsSection *input = &as->sections[i];
        MiniElfWriteSection *output = &sections[i];

        output->name = input->name;
        output->type = input->type;
        output->has_group = input->group_signature != NULL;
        output->flags = input->flags;
        if((input->flags & MINIAS_SHF_LINK_ORDER)!=0U) {
            size_t linked_target=SIZE_MAX;
            if(!resolve_link_order_target(as,i,name_index,as->section_count,
                                          &linked_target))goto done;
            if(linked_target!=SIZE_MAX) {
                output->has_link_order_target=true;
                output->linked_target_section=linked_target;
            }
        }
        output->alignment = input->align == 0U ? 1U : input->align;
        output->entry_size = input->entry_size;
        output->data = input->data;
        output->size = input->size;
    }

    if (emit_attribute) {
        MiniElfWriteSection *attr = &sections[as->section_count];
        attr->name = attribute.name;
        attr->type = attribute.type;
        attr->flags = 0U;
        attr->alignment = 1U;
        attr->entry_size = 0U;
        attr->data = attribute.data;
        attr->size = attribute.size;
    }

    for (i = 0U; i < as->symbol_count; ++i) {
        MiniAsSymbol *input = &as->symbols[i];
        MiniElfWriteSymbol *output;
        bool local_group_signature=false;

        if (!symbol_states[i].emit) {
            continue;
        }
        local_group_signature=input->bind==MINIAS_STB_LOCAL &&
            !input->defined && symbol_states[i].group_signature;
        if (input->bind == MINIAS_STB_LOCAL && !input->defined &&
            !local_group_signature) {
            minias_set_error(as,
                             "undefined-local-symbol:%s",
                             input->name);
            goto done;
        }
        if (output_symbol_count >= expected_symbol_count) {
            minias_set_error(as, "internal:symbol-count");
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
                    minias_set_error(as,"private-symbol-name-overflow");
                    goto done;
                }
                n=snprintf(visible,sizeof(visible),
                           ".Lminias_private_%zu",private_suffix++);
                if(n<0 || (size_t)n>=sizeof(visible)) {
                    minias_set_error(as,"private-symbol-name-capacity");
                    goto done;
                }
            }while(minias_get_symbol(as,visible,false)!=NULL);
            private_names[i]=minias_strdup(visible);
            if(private_names[i]==NULL) {
                minias_set_error(as,"out-of-memory:elf-private-symbol");
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
                   (size_t)input->section >= as->section_count ||
                   (size_t)input->section + 1U > UINT16_MAX) {
            minias_set_error(as,
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
        minias_set_error(as, "internal:symbol-count");
        goto done;
    }
    for(i=0U;i<as->section_count;++i) {
        const MiniAsSection *sec=&as->sections[i];
        if(sec->group_signature!=NULL) {
            const MiniAsSymbol *sig=minias_get_symbol(as,sec->group_signature,false);
            size_t idx;
            if(sig==NULL) {
                minias_set_error(as,"missing-group-signature:%s",sec->name);
                goto done;
            }
            idx=(size_t)(sig-as->symbols);
            if(idx>=as->symbol_count || symbol_map[idx]==SIZE_MAX) {
                minias_set_error(as,"invalid-group-signature:%s",sec->name);
                goto done;
            }
            sections[i].group_signature_symbol=symbol_map[idx];
        }
    }

    for (i = 0U; i < as->fixup_count; ++i) {
        MiniAsFixup *input = &as->fixups[i];
        MiniElfWriteRela *output = &relocations[i];
        uint64_t final_offset;
        bool section_reloc=section_symbol_fixup(as,input);
        if (input->origin == MINIAS_FIXUP_ORIGIN_FRAGMENT) {
            const MiniAsStmt *owner;
            if (input->fragment_id >= as->stmt_count) {
                minias_set_error(as, "invalid-fixup-fragment-id");
                goto done;
            }
            owner = &as->stmts[input->fragment_id];
            if (owner->section != input->section ||
                input->fragment_offset > owner->size ||
                input->width > owner->size - input->fragment_offset ||
                owner->offset > UINT64_MAX - input->fragment_offset) {
                minias_set_error(as, "invalid-fixup-fragment-layout:line=%zu",
                                 owner->line);
                goto done;
            }
            final_offset = owner->offset + input->fragment_offset;
        } else if (input->origin == MINIAS_FIXUP_ORIGIN_EXPLICIT_SECTION ||
                   input->origin == MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION) {
            final_offset = input->offset;
        } else {
            minias_set_error(as, "invalid-fixup-origin");
            goto done;
        }
        if (input->section < 0 ||
            (size_t)input->section >= as->section_count ||
            input->symbol_index >= as->symbol_count ||
            (!section_reloc && symbol_map[input->symbol_index] == SIZE_MAX)) {
            minias_set_error(as, "internal:bad-relocation-symbol");
            goto done;
        }
        /* Finalization of the target-owned, typed fixup.
         * Check the actual emitted section extent before exposing an ELF
         * relocation; do not allow an out-of-bounds machine-field patch. */
        if (input->width > as->sections[(size_t)input->section].size ||
            final_offset >
            as->sections[(size_t)input->section].size - input->width) {
            minias_set_error(as, "fixup-field-out-of-bounds:kind=%u",
                             (unsigned)input->kind);
            goto done;
        }
        /* The encoded ELF relocation number is an opaque Target value,
         * not a predicate. R_*_NONE may legitimately be zero; only the
         * semantic capability contract can reject an unsupported fixup.
         * A raw NONE request is never allowed to masquerade as an actual
         * relocation, regardless of a future target's numeric mapping. */
        if (input->kind == MINIAS_FIXUP_NONE ||
            as->target->supports_fixup == NULL ||
            !as->target->supports_fixup(input->kind,input->width)) {
            minias_set_error(as, "unsupported-elf-fixup:kind=%u",
                             (unsigned)input->kind);
            goto done;
        }
        output->target_section = (size_t)input->section;
        output->offset = final_offset;
        output->symbol_index = symbol_map[input->symbol_index];
        output->type =
            as->target->fixup_relocation_type(input->kind, input->width);
        output->addend = input->addend;
        if(section_reloc) {
            const MiniAsSymbol *local=&as->symbols[input->symbol_index];
            if(local->value>(uint64_t)INT64_MAX ||
               (input->addend>0 &&
                (uint64_t)input->addend>(uint64_t)INT64_MAX-local->value)) {
                minias_set_error(as,"section-fixup-addend-overflow:%s",
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
    spec.machine = as->target->elf_machine;
    spec.flags = as->elf_flags;
    spec.sections = sections;
    spec.section_count = section_count;
    spec.symbols = symbols;
    spec.symbol_count = output_symbol_count;
    spec.relocations = relocations;
    spec.relocation_count = as->fixup_count;
    spec.emit_section_symbols = true;

    if (!minielf_build_relocatable(&spec,
                                   &image,
                                   &image_size,
                                   &write_error)) {
        minias_set_error(as,
                         "elf-write:%s",
                         minielf_write_error_string(write_error));
        goto done;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        minias_set_error(as, "output-open:%s", path);
        goto done;
    }
    {
        size_t written=fwrite(image,1U,image_size,file);
        int close_status=fclose(file);
        file = NULL;
        if(written!=image_size || close_status!=0) {
            minias_set_error(as,"output-write:%s",path);
            goto done;
        }
    }
    ok = true;

done:
    if(private_names!=NULL) {
        for(i=0U;i<as->symbol_count;++i)free(private_names[i]);
    }
    free(private_names);
    if (file != NULL) {
        (void)fclose(file);
    }
    free(image);
    free(symbol_map);
    free(name_index);
    free(symbol_states);
    free(relocations);
    free(symbols);
    free(sections);
    return ok;
}

bool minias_write_elf64(MiniAs *as, const char *path) {
    return minias_write_elf(as, path, ELFCLASS64);
}

bool minias_write_elf32(MiniAs *as, const char *path) {
    return minias_write_elf(as, path, ELFCLASS32);
}
