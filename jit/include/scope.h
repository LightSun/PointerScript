#ifndef _PTRS_SCOPE
#define _PTRS_SCOPE

#include <jitlib.h>
#include "../../parser/common.h"
#include "../../parser/ast.h"

typedef struct
{
	uintptr_t continueLabel;
	uintptr_t breakLabel;
	ptrs_patchlist_t *continuePatches;
	ptrs_patchlist_t *breakPatches;
} ptrs_patchstore_t;

void ptrs_scope_storePatches(ptrs_patchstore_t *store, ptrs_scope_t *scope);
void ptrs_scope_restorePatches(ptrs_patchstore_t *store, ptrs_scope_t *scope);

void ptrs_scope_patch(jit_state_t *jit, ptrs_patchlist_t *curr);

/*
the following to functions expect a stackframe that looks like the following,
the current stackframe starts at R_FP + scope->stackOffset

struct ptrs_stackframe
{
	struct ptrs_stackframe *outer;
	ptrs_var_t variables[];
}
*/
void ptrs_scope_store(jit_state_t *jit, ptrs_scope_t *scope, ptrs_symbol_t symbol, long val, long meta);
void ptrs_scope_load(jit_state_t *jit, ptrs_scope_t *scope, ptrs_symbol_t symbol, long val, long meta);

#endif
