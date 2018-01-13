/*
 * BSD 3-Clause License
 *
 * Copyright (c) 2017, plures
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <stdio.h>
#include <assert.h>
#include <setjmp.h>
#include "ndtypes.h"
#include "seq.h"
#include "grammar.h"
#include "lexer.h"


#ifdef YYDEBUG
int yydebug = 1;
#endif


static FILE *
ndt_fopen(const char *name, const char *mode)
{
#ifdef _MSC_VER
    FILE *fp;

    (void)fopen_s(&fp, name, mode);
    return fp;
#else
    return fopen(name, mode);
#endif
}

/* The yy_fatal_error() function of flex calls exit(). We intercept the function
   and do a longjmp() for proper error handling. */
jmp_buf ndt_lexerror;


static ndt_t *
_ndt_from_fp(ndt_meta_t *m, FILE *fp, ndt_context_t *ctx)
{
    volatile yyscan_t scanner = NULL;
    ndt_t *ast = NULL;
    int ret;

    if (setjmp(ndt_lexerror) == 0) {
        if (yylex_init_extra(ctx, (yyscan_t *)&scanner) != 0) {
            ndt_err_format(ctx, NDT_LexError, "lexer initialization failed");
            return NULL;
        }

        if (fp != stdin) {
            yyset_in(fp, scanner);
        }

        ret = yyparse(scanner, &ast, m, ctx);
        yylex_destroy(scanner);

        if (ret == 2) {
            ndt_err_format(ctx, NDT_MemoryError, "out of memory");
        }

        return ast;
    }
    else {
        if (scanner) {
            yylex_destroy(scanner);
        }
        ndt_err_format(ctx, NDT_MemoryError,
            "out of memory (most likely) or internal lexer error");
        return NULL;
    }
}

static ndt_t *
_ndt_from_file(ndt_meta_t *m, const char *name, ndt_context_t *ctx)
{
    FILE *fp;
    ndt_t *t;

    if (strcmp(name, "-") == 0) {
        fp = stdin;
    }
    else {
        fp = ndt_fopen(name, "rb");
        if (fp == NULL) {
            ndt_err_format(ctx, NDT_OSError, "could not open %s", name);
            return NULL;
        }
    }

    t = _ndt_from_fp(m, fp, ctx);
    fclose(fp);

    return t;
}

ndt_t *
ndt_from_file(const char *name, ndt_context_t *ctx)
{
    return _ndt_from_file(NULL, name, ctx);
}

ndt_t *
ndt_from_file_fill_meta(ndt_meta_t *m, const char *name, ndt_context_t *ctx)
{
    return _ndt_from_file(m, name, ctx);
}

static ndt_t *
_ndt_from_string(ndt_meta_t *m, const char *input, ndt_context_t *ctx)
{
    volatile yyscan_t scanner = NULL;
    volatile YY_BUFFER_STATE state = NULL;
    char *buffer;
    size_t size;
    ndt_t *ast = NULL;
    int ret;

    size = strlen(input);
    if (size > INT_MAX / 2) {
        /* The code generated by flex truncates size_t in several places. */
        ndt_err_format(ctx, NDT_LexError, "maximum input length: %d", INT_MAX/2);
        return NULL;
    }

    buffer = ndt_alloc_size(size+2);
    if (buffer == NULL) {
        return ndt_memory_error(ctx);
    }
    memcpy(buffer, input, size);
    buffer[size] = '\0';
    buffer[size+1] = '\0';

    if (setjmp(ndt_lexerror) == 0) {
        if (yylex_init_extra(ctx, (yyscan_t *)&scanner) != 0) {
            ndt_err_format(ctx, NDT_LexError, "lexer initialization failed");
            ndt_free(buffer);
            return NULL;
        }

        state = yy_scan_buffer(buffer, size+2, scanner);
        state->yy_bs_lineno = 1;
        state->yy_bs_column = 1;

        ret = yyparse(scanner, &ast, m, ctx);
        yy_delete_buffer(state, scanner);
        yylex_destroy(scanner);
        ndt_free(buffer);

        if (ret == 2) {
            ndt_err_format(ctx, NDT_MemoryError, "out of memory");
        }

        return ast;
    }
    else { /* fatal lexer error */
        if (state) {
            ndt_free(state);
        }
        if (scanner) {
            yylex_destroy(scanner);
        }
        ndt_free(buffer);
        ndt_err_format(ctx, NDT_MemoryError, "flex: internal lexer error");
        return NULL;
    }
}

ndt_t *
ndt_from_string(const char *input, ndt_context_t *ctx)
{
    return _ndt_from_string(NULL, input, ctx);
}

ndt_t *
ndt_from_string_fill_meta(ndt_meta_t *m, const char *input, ndt_context_t *ctx)
{
    return _ndt_from_string(m, input, ctx);
}

ndt_t *
ndt_from_metadata_and_dtype(const ndt_meta_t *m, const char *dtype, ndt_context_t *ctx)
{
    ndt_t *t, *type;
    int i;

    type = ndt_from_string(dtype, ctx);
    if (type == NULL) {
        return NULL;
    }

    if (ndt_is_abstract(type)) {
        ndt_err_format(ctx, NDT_InvalidArgumentError,
            "cannot create abstract type with offsets");
        ndt_del(type);
        return NULL;
    }

    for (i=m->num_offset_arrays-1, t=type; i >= 0; i--, type=t) {
        t = ndt_var_dim(type, ExternalOffsets, m->num_offsets[i], m->offset_arrays[i],
                        0, NULL, ctx);
        if (t == NULL) {
            return NULL;
        }
    }

    return t;
}
