#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>

#ifdef _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#endif

#include "../../parser/common.h"
#include "../../parser/ast.h"
#include "../include/error.h"
#include "../include/conversion.h"

typedef struct
{
	char *currLine;
	int line;
	int column;
} codepos_t;

FILE *ptrs_errorfile = NULL;

void ptrs_getpos(codepos_t *pos, ptrs_ast_t *ast)
{
	int line = 1;
	int column = 1;
	char *currLine = ast->code;
	for(int i = 0; i < ast->codepos; i++)
	{
		if(ast->code[i] == '\n')
		{
			line++;
			column = 1;
			currLine = &(ast->code[i + 1]);
		}
		else
		{
			column++;
		}
	}

	pos->currLine = currLine;
	pos->line = line;
	pos->column = column;
}

char *ptrs_backtrace()
{
	int bufflen = 1024;
	char *buff = malloc(bufflen);
	char *buffptr = buff;

	jit_stack_trace_t trace = jit_exception_get_stack_trace();
	int count = jit_stack_trace_get_size(trace);

	for(int i = 0; i < count; i++)
	{
		if(buffptr - buff > bufflen - 128)
		{
			int diff = buffptr - buff;
			bufflen *= 2;
			buff = realloc(buff, bufflen);
			buffptr = buff + diff;
		}

		jit_function_t func = jit_stack_trace_get_function(ptrs_jit_context, trace, i);
		if(func)
		{
			//TODO
		}
		else
		{
#ifdef _GNU_SOURCE
			void *ptr = jit_stack_trace_get_pc(trace, i);
			Dl_info info;

			if(dladdr(ptr, &info) == 0)
			{
				info.dli_sname = NULL;
				info.dli_fname = NULL;
			}

			if(info.dli_sname != NULL)
				buffptr += sprintf(buffptr, "    at %s ", info.dli_sname);
			else
				buffptr += sprintf(buffptr, "    at %p ", ptr);

			if(info.dli_fname != NULL)
				buffptr += sprintf(buffptr, "(%s)\n", info.dli_fname);
			else
				buffptr += sprintf(buffptr, "(unknown)\n");
#else
			buffptr += sprintf(buffptr, "    at %p (unknown)\n", jit_stack_trace_get_pc(trace, i));
#endif
		}
	}

	return buff;
}

void *ptrs_formatErrorMsg(const char *msg, va_list ap)
{
	//special printf formats:
	//		%t for printing a type
	//		%mt for printing the type stored in a ptrs_meta_t
	//		%ms for printing the array size stored in a ptrs_meta_t
	//		%v for printing a variable

	int bufflen = 1024;
	char *buff = malloc(bufflen);
	char *buffptr = buff;

	while(*msg != 0)
	{
		if(*msg == '%')
		{
			uintptr_t val;
			char valbuff[32];

			const char *str;
			switch(*++msg)
			{
				case 't':
					str = ptrs_typetoa(va_arg(ap, long));
					break;
				case 'm':
					val = va_arg(ap, uintptr_t);
					switch(*++msg)
					{
						case 't':
							str = ptrs_typetoa((*(ptrs_meta_t *)&val).type);
							break;
						case 's':
							sprintf(valbuff, "%d", (*(ptrs_meta_t *)&val).array.size);
							str = valbuff;
							break;
					}
					break;
				case 'v':
					val = va_arg(ap, uintptr_t);
					uintptr_t meta = va_arg(ap, uintptr_t);
					str = ptrs_vartoa(*(ptrs_val_t *)&val, *(ptrs_meta_t *)&meta, buff, 32);
				default:
					;
					char format[3] = {'%', *(msg - 1), 0};
					snprintf(valbuff, 32, format, va_arg(ap, long));
					str = valbuff;
			}

			int len = strlen(str);
			while(buffptr + len > buff + bufflen)
			{
				ptrdiff_t diff = buffptr - buff;
				bufflen *= 2;
				buff = realloc(buff, bufflen);
				buffptr = buff + diff;
			}

			memcpy(buffptr, str, len + 1);
			buffptr += len;
		}
		else
		{
			*buffptr += *msg;
		}

		msg++;
	}

	return buff;
}

void ptrs_handle_sig(int sig, siginfo_t *info, void *data)
{
	//TODO allow catching errors

	ptrs_error_t *error = malloc(sizeof(ptrs_error_t));

	int len = snprintf(NULL, 0, "Received signal: %s", strsignal(sig));
	error->message = malloc(len + 1);
	sprintf(error->message, "Received signal: %s", strsignal(sig));

	error->backtrace = ptrs_backtrace();

	error->file = NULL;
	error->line = -1;
	error->column = -1;
	jit_exception_throw(error);
}

void ptrs_handle_signals(jit_function_t func)
{
	struct sigaction action;
	action.sa_sigaction = ptrs_handle_sig;
	action.sa_flags = SA_SIGINFO | SA_NODEFER | SA_RESTART;

	sigaction(SIGINT, &action, NULL);
	sigaction(SIGQUIT, &action, NULL);
	sigaction(SIGTERM, &action, NULL);
	//SIGHUP?

	sigaction(SIGILL, &action, NULL);
	sigaction(SIGABRT, &action, NULL);
	sigaction(SIGFPE, &action, NULL);
	sigaction(SIGSEGV, &action, NULL);
	sigaction(SIGPIPE, &action, NULL);
}

void ptrs_handle_error(ptrs_ast_t *ast, const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);

	ptrs_error_t *error = malloc(sizeof(ptrs_error_t));
	error->message = ptrs_formatErrorMsg(msg, ap);
	error->backtrace = ptrs_backtrace();
	error->file = ast->file;

	codepos_t pos;
	ptrs_getpos(&pos, ast);
	error->line = pos.line;
	error->column = pos.column;
}

void ptrs_jit_assert(ptrs_ast_t *ast, jit_function_t func, jit_value_t condition,
	size_t argCount, const char *text, ...)
{
	va_list ap;
	va_start(ap, text);

	argCount += 2;
	jit_value_t args[argCount];
	args[0] = jit_const_int(func, void_ptr, (uintptr_t)ast);
	args[1] = jit_const_int(func, void_ptr, (uintptr_t)text);

	for(size_t i = 2; i < argCount; i++)
		args[i] = va_arg(ap, jit_value_t);

	va_end(ap);

	jit_label_t label = jit_label_undefined;
	jit_insn_branch_if(func, condition, &label);

	jit_type_t params[2] = {jit_type_void_ptr, jit_type_void_ptr};
	jit_type_t signature = jit_type_create_signature(jit_abi_vararg, jit_type_void, params, 2, 1);
	jit_insn_call_native(func, "ptrs_handle_error", ptrs_handle_error, signature, args, argCount, JIT_CALL_NORETURN);
	jit_type_free(signature);

	jit_insn_label(func, &label);
}
