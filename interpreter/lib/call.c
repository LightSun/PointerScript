#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <avcall.h>
#include <callback.h>

#include "../include/error.h"
#include "../include/conversion.h"
#include "../include/stack.h"
#include "../include/call.h"
#include "../include/callback.h"
#include "../include/struct.h"
#include "../../parser/common.h"

ptrs_var_t *ptrs_call(ptrs_ast_t *ast, ptrs_var_t *func, ptrs_var_t *result, struct ptrs_astlist *arguments, ptrs_scope_t *scope)
{
	int len = 0;
	struct ptrs_astlist *list = arguments;
	while(list)
	{
		len++;
		list = list->next;
	}

	ptrs_var_t *arg;
	ptrs_var_t args[len + 1];
	list = arguments;
	for(int i = 0; i < len; i++)
	{
		arg = list->entry->handler(list->entry, &args[i], scope);

		if(arg != &args[i])
			memcpy(&args[i], arg, sizeof(ptrs_var_t));

		list = list->next;
	}
	args[len].type = PTRS_TYPE_NATIVE;
	args[len].value.nativeval = NULL;

	ptrs_var_t overload;

	if(func->type == PTRS_TYPE_FUNCTION)
	{
		result = ptrs_callfunc(ast, result, scope, func, len, args);
	}
	else if(func->type == PTRS_TYPE_NATIVE)
	{
		result->type = PTRS_TYPE_INT;
		result->value.intval = ptrs_callnative(func->value.nativeval, len, args);
	}
	else if(func->type == PTRS_TYPE_STRUCT && (overload.value.funcval = ptrs_struct_getOverload(func, "()")) != NULL)
	{
		overload.type = PTRS_TYPE_FUNCTION;
		overload.meta.this = func->value.structval;
		result = ptrs_callfunc(ast, result, scope, &overload, len, args);
	}
	else
	{
		ptrs_error(ast, scope, "Cannot call value of type %s", ptrs_typetoa(func->type));
	}

	return result;
}

ptrs_var_t *ptrs_callfunc(ptrs_ast_t *callAst, ptrs_var_t *result, ptrs_scope_t *callScope, ptrs_var_t *funcvar, int argc, ptrs_var_t *argv)
{
	ptrs_function_t *func = funcvar->value.funcval;
	ptrs_scope_t *scope = ptrs_scope_increase(callScope, func->stackOffset);
	scope->outer = func->scope;
	scope->callScope = callScope;
	scope->callAst = callAst;
	scope->calleeName = NULL;

	ptrs_var_t val;
	val.type = PTRS_TYPE_UNDEFINED;
	for(int i = 0; i < func->argc; i++)
	{
		if(i < argc)
			ptrs_scope_set(scope, func->args[i], &argv[i]);
		else if(func->argv[i] != NULL)
			ptrs_scope_set(scope, func->args[i], func->argv[i]->handler(func->argv[i], result, scope));
		else
			ptrs_scope_set(scope, func->args[i], &val);
	}

	if(funcvar->meta.this != NULL)
	{
		val.type = PTRS_TYPE_STRUCT;
		val.value.structval = funcvar->meta.this;
		ptrs_scope_set(scope, ptrs_thisSymbol, &val);
	}

	val.type = PTRS_TYPE_POINTER;
	val.value.ptrval = argv;
	ptrs_scope_set(scope, ptrs_argumentsSymbol, &val);

	ptrs_var_t *_result = func->body->handler(func->body, result, scope);

	if(scope->exit != 3)
		result->type = PTRS_TYPE_UNDEFINED;
	else if(result != _result)
		memcpy(result, _result, sizeof(ptrs_var_t));

	return result;
}

void ptrs_callcallback(ptrs_function_t *func, va_alist alist)
{
	va_start_longlong(alist);

	ptrs_var_t argv[func->argc];

	ptrs_var_t result;
	ptrs_var_t funcvar;
	funcvar.type = PTRS_TYPE_FUNCTION;
	funcvar.value.funcval = func;

	ptrs_scope_t scope;
	memset(&scope, 0, sizeof(ptrs_scope_t));
	scope.calleeName = "(native callback)";

	for(int i = 0; i < func->argc; i++)
	{
		argv[i].type = PTRS_TYPE_INT;
		argv[i].value.intval = va_arg_longlong(alist);
	}

	ptrs_callfunc(NULL, &result, &scope, &funcvar, func->argc, argv);

	if(scope.stackstart != NULL)
		free(scope.stackstart);

	//is it a good idea to reset alist's rtype? why does va_start need a type anyways?
	switch(result.type)
	{
		case PTRS_TYPE_UNDEFINED:
			alist->rtype = __VAvoid;
			va_return_void(alist);
			break;
		case PTRS_TYPE_INT:
			alist->rtype = __VAlonglong;
			va_return_longlong(alist, result.value.intval);
			break;
		case PTRS_TYPE_FLOAT:
			alist->rtype = __VAdouble;
			va_return_double(alist, result.value.floatval);
			break;
		default: //pointer type
			alist->rtype = __VAvoidp;
			va_return_ptr(alist, void *, result.value.intval);
			break;
	}
}

int64_t ptrs_callnative(void *func, int argc, ptrs_var_t *argv)
{
	av_alist alist;
	int64_t retVal = 0;
	void *callbackArgs[argc];
	bool hasCallbackArgs = false;
	av_start_longlong(alist, func, &retVal);

	for(int i = 0; i < argc; i++)
	{
		callbackArgs[i] = NULL;
		switch(argv[i].type)
		{
			case PTRS_TYPE_FLOAT:
				av_double(alist, argv[i].value.floatval);
				break;
			case PTRS_TYPE_INT:
				av_longlong(alist, argv[i].value.intval);
				break;
			case PTRS_TYPE_FUNCTION:
				;
				ptrs_function_t *func = argv[i].value.funcval;
				if(func->scope->outer == NULL)
				{
					if(func->nativeCb == NULL)
						func->nativeCb = alloc_callback(&ptrs_callcallback, func);
					av_ptr(alist, void *, func->nativeCb);
					break;
				}
				else
				{
					callbackArgs[i] = alloc_callback(&ptrs_callcallback, func);
					av_ptr(alist, void *, callbackArgs[i]);
				}
			default:
				av_ptr(alist, void *, argv[i].value.nativeval);
				break;
		}
	}

	av_call(alist);

	if(hasCallbackArgs)
	{
		for(int i = 0; i < argc; i++)
		{
			if(callbackArgs[i] != NULL)
				free_callback(callbackArgs[i]);
		}
	}
	return retVal;
}
