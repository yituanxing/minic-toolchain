#include "minias_elf_object_view.h"

/* The valid serializer consumer may borrow identities and callbacks only. */
bool elf_object_view_const_consumer(const MiniAsElfObjectView *view) {
    const MiniAsObjectModelView *model=&view->model;
    const MiniAsSection *section=model->section_count?&model->sections[0]:NULL;
    const MiniAsSymbol *symbol=model->symbol_count?&model->symbols[0]:NULL;
    const MiniAsFixup *fixup=model->fixup_count?&model->fixups[0]:NULL;
    return view->policy->supports_fixup!=NULL && model->section_bytes!=NULL &&
           (section==NULL || section->name!=NULL) &&
           (symbol==NULL || symbol->name!=NULL) &&
           (fixup==NULL || fixup->width!=0U);
}

#if defined(TRY_SECTION_WRITE)
void invalid_section(MiniAsElfObjectView *view) { view->model.sections[0].flags=0U; }
#elif defined(TRY_ELF_POLICY_WRITE)
void invalid_elf_policy(MiniAsElfObjectView *view) { view->policy->elf_machine=0U; }
#elif defined(TRY_LEGACY_TARGET_POLICY)
void invalid_legacy_policy(const MiniAsTargetOps *target) { (void)target->elf_machine; }
#elif defined(TRY_SYMBOL_WRITE)
void invalid_symbol(MiniAsElfObjectView *view) { view->model.symbols[0].bind=0U; }
#elif defined(TRY_FIXUP_WRITE)
void invalid_fixup(MiniAsElfObjectView *view) { view->model.fixups[0].width=4U; }
#elif defined(TRY_FILENAME_WRITE)
void invalid_filename(MiniAsElfObjectView *view) { view->model.cu_file[0]='x'; }
#elif defined(TRY_TARGET_WRITE)
void invalid_target(MiniAsElfObjectView *view) { (void)view->target; }
#elif defined(TRY_SESSION_LOOKUP)
void invalid_session(MiniAsElfObjectView *view) { (void)view->stmts; }
#elif defined(TRY_EMISSION_ACCESS)
void invalid_emission(MiniAsElfObjectView *view) { (void)view->emission; }
#endif
