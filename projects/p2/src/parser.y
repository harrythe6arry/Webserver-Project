/**
 * @file parser.y
 * @brief Grammar for HTTP
 * @author Rajul Bhatnagar (2016)
 */

%{
#include "parse.h"

/* Define YACCDEBUG to enable debug messages for this lex file */
//#define YACCDEBUG
#define YYERROR_VERBOSE
#define METHOD_GET 0
#define METHOD_HEAD 1
#ifdef YACCDEBUG
#include <stdio.h>
#define YPRINTF(...) printf(__VA_ARGS__)
#else
#define YPRINTF(...)
#endif

/* yyparse() calls yyerror() on error */
void yyerror (const char *s);

void set_parsing_options(char *buf, size_t siz, Request *parsing_request);

/* yyparse() calls yylex() to get tokens */
extern int yylex();

/* Pointer to the buffer that contains input */
char *parsing_buf;

/* Current position in the buffer */
int parsing_offset;

/* Buffer size */
size_t parsing_buf_siz;

/* Current parsing_request Header Struct */
Request *parsing_request;

%}

/* Define the types of values that can be returned from lex */
%union {
	char str[8192];
	int i;
}

%start request

/* Define tokens for terminal symbols */
%token t_crlf t_backslash t_slash t_digit t_dot t_token_char t_lws t_colon t_separators t_sp t_ws t_ctl

/* Define types for non-terminal symbols */
%type<str> token
%type<str> text
%type<str> ows

%%

/* Bison grammar rules section */

allowed_char_for_token:
    t_token_char
    | t_digit
    | t_dot
    ;

token:
    allowed_char_for_token
    | token allowed_char_for_token
    ;

allowed_char_for_text:
    allowed_char_for_token
    | t_separators
    | t_colon
    | t_slash
    ;

text:
    allowed_char_for_text
    | text ows allowed_char_for_text
    ;

ows:
    /* empty */
    | t_sp
    | t_ws
    ;

request_line:
    token t_sp text t_sp text t_crlf
    {
        YPRINTF("request_Line:\n%s\n%s\n%s\n", $1, $3, $5);
        strcpy(parsing_request->http_method, $1);
        strcpy(parsing_request->http_uri, $3);
        strcpy(parsing_request->http_version, $5);
    }
    ;

request_header:
    token ows t_colon ows text ows t_crlf
    {
        YPRINTF("request_Header:\n%s\n%s\n", $1, $5);
        strcpy(parsing_request->headers[parsing_request->header_count].header_name, $1);
        strcpy(parsing_request->headers[parsing_request->header_count].header_value, $5);
        parsing_request->header_count++;
    }
    ;

request:
    request_line request_headers t_crlf
    {
        YPRINTF("Complete HTTP Request Parsed Successfully.\n");
        return SUCCESS;
    }
    ;

request_headers:
    /* empty */
    {
        YPRINTF("No headers present.\n");
    }
    | request_headers request_header
    {
        // Incrementally parse each header.
        YPRINTF("Header Parsed.\n");
    }
    ;

%%

/* Additional C code */

void set_parsing_options(char *buf, size_t siz, Request *request)
{
    parsing_buf = buf;
    parsing_offset = 0;
    parsing_buf_siz = siz;
    parsing_request = request;
}

void yyerror (const char *s) { fprintf(stderr, "%s\n", s); }
