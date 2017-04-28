#include "../include/scope.h"

void ptrs_scope_storePatches(ptrs_patchstore_t *store, ptrs_scope_t *scope)
{
	store->breakLabel = scope->breakLabel;
	store->continueLabel = scope->continueLabel;
	store->breakPatches = scope->breakPatches;
	store->continuePatches = scope->continuePatches;

	scope->breakLabel = 0;
	scope->continueLabel = 0;
	scope->breakPatches = NULL;
	scope->continuePatches = NULL;
}

void ptrs_scope_restorePatches(ptrs_patchstore_t *store, ptrs_scope_t *scope)
{
	scope->breakLabel = store->breakLabel;
	scope->continueLabel = store->continueLabel;
	scope->breakPatches = store->breakPatches;
	scope->continuePatches = store->continuePatches;
}

void ptrs_scope_patch(jit_state_t *jit, ptrs_patchlist_t *curr)
{
	while(curr != NULL)
	{
		jit_patch(jit, curr->patch);

		ptrs_patchlist_t *prev = curr;
		curr = curr->next;
		free(prev);
	}
}

void ptrs_scope_store(jit_state_t *jit, ptrs_scope_t *scope, ptrs_symbol_t symbol, long val, long meta)
{
	if(symbol.scope > 0)
	{
		long tmp = R(scope->usedRegCount);
		jit_ldxi(jit, tmp, R_FP, scope->fpOffset, sizeof(void *));

		for(int i = 0; i < symbol.scope; i++)
			jit_ldr(jit, tmp, tmp, sizeof(void *));

		jit_stxi(jit, symbol.offset, tmp, val, sizeof(ptrs_val_t));
		jit_stxi(jit, symbol.offset + 8, tmp, meta, sizeof(ptrs_meta_t));
	}
	else
	{
		jit_stxi(jit, scope->fpOffset + symbol.offset, R_FP, val, sizeof(ptrs_val_t));
		jit_stxi(jit, scope->fpOffset + symbol.offset + 8, R_FP, meta, sizeof(ptrs_meta_t));
	}
}

void ptrs_scope_load(jit_state_t *jit, ptrs_scope_t *scope, ptrs_symbol_t symbol, long val, long meta)
{
	if(symbol.scope > 0)
	{
		long tmp = R(scope->usedRegCount);
		jit_ldxi(jit, tmp, R_FP, scope->fpOffset, sizeof(void *));

		for(int i = 0; i < symbol.scope; i++)
			jit_ldr(jit, tmp, tmp, sizeof(void *));

		jit_ldxi(jit, val, tmp, symbol.offset, sizeof(ptrs_val_t));
		jit_ldxi(jit, meta, tmp, symbol.offset + 8, sizeof(ptrs_meta_t));
	}
	else
	{
		jit_ldxi(jit, val, R_FP, scope->fpOffset + symbol.offset, sizeof(ptrs_val_t));
		jit_ldxi(jit, meta, R_FP, scope->fpOffset + symbol.offset + 8, sizeof(ptrs_meta_t));
	}
}

ptrs_var_t *ptrs_stack_get(ptrs_stackframe_t *frame, ptrs_symbol_t symbol)
{
	while(symbol.scope-- > 0)
		frame = frame->outer;

	return (ptrs_var_t *)((uint8_t *)&frame->variables + symbol.offset);
}
void ptrs_stack_set(ptrs_stackframe_t *frame, ptrs_symbol_t symbol, ptrs_var_t *val)
{
	while(symbol.scope-- > 0)
		frame = frame->outer;

	memcpy((uint8_t *)&frame->variables + symbol.offset, val, sizeof(ptrs_var_t));
}
