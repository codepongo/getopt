/*
* getopt.c - Enhanced implementation of BSD getopt(1)
* Copyright (c) 1997-2005 Frodo Looijaard <frodo@frodo.looijaard.name>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along
* with this program; if not, write to the Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

/*
* Version 1.0-b4: Tue Sep 23 1997. First public release.
* Version 1.0: Wed Nov 19 1997.
* Bumped up the version number to 1.0
* Fixed minor typo (CSH instead of TCSH)
* Version 1.0.1: Tue Jun 3 1998
* Fixed sizeof instead of _tcslen bug
* Bumped up the version number to 1.0.1
* Version 1.0.2: Thu Jun 11 1998 (not present)
* Fixed gcc-2.8.1 warnings
* Fixed --version/-V option (not present)
* Version 1.0.5: Tue Jun 22 1999
* Make -u option work (not present)
* Version 1.0.6: Tue Jun 27 2000
* No important changes
* Version 1.1.0: Tue Jun 30 2000
* Added NLS support (partly written by Arkadiusz Mi<B6>kiewicz
* <misiek@pld.org.pl>)
* Version 1.1.4: Mon Nov 7 2005
* Fixed a few type's in the manpage
*/

/* Exit codes:
* 0) No errors, successful operation.
* 1) getopt(3) returned an error.
* 2) A problem with parameter parsing for getopt(1).
* 3) Internal error, out of memory
* 4) Returned for -T
*/
#define GETOPT_EXIT_CODE 1
#define PARAMETER_EXIT_CODE 2
#define XALLOC_EXIT_CODE 3
#define TEST_EXIT_CODE 4

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <unistd.h>
#include <tchar.h>
#include <ctype.h>
#include "getopt.h"

//#include "closestream.h"
//#include "nls.h"
//#include "xalloc.h"
#define xmalloc (TCHAR*)malloc
#define xrealloc realloc
#define warnx _tprintf

/* NON_OPT is the code that is returned when a non-option is found in '+'
* mode */
#define NON_OPT 1
/* LONG_OPT is the code that is returned when a long option is found. */
#define LONG_OPT 2

/* The shells recognized. */
typedef enum { BASH, TCSH } shell_t;


/* Some global variables that tells us how to parse. */
static shell_t shell = BASH;	/* The shell we generate output for. */
static int quiet_errors = 0;	/* 0 is not quiet. */
static int quiet_output = 0;	/* 0 is not quiet. */
static int quote = 1;	/* 1 is do quote. */

/* Allow changing which getopt is in use with function pointer */
int (*getopt_long_fp) (int argc, TCHAR *const *argv, const TCHAR *optstr,
	const struct option * longopts, int *longindex);

/* Function prototypes */
static const TCHAR *normalize(const TCHAR *arg);
static int generate_output(TCHAR *argv[], int argc, const TCHAR *optstr,
	const struct option *longopts);
static void parse_error(const TCHAR *message);
static void add_long_options(TCHAR *options);
static void add_longopt(const TCHAR *name, int has_arg);
static void print_help(void);
static void set_shell(const TCHAR *new_shell);

/*
* This function 'normalizes' a single argument: it puts single quotes
* around it and escapes other special characters. If quote is false, it
* just returns its argument.
*
* Bash only needs special treatment for single quotes; tcsh also recognizes
* exclamation marks within single quotes, and nukes whitespace. This
* function returns a pointer to a buffer that is overwritten by each call.
*/
static const TCHAR *normalize(const TCHAR *arg)
{
	static TCHAR *BUFFER = NULL;
	const TCHAR *argptr = arg;
	TCHAR *bufptr;

	if (!quote) {
		/* Just copy arg */
		BUFFER = xmalloc(wcslen(arg) + 1);
		wcscpy(BUFFER, arg);
		return BUFFER;
	}

	/*
	* Each character in arg may take up to four characters in the
	* result: For a quote we need a closing quote, a backslash, a quote
	* and an opening quote! We need also the global opening and closing
	* quote, and one extra character for '\0'.
	*/
	BUFFER = xmalloc(wcslen(arg) * 4 + 3);

	bufptr = BUFFER;
	*bufptr++ = '\'';

	while (*argptr) {
		if (*argptr == '\'') {
			/* Quote: replace it with: '\'' */
			*bufptr++ = '\'';
			*bufptr++ = '\\';
			*bufptr++ = '\'';
			*bufptr++ = '\'';
		} else if (shell == TCSH && *argptr == '!') {
			/* Exclamation mark: replace it with: \! */
			*bufptr++ = '\'';
			*bufptr++ = '\\';
			*bufptr++ = '!';
			*bufptr++ = '\'';
		} else if (shell == TCSH && *argptr == '\n') {
			/* Newline: replace it with: \n */
			*bufptr++ = '\\';
			*bufptr++ = 'n';
		} else if (shell == TCSH && isspace(*argptr)) {
			/* Non-newline whitespace: replace it with \<ws> */
			*bufptr++ = '\'';
			*bufptr++ = '\\';
			*bufptr++ = *argptr;
			*bufptr++ = '\'';
		} else
			/* Just copy */
			*bufptr++ = *argptr;
		argptr++;
	}
	*bufptr++ = '\'';
	*bufptr++ = '\0';
	return BUFFER;
}

/*
* Generate the output. argv[0] is the program name (used for reporting errors).
* argv[1..] contains the options to be parsed. argc must be the number of
* elements in argv (ie. 1 if there are no options, only the program name),
* optstr must contain the short options, and longopts the long options.
* Other settings are found in global variables.
*/
static int generate_output(TCHAR *argv[], int argc, const TCHAR *optstr,
	const struct option *longopts)
{
	int exit_code = EXIT_SUCCESS;	/* Assume everything will be OK */
	int opt;
	int longindex;
	const TCHAR *charptr;

	if (quiet_errors)
		/* No error reporting from getopt(3) */
		opterr = 0;
	/* Reset getopt(3) */
	optind = 0;

	while ((opt =
		(getopt_long_fp(argc, argv, optstr, longopts, &longindex)))
		!= EOF)
		if (opt == '?' || opt == ':')
			exit_code = GETOPT_EXIT_CODE;
		else if (!quiet_output) {
			if (opt == LONG_OPT) {
				_tprintf(L" --%s", longopts[longindex].name);
				if (longopts[longindex].has_arg)
					_tprintf(L" %s", normalize(optarg ? optarg : L""));
			} else if (opt == NON_OPT)
				_tprintf(L" %s", normalize(optarg));
			else {
				_tprintf(L" -%c", opt);
				charptr = _tcschr(optstr, opt);
				if (charptr != NULL && *++charptr == ':')
					_tprintf(L" %s", normalize(optarg ? optarg : L""));
			}
		}

		if (!quiet_output) {
			_tprintf(L" --");
			while (optind < argc)
				_tprintf(L" %s", normalize(argv[optind++]));
			_tprintf(L"\n");
		}
		return exit_code;
}

/*
* Report an error when parsing getopt's own arguments. If message is NULL,
* we already sent a message, we just exit with a helpful hint.
*/
static void parse_error(const TCHAR *message)
{
	if (message)
		warnx(L"%s", message);
	fprintf(stderr, "Try `%s --help' for more information.\n",
		"getopt");
	exit(PARAMETER_EXIT_CODE);
}

static struct option *long_options = NULL;
static int long_options_length = 0;	/* Length of array */
static int long_options_nr = 0;	/* Nr of used elements in array */
#define LONG_OPTIONS_INCR 10
#define init_longopt() add_longopt(NULL,0)

/* Register a long option. The contents of name is copied. */
static void add_longopt(const TCHAR *name, int has_arg)
{
	TCHAR *tmp;
	if (!name) {
		/* init */
		free(long_options);
		long_options = NULL;
		long_options_length = 0;
		long_options_nr = 0;
	}

	if (long_options_nr == long_options_length) {
		long_options_length += LONG_OPTIONS_INCR;
		long_options = (struct option*)xrealloc(long_options,
			sizeof(struct option) *
			long_options_length);
	}

	long_options[long_options_nr].name = NULL;
	long_options[long_options_nr].has_arg = 0;
	long_options[long_options_nr].flag = NULL;
	long_options[long_options_nr].val = 0;

	if (long_options_nr && name) {
		/* Not for init! */
		long_options[long_options_nr - 1].has_arg = has_arg;
		long_options[long_options_nr - 1].flag = NULL;
		long_options[long_options_nr - 1].val = LONG_OPT;
		tmp = xmalloc(_tcslen(name)+1);
		_tcscpy(tmp, name);
		long_options[long_options_nr - 1].name = tmp;
	}
	long_options_nr++;
}


/*
* Register several long options. options is a string of long options,
* separated by commas or whitespace. This nukes options!
*/
static void add_long_options(TCHAR *options)
{
	int arg_opt;
	TCHAR *tokptr = _tcstok(options, L", \t\n");
	while (tokptr) {
		arg_opt = no_argument;
		if (_tcslen(tokptr) > 0) {
			if (tokptr[_tcslen(tokptr) - 1] == ':') {
				if (tokptr[_tcslen(tokptr) - 2] == ':') {
					tokptr[_tcslen(tokptr) - 2] = '\0';
					arg_opt = optional_argument;
				} else {
					tokptr[_tcslen(tokptr) - 1] = '\0';
					arg_opt = required_argument;
				}
				if (_tcslen(tokptr) == 0)
					parse_error(L"empty long option after "
					L"-l or --long argument");
			}
			add_longopt(tokptr, arg_opt);
		}
		tokptr = _tcstok(NULL, L", \t\n");
	}
}

static void set_shell(const TCHAR *new_shell)
{
	if (!_tcscmp(new_shell, L"bash"))
		shell = BASH;
	else if (!_tcscmp(new_shell, L"tcsh"))
		shell = TCSH;
	else if (!_tcscmp(new_shell, L"sh"))
		shell = BASH;
	else if (!_tcscmp(new_shell, L"csh"))
		shell = TCSH;
	else
		parse_error(L"unknown shell after -s or --shell argument");
}

static void print_help(void)
{
	fputs("\nUsage:\n", stderr);

	fprintf(stderr,
		" %1$s optstring parameters\n"
		" %1$s [options] [--] optstring parameters\n"
		" %1$s [options] -o|--options optstring [options] [--] parameters\n",
		"getopt");

	fputs("\nOptions:\n", stderr);
	fputs(" -a, --alternative Allow long options starting with single -\n", stderr);
	fputs(" -h, --help This small usage guide\n", stderr);
	fputs(" -l, --longoptions <longopts> Long options to be recognized\n", stderr);
	fputs(" -n, --name <progname> The name under which errors are reported\n", stderr);
	fputs(" -o, --options <optstring> Short options to be recognized\n", stderr);
	fputs(" -q, --quiet Disable error reporting by getopt(3)\n", stderr);
	fputs(" -Q, --quiet-output No normal output\n", stderr);
	fputs(" -s, --shell <shell> Set shell quoting conventions\n", stderr);
	fputs(" -T, --test Test for getopt(1) version\n", stderr);
	fputs(" -u, --unquote Do not quote the output\n", stderr);
	fputs(" -V, --version Output version information\n", stderr);
	fputc('\n', stderr);

	exit(PARAMETER_EXIT_CODE);
}

int wmain(int argc, TCHAR *argv[])
{
	TCHAR *optstr = NULL;
	TCHAR *name = NULL;
	int opt;
	int compatible = 0;

	/* Stop scanning as soon as a non-option argument is found! */
	static const TCHAR *shortopts = L"+ao:l:n:qQs:TuhV";
	static const struct option longopts[] = {
		{L"options", required_argument, NULL, 'o'},
		{L"longoptions", required_argument, NULL, 'l'},
		{L"quiet", no_argument, NULL, 'q'},
		{L"quiet-output", no_argument, NULL, 'Q'},
		{L"shell", required_argument, NULL, 's'},
		{L"test", no_argument, NULL, 'T'},
		{L"unquoted", no_argument, NULL, 'u'},
		{L"help", no_argument, NULL, 'h'},
		{L"alternative", no_argument, NULL, 'a'},
		{L"name", required_argument, NULL, 'n'},
		{L"version", no_argument, NULL, 'V'},
		{NULL, 0, NULL, 0}
	};

	//setlocale(LC_ALL, "");
	//bindtextdomain(PACKAGE, LOCALEDIR);
	//textdomain(PACKAGE);
	//atexit(close_stdout);

	init_longopt();
	getopt_long_fp = getopt_long;

	if (getenv("GETOPT_COMPATIBLE"))
		compatible = 1;

	if (argc == 1) {
		if (compatible) {
			/*
			* For some reason, the original getopt gave no
			* error when there were no arguments.
			*/
			_tprintf(L" --\n");
			return EXIT_SUCCESS;
		} else
			parse_error(L"missing optstring argument");
	}

	if (argv[1][0] != '-' || compatible) {
		quote = 0;
		optstr = (TCHAR*)xmalloc(_tcslen(argv[1]) + 1);
		_tcscpy(optstr, argv[1] + _tcsspn(argv[1], L"-+"));
		argv[1] = argv[0];
		return generate_output(argv + 1, argc - 1, optstr,
			long_options);
	}

	while ((opt =
		getopt_long(argc, argv, shortopts, longopts, NULL)) != EOF)
		switch (opt) {
		case 'a':
			getopt_long_fp = getopt_long_only;
			break;
		case 'h':
			print_help();
		case 'o':
			free(optstr);
			optstr = xmalloc(_tcslen(optarg) + 1);
			_tcscpy(optstr, optarg);
			break;
		case 'l':
			add_long_options(optarg);
			break;
		case 'n':
			free(name);
			name = xmalloc(_tcslen(optarg) + 1);
			_tcscpy(name, optarg);
			break;
		case 'q':
			quiet_errors = 1;
			break;
		case 'Q':
			quiet_output = 1;
			break;
		case 's':
			set_shell(optarg);
			break;
		case 'T':
			return TEST_EXIT_CODE;
		case 'u':
			quote = 0;
			break;
		case 'V':
			_tprintf(L"%s from %s\n",
				"getopt",
				"eb");
			return EXIT_SUCCESS;
		case '?':
		case ':':
			parse_error(NULL);
		default:
			parse_error(L"internal error, contact the author.");
	}

	if (!optstr) {
		if (optind >= argc)
			parse_error(L"missing optstring argument");
		else {
			optstr = xmalloc(_tcslen(argv[optind]) + 1);
			_tcscpy(optstr, argv[optind]);
			optind++;
		}
	}
	if (name)
		argv[optind - 1] = name;
	else
		argv[optind - 1] = argv[0];

	return generate_output(argv + optind - 1, argc-optind + 1,
		optstr, long_options);
}
