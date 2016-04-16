#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ffi.h>

#include "../include/error.h"
#include "../include/stack.h"
#include "../../parser/common.h"

ptrs_var_t *ptrs_callfunc(ptrs_function_t *func, ptrs_var_t *result, int argc, ptrs_var_t *argv)
{
	void *sp = ptrs_stack;
	ptrs_scope_t *scope = ptrs_alloc(sizeof(ptrs_scope_t));
	scope->current = NULL;
	scope->outer = func->scope;

	ptrs_var_t undefined = {{42}, PTRS_TYPE_UNDEFINED};
	for(int i = 0; i < func->argc; i++)
	{
		if(i < argc)
			ptrs_scope_set(scope, func->args[i], &argv[i]);
		else
			ptrs_scope_set(scope, func->args[i], &undefined);
	}

	ptrs_var_t *_result = func->body->handler(func->body, result, scope);

	if(scope->exit != 3)
		result->type = PTRS_TYPE_UNDEFINED;
	else
		result = _result;

	ptrs_stack = sp;
	return result;
}

intptr_t ptrs_callnative(ptrs_ast_t *ast, void *func, int argc, ptrs_var_t *argv)
{
	ffi_cif cif;
	ffi_type *types[argc];
	void *values[argc];

	for(int i = 0; i < argc; i++)
	{
		switch(argv[i].type)
		{
			case PTRS_TYPE_FLOAT:
				types[i] = &ffi_type_double;
				values[i] = &argv[i].value.floatval;
				break;
			case PTRS_TYPE_INT:
				types[i] = &ffi_type_sint64;
				values[i] = &argv[i].value.intval;
				break;
			default:
				types[i] = &ffi_type_pointer;
				values[i] = &argv[i].value.strval;
				break;
		}
	}

	if(ffi_prep_cif(&cif, FFI_DEFAULT_ABI, argc, &ffi_type_pointer, types) != FFI_OK)
		ptrs_error(ast, "Could not call native function %p", func);

	int64_t retVal = 0;
	ffi_call(&cif, func, &retVal, values);
	return retVal;
}
