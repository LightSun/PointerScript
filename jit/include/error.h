#ifndef _PTRS_ERROR
#define _PTRS_ERROR

#include <stdio.h>
#include <setjmp.h>
#include "../../parser/ast.h"
#include "../../parser/common.h"

extern FILE *ptrs_errorfile;
extern bool ptrs_enableExceptions;
extern bool ptrs_enableSafety;

typedef struct
{
	char *currLine;
	int line;
	int column;
} ptrs_codepos_t;

typedef struct ptrs_error
{
	char *message;
	char *backtrace;
	ptrs_ast_t *ast;
	ptrs_codepos_t pos;
} ptrs_error_t;

void ptrs_handle_signals();
void ptrs_printError(ptrs_error_t *error);
void ptrs_error(ptrs_ast_t *ast, const char *msg, ...);

struct ptrs_assertion *ptrs_jit_vassert(ptrs_ast_t *ast, jit_function_t func, ptrs_scope_t *scope,
	jit_value_t condition, size_t argCount, const char *text, va_list ap);
struct ptrs_assertion *ptrs_jit_assert(ptrs_ast_t *ast, jit_function_t func, ptrs_scope_t *scope,
	jit_value_t condition, size_t argCount, const char *text, ...);
void ptrs_jit_appendAssert(jit_function_t func, struct ptrs_assertion *assert, jit_value_t condition);
void ptrs_jit_placeAssertions(jit_function_t func, ptrs_scope_t *scope);

#endif
