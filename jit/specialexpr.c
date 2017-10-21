#include <stdint.h>
#include <inttypes.h>
#include <assert.h>

#include "../parser/ast.h"
#include "../parser/common.h"
#include "include/error.h"
#include "include/conversion.h"
#include "include/call.h"
#include "include/struct.h"
#include "include/util.h"
#include "include/run.h"

ptrs_jit_var_t ptrs_handle_call(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	struct ptrs_ast_call *expr = &node->arg.call;

	if(expr->value->callHandler != NULL)
	{
		return expr->value->callHandler(expr->value, func, scope, node, expr->retType, expr->arguments);
	}
	else
	{
		ptrs_jit_var_t val = expr->value->handler(expr->value, func, scope);
		return ptrs_jit_call(node, func, scope, expr->retType, val, expr->arguments);
	}

}

ptrs_jit_var_t ptrs_handle_stringformat(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	struct ptrs_ast_strformat *expr = &node->arg.strformat;

	size_t argCount = expr->insertionCount + 3;
	jit_type_t argDef[argCount];
	jit_value_t args[argCount];

	argDef[0] = jit_type_void_ptr;
	argDef[1] = jit_type_nuint;
	argDef[2] = jit_type_void_ptr;
	args[2] = jit_const_int(func, void_ptr, (uintptr_t)expr->str);

	struct ptrs_stringformat *curr = expr->insertions;
	for(int i = 3; i < argCount; i++)
	{
		ptrs_jit_var_t val = curr->entry->handler(curr->entry, func, scope);

		if(curr->convert)
			val = ptrs_jit_vartoa(func, val);

		argDef[i] = jit_value_get_type(val.val);
		args[i] = val.val;
		curr = curr->next;
	}

	jit_type_t signature = jit_type_create_signature(jit_abi_cdecl, jit_type_sys_int, argDef, argCount, 0);

	args[0] = jit_const_int(func, void_ptr, 0);
	args[1] = jit_const_int(func, nuint, 0);
	jit_value_t len = jit_insn_call_native(func, "snprintf", snprintf, signature, args, argCount, 0);

	len = jit_insn_add(func, len, jit_const_int(func, sys_int, 1));
	jit_value_t buff = jit_insn_alloca(func, len);

	args[0] = buff;
	args[1] = len;
	jit_insn_call_native(func, "snprintf", snprintf, signature, args, argCount, 0);

	ptrs_jit_var_t ret = {
		.val = buff,
		.meta = ptrs_jit_arrayMeta(func,
			jit_const_long(func, ulong, PTRS_TYPE_NATIVE),
			jit_const_long(func, ulong, 0),
			len),
		.constType = PTRS_TYPE_NATIVE,
	};
	return ret;
}

ptrs_jit_var_t ptrs_handle_new(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	struct ptrs_ast_new *expr = &node->arg.newexpr;
	ptrs_jit_var_t val = expr->value->handler(expr->value, func, scope);

	return ptrs_struct_construct(node, func, scope,
		val, expr->arguments, expr->onStack);
}

ptrs_jit_var_t ptrs_handle_member(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	struct ptrs_ast_member *expr = &node->arg.member;
	ptrs_jit_var_t base = expr->base->handler(expr->base, func, scope);

	if(jit_value_is_constant(base.meta))
	{
		ptrs_meta_t meta = ptrs_jit_value_getMetaConstant(base.meta);
		ptrs_struct_t *struc = ptrs_meta_getPointer(meta);

		return ptrs_jit_struct_get(func, node, scope, base.val, struc, expr->name);
	}
	else
	{
		jit_value_t ret;
		jit_value_t astVal = jit_const_int(func, void_ptr, (uintptr_t)node);
		jit_value_t nameVal = jit_const_int(func, void_ptr, (uintptr_t)expr->name);
		ptrs_jit_reusableCall(func, ptrs_struct_get, ret, ptrs_jit_getVarType(),
			(jit_type_void_ptr, jit_type_long, jit_type_ulong, jit_type_void_ptr),
			(astVal, base.val, base.meta, nameVal)
		);

		return ptrs_jit_valToVar(func, ret);
	}
}
void ptrs_handle_assign_member(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope,
	ptrs_jit_var_t val)
{
	struct ptrs_ast_member *expr = &node->arg.member;
	ptrs_jit_var_t base = expr->base->handler(expr->base, func, scope);

	if(jit_value_is_constant(base.meta))
	{
		ptrs_meta_t meta = ptrs_jit_value_getMetaConstant(base.meta);
		ptrs_struct_t *struc = ptrs_meta_getPointer(meta);

		return ptrs_jit_struct_set(func, node, scope, base.val, struc, expr->name, val);
	}
	else
	{
		ptrs_jit_reusableCallVoid(func, ptrs_struct_set,
			(
				jit_type_void_ptr,
				jit_type_long,
				jit_type_ulong,
				jit_type_void_ptr,
				jit_type_long,
				jit_type_ulong
			), (
				jit_const_int(func, void_ptr, (uintptr_t)node),
				base.val,
				base.meta,
				jit_const_int(func, void_ptr, (uintptr_t)expr->name),
				val.val,
				val.meta
			)
		);
	}
}
ptrs_jit_var_t ptrs_handle_addressof_member(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	struct ptrs_ast_member *expr = &node->arg.member;
	ptrs_jit_var_t base = expr->base->handler(expr->base, func, scope);

	jit_value_t ret;
	jit_value_t astVal = jit_const_int(func, void_ptr, (uintptr_t)node);
	jit_value_t nameVal = jit_const_int(func, void_ptr, (uintptr_t)expr->name);
	ptrs_jit_reusableCall(func, ptrs_struct_addressOf, ret, ptrs_jit_getVarType(),
		(jit_type_void_ptr, jit_type_long, jit_type_ulong, jit_type_void_ptr),
		(astVal, base.val, base.meta, nameVal)
	);

	return ptrs_jit_valToVar(func, ret);
}
ptrs_jit_var_t ptrs_handle_call_member(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope,
	ptrs_ast_t *caller, ptrs_nativetype_info_t *retType, struct ptrs_astlist *arguments)
{
	//TODO
}

ptrs_jit_var_t ptrs_handle_prefix_sizeof(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	node = node->arg.astval;

	ptrs_jit_var_t val = node->handler(node, func, scope);

	ptrs_jit_typeRangeCheck(node, func, scope, val, PTRS_TYPE_NATIVE, PTRS_TYPE_POINTER,
		1, "Cannot get size of variable of type %t", TYPECHECK_TYPE);

	val.val = ptrs_jit_getArraySize(func, val.meta);
	val.meta = ptrs_jit_const_meta(func, PTRS_TYPE_INT);
	val.constType = PTRS_TYPE_INT;

	return val;
}

ptrs_jit_var_t ptrs_handle_prefix_address(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	node = node->arg.astval;

	if(node->addressHandler == NULL)
		ptrs_error(node, "Cannot get address of temporary or constant value");

	return node->addressHandler(node, func, scope);
}

ptrs_jit_var_t ptrs_handle_prefix_dereference(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	node = node->arg.astval;
	ptrs_jit_var_t val = node->handler(node, func, scope);

	ptrs_jit_var_t ret = {
		jit_value_create(func, jit_type_long),
		jit_value_create(func, jit_type_ulong),
		-1,
	};

	ptrs_jit_typeSwitch(node, func, scope, val,
		(1, "Cannot dereference variable of type %t", TYPESWITCH_TYPE),
		(PTRS_TYPE_NATIVE, PTRS_TYPE_POINTER),
		case PTRS_TYPE_POINTER:
			jit_insn_store(func, ret.val,
				jit_insn_load_relative(func, val.val, 0, jit_type_long));
			jit_insn_store(func, ret.meta,
				jit_insn_load_relative(func, val.val, sizeof(ptrs_val_t), jit_type_ulong));
			break;

		case PTRS_TYPE_NATIVE:
			jit_insn_store(func, ret.val, jit_insn_load_relative(func, val.val, 0, jit_type_ubyte));
			jit_insn_store(func, ret.meta, ptrs_jit_const_meta(func, PTRS_TYPE_INT));
			ret.constType = PTRS_TYPE_INT;
			break;
	);

	return ret;
}
void ptrs_handle_assign_dereference(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope, ptrs_jit_var_t val)
{
	node = node->arg.astval;
	ptrs_jit_var_t base = node->handler(node, func, scope);

	ptrs_jit_typeSwitch(node, func, scope, base,
		(1, "Cannot dereference variable of type %t", TYPESWITCH_TYPE),
		(PTRS_TYPE_NATIVE, PTRS_TYPE_POINTER),

		case PTRS_TYPE_NATIVE:
			;
			jit_value_t byteVal = jit_insn_convert(func, ptrs_jit_vartoi(func, val), jit_type_ubyte, 0);
			jit_insn_store_relative(func, base.val, 0, byteVal);
			break;

		case PTRS_TYPE_POINTER:
			jit_insn_store_relative(func, base.val, 0, val.val);
			jit_insn_store_relative(func, base.val, sizeof(ptrs_val_t), val.meta);
			break;
	);
}

ptrs_jit_var_t ptrs_handle_indexlength(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	ptrs_jit_var_t ret;
	ret.val = scope->indexSize;
	ret.meta = ptrs_jit_const_meta(func, PTRS_TYPE_INT);
	ret.constType = PTRS_TYPE_INT;

	return ret;
}

static void ptrs_handle_index_common(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope,
	ptrs_jit_var_t *base, jit_value_t *type, jit_value_t *size, jit_value_t *index)
{
	struct ptrs_ast_binary *expr = &node->arg.binary;
	*base = expr->left->handler(expr->left, func, scope);
	*type = ptrs_jit_getType(func, base->meta);

	jit_value_t oldSize = scope->indexSize;
	scope->indexSize = ptrs_jit_getArraySize(func, base->meta);

	ptrs_jit_var_t _index = expr->right->handler(expr->right, func, scope);
	*index = ptrs_jit_vartoi(func, _index);

	struct ptrs_assertion *assertion = ptrs_jit_assert(node, func, scope,
		jit_insn_lt(func, *index, scope->indexSize),
		2, "Index %d is out of range of array of size %d", *index, scope->indexSize);
	ptrs_jit_appendAssert(func, assertion, jit_insn_ge(func, *index, jit_const_long(func, long, 0)));

	if(size != NULL)
		*size = scope->indexSize;
	scope->indexSize = oldSize;
}

ptrs_jit_var_t ptrs_handle_index(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	ptrs_jit_var_t base;
	jit_value_t index;
	jit_value_t type;
	ptrs_handle_index_common(node, func, scope, &base, &type, NULL, &index);

	ptrs_jit_var_t result = {
		.val = jit_value_create(func, jit_type_long),
		.meta = jit_value_create(func, jit_type_ulong),
		.constType = -1,
	};

	ptrs_jit_typeSwitch(node, func, scope, base,
		(1, "Cannot dereference variable of type %t", type),
		(PTRS_TYPE_NATIVE, PTRS_TYPE_POINTER),

		case PTRS_TYPE_NATIVE:
			jit_insn_store(func, result.val, jit_insn_load_elem(func, base.val, index, jit_type_ubyte));
			jit_insn_store(func, result.meta, ptrs_jit_const_meta(func, PTRS_TYPE_INT));
			break;

		case PTRS_TYPE_POINTER:
			;
			jit_value_t valIndex = jit_insn_shl(func, index, jit_const_int(func, nint, 1));
			jit_insn_store(func, result.val, jit_insn_load_elem(func, base.val, valIndex, jit_type_long));
			jit_value_t metaIndex = jit_insn_add(func, valIndex, jit_const_int(func, nint, 1));
			jit_insn_store(func, result.meta, jit_insn_load_elem(func, base.val, metaIndex, jit_type_ulong));
			break;
	);

	return result;
}
void ptrs_handle_assign_index(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope, ptrs_jit_var_t val)
{
	ptrs_jit_var_t base;
	jit_value_t index;
	jit_value_t type;
	ptrs_handle_index_common(node, func, scope, &base, &type, NULL, &index);

	ptrs_jit_typeSwitch(node, func, scope, base,
		(1, "Cannot dereference variable of type %t", type),
		(PTRS_TYPE_NATIVE, PTRS_TYPE_POINTER),

		case PTRS_TYPE_NATIVE:
			;
			jit_value_t intVal = ptrs_jit_vartoi(func, val);
			jit_value_t uByteVal = jit_insn_convert(func, intVal, jit_type_ubyte, 1);
			jit_insn_store_elem(func, base.val, index, uByteVal);
			break;

		case PTRS_TYPE_POINTER:
			;
			jit_value_t valIndex = jit_insn_shl(func, index, jit_const_int(func, nint, 1));
			jit_insn_store_elem(func, base.val, valIndex, val.val);
			jit_value_t metaIndex = jit_insn_add(func, valIndex, jit_const_int(func, nint, 1));
			jit_insn_store_elem(func, base.val, metaIndex, val.meta);
			break;
	);
}
ptrs_jit_var_t ptrs_handle_addressof_index(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	ptrs_jit_var_t base;
	jit_value_t index;
	jit_value_t type;
	jit_value_t size;
	ptrs_handle_index_common(node, func, scope, &base, &type, &size, &index);

	ptrs_jit_var_t result = {
		.val = jit_value_create(func, jit_type_long),
		.meta = ptrs_jit_setArraySize(func, base.meta, jit_insn_sub(func, size, index)),
		.constType = base.constType,
	};

	ptrs_jit_typeSwitch(node, func, scope, base,
		(1, "Cannot dereference variable of type %t", type),
		(PTRS_TYPE_NATIVE, PTRS_TYPE_POINTER),

		case PTRS_TYPE_NATIVE:
			jit_insn_store(func, result.val,
				jit_insn_load_elem_address(func, base.val, index, jit_type_ubyte));
			break;

		case PTRS_TYPE_POINTER:
			;
			jit_value_t valIndex = jit_insn_shl(func, index, jit_const_int(func, nint, 1));
			jit_insn_store(func, result.val,
				jit_insn_load_elem_address(func, base.val, valIndex, jit_type_long));
			break;
	);

	return result;
}
ptrs_jit_var_t ptrs_handle_call_index(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope,
	ptrs_ast_t *caller, struct ptrs_astlist *arguments)
{
	//TODO
}

ptrs_jit_var_t ptrs_handle_slice(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	struct ptrs_ast_slice *expr = &node->arg.slice;

	ptrs_jit_var_t val = expr->base->handler(expr->base, func, scope);
	jit_value_t type = ptrs_jit_getType(func, val.meta);

	jit_value_t oldSize = scope->indexSize;
	scope->indexSize = ptrs_jit_getArraySize(func, val.meta);

	ptrs_jit_var_t _start = expr->start->handler(expr->start, func, scope);
	jit_value_t start = ptrs_jit_vartoi(func, _start);

	ptrs_jit_var_t _end = expr->end->handler(expr->end, func, scope);
	jit_value_t end = ptrs_jit_vartoi(func, _end);

	jit_value_t newSize = jit_insn_sub(func, end, start);
	jit_value_t newPtr = jit_value_create(func, jit_type_void_ptr);

	jit_label_t done = jit_label_undefined;
	jit_insn_store(func, newPtr, jit_insn_load_elem_address(func, val.val, start, jit_type_ubyte));
	jit_insn_branch_if(func, jit_insn_eq(func, type, jit_const_int(func, ulong, PTRS_TYPE_NATIVE)), &done);

	ptrs_jit_assert(node, func, scope, jit_insn_eq(func, type, jit_const_int(func, ulong, PTRS_TYPE_POINTER)),
		1, "Cannot slice variable of type %t", type);
	jit_insn_store(func, newPtr, jit_insn_load_elem_address(func, val.val, start, jit_type_ubyte));

	jit_insn_label(func, &done);

	/*
	disabled so we can slice arrays received from native functions.
	TODO: should there be a seperate syntax for that?

	ptrs_jit_assert(node, func, scope, jit_insn_le(func, end, scope->indexSize),
		2, "Canot end a slice at %d for an array of size %d", end, scope->indexSize);
	ptrs_jit_assert(node, func, scope, jit_insn_ge(func, start, jit_const_long(func, ulong, 0)),
		1, "Canot start a slice at %d", start);
	ptrs_jit_assert(node, func, scope, jit_insn_lt(func, start, end),
		2, "Slice start (%d) is bigger than slice end (%d)", start, end);
	*/

	scope->indexSize = oldSize;

	ptrs_jit_var_t ret;
	ret.val = newPtr;
	ret.meta = ptrs_jit_setArraySize(func, val.meta, newSize);
	ret.constType = val.constType;

	return ret;
}

ptrs_jit_var_t ptrs_handle_as(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	struct ptrs_ast_cast *expr = &node->arg.cast;

	ptrs_jit_var_t val = expr->value->handler(expr->value, func, scope);

	if(expr->builtinType != PTRS_TYPE_FLOAT)
		val.val = ptrs_jit_reinterpretCast(func, val.val, jit_type_long);

	val.meta = ptrs_jit_const_meta(func, expr->builtinType);
	val.constType = expr->builtinType;
	return val;
}

ptrs_jit_var_t ptrs_handle_cast_builtin(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	struct ptrs_ast_cast *expr = &node->arg.cast;
	ptrs_jit_var_t val = expr->value->handler(expr->value, func, scope);

	switch(expr->builtinType)
	{
		case PTRS_TYPE_INT:
			val.val = ptrs_jit_vartoi(func, val);
			val.meta = ptrs_jit_const_meta(func, PTRS_TYPE_INT);
			val.constType = PTRS_TYPE_INT;
			break;
		case PTRS_TYPE_FLOAT:
			val.val = ptrs_jit_vartof(func, val);
			val.meta = ptrs_jit_const_meta(func, PTRS_TYPE_FLOAT);
			val.constType = PTRS_TYPE_FLOAT;
			break;
		default:
			ptrs_error(node, "Cannot convert to type %s", ptrs_typetoa(expr->builtinType));
	}

	return val;
}

ptrs_jit_var_t ptrs_handle_tostring(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	struct ptrs_ast_cast *expr = &node->arg.cast;
	ptrs_jit_var_t val = expr->value->handler(expr->value, func, scope);

	return ptrs_jit_vartoa(func, val);
}

ptrs_jit_var_t ptrs_handle_cast(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	//TODO
}

ptrs_jit_var_t ptrs_handle_importedsymbol(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	struct ptrs_ast_importedsymbol *expr = &node->arg.importedsymbol;
	struct ptrs_ast_import *stmt = &expr->import->arg.import;

	if(stmt->isScriptImport)
	{
		ptrs_ast_t *ast = stmt->expressions[expr->index];
		return ast->handler(ast, func, scope);
	}
	else if(expr->type == NULL)
	{
		return ptrs_jit_varFromConstant(func, stmt->symbols[expr->index]);
	}
	else
	{
		ptrs_jit_var_t ret;
		jit_value_t addr = jit_const_int(func, void_ptr,
			(uintptr_t)stmt->symbols[expr->index].value.nativeval);

		ret.val = jit_insn_load_relative(func, addr, 0, expr->type->jitType);
		ret.meta = ptrs_jit_const_meta(func, expr->type->varType);
		ret.constType = expr->type->varType;

		return ret;
	}
}
void ptrs_handle_assign_importedsymbol(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope,
	ptrs_jit_var_t val)
{
	struct ptrs_ast_importedsymbol *expr = &node->arg.importedsymbol;
	struct ptrs_ast_import *stmt = &expr->import->arg.import;

	if(stmt->isScriptImport)
	{
		ptrs_ast_t *ast = stmt->expressions[expr->index];
		if(ast->setHandler == NULL)
			ptrs_error(node, "Invalid assign expression, left side is not a valid lvalue");
		else
			ast->setHandler(ast, func, scope, val);

		return;
	}

	if(expr->type == NULL)
	{
		ptrs_error(node, "Cannot re-assign an imported function");
	}

	jit_value_t addr = jit_const_int(func, void_ptr,
		(uintptr_t)stmt->symbols[expr->index].value.nativeval);
	ptrs_jit_assignTypedFromVar(func, addr, expr->type->jitType, val);
}
ptrs_jit_var_t ptrs_handle_call_importedsymbol(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope,
	ptrs_ast_t *caller, ptrs_nativetype_info_t *retType, struct ptrs_astlist *arguments)
{
	struct ptrs_ast_importedsymbol *expr = &node->arg.importedsymbol;
	struct ptrs_ast_import *stmt = &expr->import->arg.import;
	ptrs_jit_var_t val;

	if(stmt->isScriptImport)
	{
		ptrs_ast_t *ast = stmt->expressions[expr->index];
		if(ast->callHandler != NULL)
			return ast->callHandler(ast, func, scope, caller, retType, arguments);
		else
			val = ast->handler(ast, func, scope);
	}
	else if(expr->type == NULL)
	{
		val = ptrs_jit_varFromConstant(func, stmt->symbols[expr->index]);
	}
	else
	{
		ptrs_jit_var_t ret;
		jit_value_t addr = jit_const_int(func, void_ptr,
			(uintptr_t)stmt->symbols[expr->index].value.nativeval);

		val.val = jit_insn_load_relative(func, addr, 0, expr->type->jitType);
		val.meta = ptrs_jit_const_meta(func, expr->type->varType);
		val.constType = expr->type->varType;
	}

	return ptrs_jit_call(node, func, scope, retType, val, arguments);
}
ptrs_jit_var_t ptrs_handle_addressof_importedsymbol(ptrs_ast_t *node,
	jit_function_t func, ptrs_scope_t *scope)
{
	struct ptrs_ast_importedsymbol *expr = &node->arg.importedsymbol;
	struct ptrs_ast_import *stmt = &expr->import->arg.import;

	if(stmt->isScriptImport)
	{
		ptrs_ast_t *ast = stmt->expressions[expr->index];
		if(ast->addressHandler != NULL)
			return ast->addressHandler(ast, func, scope);
		else
			ptrs_error(node, "Cannot get address of temporary or constant value");
	}
	else if(expr->type == NULL)
	{
		ptrs_error(node, "Cannot get address of imported native function");
	}
	else
	{
		ptrs_jit_var_t ret;
		ret.val = jit_const_int(func, void_ptr,
			(uintptr_t)stmt->symbols[expr->index].value.nativeval);
		ret.meta = ptrs_jit_const_arrayMeta(func, PTRS_TYPE_NATIVE, false, expr->type->size);
		ret.constType = PTRS_TYPE_NATIVE;

		return ret;
	}
}

ptrs_jit_var_t ptrs_handle_identifier(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	ptrs_jit_var_t target = *node->arg.varval;
	jit_function_t targetFunc = jit_value_get_function(target.val);

	if(func == targetFunc)
	{
		return target;
	}
	else
	{
		target.val = jit_insn_import(func, target.val);
		if(target.val == NULL)
			ptrs_error(node, "Cannot access that variable from here");

		target.val = jit_insn_load_relative(func, target.val, 0, jit_type_long);

		if(!jit_value_is_constant(target.meta))
		{
			target.meta = jit_insn_import(func, target.meta);
			target.meta = jit_insn_load_relative(func, target.meta, 0, jit_type_ulong);
		}

		return target;
	}
}
void ptrs_handle_assign_identifier(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope, ptrs_jit_var_t val)
{
	ptrs_jit_var_t target = *node->arg.varval;
	jit_function_t targetFunc = jit_value_get_function(target.val);

	if(func == targetFunc)
	{
		if(target.constType != PTRS_TYPE_FLOAT)
			val.val = ptrs_jit_reinterpretCast(func, val.val, jit_type_long);
		jit_insn_store(func, target.val, val.val);
	}
	else
	{
		target.val = jit_insn_import(func, target.val);
		if(target.val == NULL)
			ptrs_error(node, "Cannot access that variable from here");

		jit_insn_store_relative(func, target.val, 0, val.val);
	}

	if(target.constType != -1)
	{
		if(val.constType == -1)
		{
			jit_value_t type = ptrs_jit_getType(func, val.meta);
			ptrs_jit_assert(node, func, scope,
				jit_insn_eq(func, type, jit_const_long(func, ulong, target.constType)),
				2, "Cannot assign value of type %mt to variable of type %t", type, target.constType);
		}
		else if(val.constType != target.constType)
		{
			ptrs_error(node, "Cannot assign value of type %t to variable of type %t",
				val.constType, target.constType);
		}
	}

	if(jit_value_is_constant(target.meta))
	{
		//nothing
	}
	else if(func == targetFunc)
	{
		jit_insn_store(func, target.meta, val.meta);
	}
	else
	{
		target.meta = jit_insn_import(func, target.meta);
		jit_insn_store_relative(func, target.meta, 0, val.meta);
	}
}
ptrs_jit_var_t ptrs_handle_addressof_identifier(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	ptrs_jit_var_t target = *node->arg.varval;
	jit_function_t targetFunc = jit_value_get_function(target.val);

	if(func == targetFunc)
	{
		target.val = jit_insn_address_of(func, target.val);
	}
	else
	{
		target.val = jit_insn_import(func, target.val);
		if(target.val == NULL)
			ptrs_error(node, "Cannot access that variable from here");
	}

	target.meta = ptrs_jit_const_arrayMeta(func, PTRS_TYPE_POINTER, 0, 1);
	target.constType = PTRS_TYPE_POINTER;
	return target;
}

ptrs_jit_var_t ptrs_handle_functionidentifier(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	jit_function_t target = *node->arg.funcval;

	ptrs_jit_var_t ret;
	ret.val = jit_const_long(func, long, (uintptr_t)jit_function_to_closure(target));
	ret.meta = ptrs_jit_pointerMeta(func,
		jit_const_long(func, ulong, PTRS_TYPE_FUNCTION),
		jit_insn_get_parent_frame_pointer_of(func, target)
	);
	ret.constType = PTRS_TYPE_FUNCTION;

	return ret;
}
ptrs_jit_var_t ptrs_handle_call_functionidentifier(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope,
	ptrs_ast_t *caller, ptrs_nativetype_info_t *retType, struct ptrs_astlist *arguments)
{
	return ptrs_jit_callnested(func, scope, *node->arg.funcval, arguments);
}

ptrs_jit_var_t ptrs_handle_constant(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	return ptrs_jit_varFromConstant(func, node->arg.constval);
}

ptrs_jit_var_t ptrs_handle_lazy(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	//TODO
}

ptrs_jit_var_t ptrs_handle_prefix_typeof(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	node = node->arg.astval;
	ptrs_jit_var_t val = node->handler(node, func, scope);

	if(val.constType == -1)
	{
		val.val = ptrs_jit_getType(func, val.meta);
		val.meta = ptrs_jit_const_meta(func, PTRS_TYPE_INT);
		val.constType = PTRS_TYPE_INT;
	}
	else
	{
		val.val = jit_const_long(func, long, val.constType);
		val.meta = ptrs_jit_const_meta(func, PTRS_TYPE_INT);
		val.constType = PTRS_TYPE_INT;
	}
	return val;
}

ptrs_jit_var_t ptrs_handle_op_instanceof(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	//TODO
}

ptrs_jit_var_t ptrs_handle_op_in(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	//TODO
}

ptrs_jit_var_t ptrs_handle_yield(ptrs_ast_t *node, jit_function_t func, ptrs_scope_t *scope)
{
	//TODO
}
