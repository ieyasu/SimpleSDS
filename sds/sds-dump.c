/* sds-dump.c - prints human- and script-readable parts of supported SDS files.
 */
#include <sds.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_DIMS 32

static const int TYPE_COLOR    = 16; // bright cyan
static const int ATTNAME_COLOR = 3; // yellow
static const int VARNAME_COLOR = 2; // green
static const int DIMNAME_COLOR = 5; // magenta
static const int VALUE_COLOR   = 14; // blue
static const int QUOTE_COLOR   = 4;

enum OutputType {
    FULL_SUMMARY,
    LIST_DIMS,
    LIST_VARS,
    LIST_ATTS,
    LIST_DIM_SIZES,
    PRINT_ATTS,
    PRINT_VAR
};

enum DimStyle {
    C_DIM_STYLE,
    FORTRAN_DIM_STYLE
};

struct OutOpts {
    char *infile;
    int color;
    int single_column;
    char *separator;
    enum DimStyle dim_style;
    enum OutputType out_type;
    char *name; // dim, var, etc. to narrow output to
    const char *att;
    int ranges[MAX_DIMS][2], n_ranges;
};

static struct OutOpts opts = {
    .infile = NULL, .color = 0, .single_column = 0, .separator = " ",
    .dim_style = FORTRAN_DIM_STYLE, .out_type = FULL_SUMMARY,
    .name = NULL, .att = NULL, .n_ranges = -1
};

#ifndef S_ISLNK
#  define S_ISLNK(mode) (((mode) & S_IFLNK) != 0)
#endif

static int file_exists(const char *path)
{
    struct stat buf;
    if (stat(path, &buf)) {
        return 0; // error stat()ing so, not a file for our purposes
    }
    if ((S_ISREG(buf.st_mode) || S_ISLNK(buf.st_mode))) {
        return 1;
    }
    return 0;
}

/* Print ANSI terminal color escape sequence.
 * Color: 0 - black, 1 - red, 2 - green, 3 - yellow, 4 - blue, 5 - dark magenta,
 *        6 - cyan, 7 - white; 10 + any previous color turns on bold.
 */
static void fesc_color(int color, FILE *fout)
{
    if (opts.color) {
        if (color < 10) {
            fprintf(fout, "\033[%im", 30 + color);
        } else {
            fprintf(fout, "\033[%i;1m", 20 + color);
        }
    }
}

static void esc_color(int color)
{
    fesc_color(color, stdout);
}

static void fesc_bold(FILE *fout)
{
    if (opts.color)
        fputs("\033[1m", fout);
}

static void esc_bold(void)
{
    fesc_bold(stdout);
}

static void fesc_stop(FILE *fout)
{
    if (opts.color)
        fputs("\033[0m", fout);
}

static void esc_stop(void)
{
    fesc_stop(stdout);
}

static void print_type(SDSType type, int min_width)
{
    esc_color(TYPE_COLOR);
    const char *s = sds_type_names[type];
    int w = strlen(s);
    fputs(s, stdout);
    for (; w < min_width; w++) {
        putc(' ', stdout);
    }
    esc_stop();
}

static void print_value(SDSType type, void *ary, size_t u)
{
    esc_color(VALUE_COLOR);
    switch (type) {
    case SDS_NO_TYPE:
        fputs("?", stdout);
        break;
    case SDS_I8:
        printf("%i", ((int8_t *)ary)[u]);
        break;
    case SDS_U8:
        printf("%u", ((uint8_t *)ary)[u]);
        break;
    case SDS_I16:
        printf("%i", ((int16_t *)ary)[u]);
        break;
    case SDS_U16:
        printf("%u", ((uint16_t *)ary)[u]);
        break;
    case SDS_I32:
        printf("%i", ((int32_t *)ary)[u]);
        break;
    case SDS_U32:
        printf("%u", ((uint32_t *)ary)[u]);
        break;
    case SDS_I64:
        printf("%li", (long)((int64_t *)ary)[u]);
        break;
    case SDS_U64:
        printf("%lu", (unsigned long)((uint64_t *)ary)[u]);
        break;
    case SDS_FLOAT:
        printf("%g", ((float *)ary)[u]);
        break;
    case SDS_DOUBLE:
        printf("%g", ((double *)ary)[u]);
        break;
    case SDS_STRING:
        fprintf(stderr, "asked to print_value(SDS_STRING, ...)!");
        abort();
        break;
    }
    esc_stop();
}

static void print_string_value(const char *s)
{
    esc_color(QUOTE_COLOR);
    fputc('"', stdout);
    esc_color(VALUE_COLOR);
    for (int i = 0; s[i] != '\0'; i++) {
        switch (s[i]) {
        case '\n':
            fputc('\n', stdout);
            esc_color(VALUE_COLOR); // re-color for new line
            break;
        case '"':
            fputc('\\', stdout);
            // FALLTHROUGH intended
        default:
            fputc(s[i], stdout);
            break;
        }
    }
    esc_stop();
    esc_color(QUOTE_COLOR);
    fputc('"', stdout);
    esc_stop();
}

static void print_att_values(SDSAttInfo *att)
{
    if (att->type == SDS_STRING) {
        print_string_value(att->data.v);
    } else { // all other value types
        for (int i = 0;;) {
            print_value(att->type, att->data.v, i);
            if (++i >= att->count)
                break;
            fputs(opts.separator, stdout);
        }
    }
}

static void print_atts(SDSAttInfo *att)
{
    fputs("  ", stdout);
    print_type(att->type, 7);
    esc_color(ATTNAME_COLOR);
    fputs(att->name, stdout);
    esc_stop();

    // string length
    if (att->type == SDS_STRING) {
        switch (opts.dim_style) {
        case C_DIM_STYLE:
            printf("[%u]", (unsigned)(att->count - 1));
            break;
        case FORTRAN_DIM_STYLE:
            printf("(%u)", (unsigned)(att->count - 1));
            break;
        }
    }

    fputs(" = ", stdout);

    print_att_values(att);

    puts("");
    if (att->next)
        print_atts(att->next);
}

static void print_full_summary(SDSInfo *sds)
{
    opts.separator = ", ";

    esc_bold();
    fputs(sds->path, stdout);
    esc_stop();
    printf(": %s format\n  ", sds_file_types[(int)sds->type]);

    esc_color(ATTNAME_COLOR);
    printf("%u global attributes",
           (unsigned)sds_list_count((SDSList *)sds->gatts));
    esc_stop();
    fputs(", ", stdout);
    esc_color(DIMNAME_COLOR);
    printf("%u dimensions", (unsigned)sds_list_count((SDSList *)sds->dims));
    esc_stop();
    fputs(", ", stdout);
    esc_color(VARNAME_COLOR);
    printf("%u variables\n", (unsigned)sds_list_count((SDSList *)sds->vars));
    esc_stop();

    if (sds->gatts) {
        puts("\nGlobal attributes:");
        print_atts(sds->gatts);
        puts("");
    } else {
        puts("\n - no global attributes -\n");
    }

    puts("Dimensions:");
    SDSDimInfo *dim = sds->dims;
    while (dim) {
        fputs("  ", stdout);
        esc_color(DIMNAME_COLOR);
        fputs(dim->name, stdout);
        esc_stop();

        fputs(" = ", stdout);
        esc_color(VALUE_COLOR);
        printf("%u", (unsigned)dim->size);
        esc_stop();

        puts(dim->isunlim ? " (unlimited)" : "");

        dim = dim->next;
    }

    fputs("\nVariables:\n", stdout);
    SDSVarInfo *var = sds->vars;
    while (var) {
        puts("");
        print_type(var->type, -1);
        putc(' ', stdout);
        esc_color(VARNAME_COLOR);
        fputs(var->name, stdout);
        esc_stop();
        switch (opts.dim_style) {
        case C_DIM_STYLE:
            for (int i = 0; i < var->ndims; i++) {
                putc('[', stdout);
                esc_color(DIMNAME_COLOR);
                fputs(var->dims[i]->name, stdout);
                esc_stop();
                printf("=%u]", (unsigned)var->dims[i]->size);
            }
            break;
        case FORTRAN_DIM_STYLE:
            putc('(', stdout);
            for (int i = var->ndims - 1; i >= 0; i--) {
                esc_color(DIMNAME_COLOR);
                fputs(var->dims[i]->name, stdout);
                esc_stop();
                printf("=%u", (unsigned)var->dims[i]->size);
                if (i > 0)
                    putc(',', stdout);
            }
            putc(')', stdout);
            break;
        }
        if (var->iscoord)
            fputs(" (coordinate)\n", stdout);
        else
            puts("");
        if (var->atts)
            print_atts(var->atts);

        var = var->next;
    }
    puts("");
}

static SDSVarInfo *var_or_die(SDSInfo *sds, const char *varname)
{
    SDSVarInfo *var = sds_var_by_name(sds->vars, varname);
    if (!var) {
        fesc_bold(stderr);
        fputs(opts.infile, stderr);
        fesc_stop(stderr);
        fputs(": no variable '", stderr);
        fesc_color(VARNAME_COLOR, stderr);
        fputs(varname, stderr);
        fesc_stop(stderr);
        fputs("' found\n", stderr);

        exit(-3);
    }
    return var;
}

static void print_list_atts(SDSInfo *sds)
{
    SDSAttInfo *att = sds->gatts;
    if (opts.name) {
        att = var_or_die(sds, opts.name)->atts;
    }

    for (; att != NULL; att = att->next) {
        esc_color(ATTNAME_COLOR);
        fputs(att->name, stdout);
        esc_stop();
        fputs(opts.separator, stdout);
    }
    if (!opts.single_column)
        puts("");
}

static void print_dim(SDSDimInfo *dim)
{
    esc_color(DIMNAME_COLOR);
    fputs(dim->name, stdout);
    esc_stop();
    fputs(opts.separator, stdout);
}

static void print_list_dims(SDSInfo *sds)
{
    if (opts.name) {
        SDSVarInfo *var = var_or_die(sds, opts.name);
        for (int i = 0; i < var->ndims; i++)
            print_dim(var->dims[i]);
    } else {
        for (SDSDimInfo *dim = sds->dims; dim != NULL; dim = dim->next)
            print_dim(dim);
    }
    if (!opts.single_column)
        puts("");
}

static void print_list_vars(SDSVarInfo *vars)
{
    for (SDSVarInfo *var = vars; var != NULL; var = var->next) {
        esc_color(VARNAME_COLOR);
        fputs(var->name, stdout);
        esc_stop();
        fputs(opts.separator, stdout);
    }
    if (!opts.single_column)
        puts("");
}

static void print_dim_sizes(SDSInfo *sds)
{
    for (SDSDimInfo *dim = sds->dims; dim != NULL; dim = dim->next) {
        esc_color(VALUE_COLOR);
        printf("%u", (unsigned)dim->size);
        esc_stop();
        fputs(opts.separator, stdout);
    }
    if (!opts.single_column)
        puts("");
}

static void print_var_dim_sizes(SDSInfo *sds)
{
    SDSVarInfo *var = var_or_die(sds, opts.name);

    for (int i = 0; i < var->ndims; i++) {
        esc_color(VALUE_COLOR);
        printf("%u", (unsigned)var->dims[i]->size);
        esc_stop();
        fputs(opts.separator, stdout);
    }
    if (!opts.single_column)
        puts("");
}

static void print_atts_values(SDSInfo *sds)
{
    SDSAttInfo *atts = sds->gatts;
    if (opts.name) { // narrow to a var
        atts = var_or_die(sds, opts.name)->atts;
    }

    if (opts.att) { // narrow to an attribute
        SDSAttInfo *att = sds_att_by_name(atts, opts.att);
        if (!att) {
            fesc_bold(stderr);
            fputs(opts.infile, stderr);
            fesc_stop(stderr);
            fputs(": ", stderr);
            if (!opts.name)
                fputs("global ", stderr);
            fputs("attribute '", stderr);
            fesc_color(ATTNAME_COLOR, stderr);
            fputs(opts.att, stderr);
            fesc_stop(stderr);
            if (opts.name) {
                fputs("' not found for variable '", stderr);
                fesc_color(VARNAME_COLOR, stderr);
                fputs(opts.name, stderr);
                fesc_stop(stderr);
                fputs("'\n", stderr);
            } else {
                fputs("' not found\n", stderr);
            }
            exit(-4);
        }
        print_att_values(att);
    } else {
        while (atts) {
            print_att_values(atts);
            fputs(opts.separator, stdout);
            atts = atts->next;
        }
    }
    puts("");
}

static void print_some_values(SDSType type, void *values, size_t count)
{
    for (size_t u = 0;;) {
        print_value(type, values, u);
        if (++u >= count)
            break;
        fputs(opts.separator, stdout);
    }
}

static void parse_error(const char *pos, const char *msg)
{
    // patch up expr with '[' or '('
    size_t o = strlen(opts.name);
    opts.name[o] = '[';
    size_t l = strlen(opts.name);
    if (opts.name[l-1] == ')')
        opts.name[o] = '(';

    fprintf(stderr, "in %s\n", opts.name);
    for (int i = (pos - opts.name) + 3; i > 0; i--)
        fputc(' ', stderr);
    fprintf(stderr, "^\nparse error: %s\n", msg);

    exit(-1);
}

static char *skip_ws(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

/* parse one dimension of a range expression.
 * [START] ':' [END] | START
 * stop - the character(s) to stop parsing at
 */
static char *parse_one_range(char *s)
{
    char *nxt;

    s = skip_ws(s);

    if ('0' <= *s && *s <= '9') { // have a start
        opts.ranges[opts.n_ranges][0] = strtol(s, &nxt, 0);
        s = skip_ws(nxt);
    } else {
        opts.ranges[opts.n_ranges][0] = -1;
    }

    if (*s == ':') {
        s = skip_ws(s + 1);
    } else if (opts.ranges[opts.n_ranges][0] >= 0) {
        opts.ranges[opts.n_ranges][1] = opts.ranges[opts.n_ranges][0];
        goto finish;
    } else {
        parse_error(s, "expected a number or ':'");
    }

    if ('0' <= *s && *s <= '9') { // have an end
        opts.ranges[opts.n_ranges][1] = strtol(s, &nxt, 0);
        s = skip_ws(nxt);
    } else {
        opts.ranges[opts.n_ranges][1] = -1;
    }

    if (opts.ranges[opts.n_ranges][0] >= 0 &&
        opts.ranges[opts.n_ranges][1] >= 0 &&
        opts.ranges[opts.n_ranges][0] > opts.ranges[opts.n_ranges][1]) {
        parse_error(s, "start of range must be less than or equal to end");
    }

 finish:
    opts.n_ranges++;
    return s;
}

// "[...][...]", 0-based
//   ^
static void parse_c_range(char *s)
{
    for (opts.n_ranges = 0; opts.n_ranges < MAX_DIMS;) {
        s = parse_one_range(s);
        if (*s == ']') {
            s = skip_ws(s + 1);
        } else {
            parse_error(s, "expected ']'");
        }
        fprintf(stderr, "got c range [%i:%i]\n", opts.ranges[opts.n_ranges-1][0], opts.ranges[opts.n_ranges-1][1]);

        if (*s == '\0') { // end of string
            return;
        } else if (*s == '[') { // start of next dim
            s = skip_ws(s + 1);
            // and continue looping
        } else {
            parse_error(s, "expected '[' or end of range");
        }
    }
    parse_error(s, "too many dimensions!");
}

// "(...,...)", 1-based
//   ^
static void parse_fortran_range(char *s)
{
    for (opts.n_ranges = 0; opts.n_ranges < MAX_DIMS;) {
        s = parse_one_range(s);
        fprintf(stderr, "got fortran range (%i:%i)\n", opts.ranges[opts.n_ranges-1][0], opts.ranges[opts.n_ranges-1][1]);
        if (*s == ',') {
            s = skip_ws(s + 1);
            // and continue looping to next dim
        } else if (*s == ')') {
            s = skip_ws(s + 1);
            if (*s == '\0') {
                break; // end of range expression
            } else {
                parse_error(s, "unexpected text after ')'");
            }
        } else {
            parse_error(s, "expected ',' or ')'");
        }
    }

    if (opts.n_ranges >= MAX_DIMS) {
        parse_error(s, "too many dimensions!");
    }

    // make 0-based
    for (int i = 0; i < opts.n_ranges; i++) {
        for (int j = 0; j < 2; j++) {
            if (opts.ranges[i][j] > 0) {
                opts.ranges[i][j]--;
            } else if (opts.ranges[i][j] == 0) {
                fprintf(stderr, "in dimension %i of range: cannot start indexes with 0!\n", i);
                exit(-1);
            }
        }
    }

    // reverse range order
    for (int i = 0, j = opts.n_ranges - 1; i < j; i++, j--) {
        int a = opts.ranges[i][0];
        int b = opts.ranges[i][1];
        opts.ranges[i][0] = opts.ranges[j][0];
        opts.ranges[i][1] = opts.ranges[j][1];
        opts.ranges[j][0] = a;
        opts.ranges[j][1] = b;
    }
}

static void parse_var_range(void)
{
    int l = (int)strlen(opts.name) - 1;
    if (l <= 2)
        return;

    if (opts.name[l] == ')') {
        char *opar = strchr(opts.name, '(');
        if (opar) {
            *opar = '\0';
            parse_fortran_range(opar + 1);
        }
    } else if (opts.name[l] == ']') {
        char *obracket = strchr(opts.name, '[');
        if (obracket) {
            *obracket = '\0';
            parse_c_range(obracket + 1);
        }
    }

    l = strlen(opts.name) - 1;
    while (l > 1 && (opts.name[l] == ' ' || opts.name[l] == '\t')) {
        opts.name[l--] = '\0';
    }
}

static void validate_ranges(SDSVarInfo *var)
{
    int invalid = 0;

    if (var->ndims != opts.n_ranges) {
        fprintf(stderr, "Variable %s has %i dimensions, but got %i in the range\n",
                var->name, var->ndims, opts.n_ranges);
        invalid = -1;
    }

    for (int i = 0; i < var->ndims; i++) {
        if (opts.ranges[i][0] > var->dims[i]->size) {
            fprintf(stderr, "Variable %s dimension %i range starts too high (%i > %u)\n",
                    var->name, i+1, opts.ranges[i][0], (unsigned)var->dims[i]->size);
            invalid = -1;
        }
        if (opts.ranges[i][1] > var->dims[i]->size) {
            fprintf(stderr, "Variable %s dimension %i range ends past actual end (%i > %u)\n",
                    var->name, i+1, opts.ranges[i][1], (unsigned)var->dims[i]->size);
            invalid = -1;
        }
    }

    if (invalid)
        exit(-1);
}

static void print_var_values(SDSInfo *sds)
{
    parse_var_range();
    SDSVarInfo *var = var_or_die(sds, opts.name);

    if (opts.n_ranges > 0) {
        validate_ranges(var);
    } else {
        if (var->ndims > MAX_DIMS) {
            fprintf(stderr, "too many dims! (%i %s)\n", var->ndims, var->name);
            abort();
        }
        // fill out ranges so they match the full variable
        for (size_t u = 0; u < var->ndims; u++) {
            opts.ranges[u][0] = 0;
            opts.ranges[u][1] = (int)var->dims[u]->size;
        }
    }

    // XXX use opts.ranges to subset var (or not)

    size_t count_per_tstep = 1;
    for (int i = 1; i < var->ndims; i++)
        count_per_tstep *= var->dims[i]->size;

    void *buf = NULL;
    if (var->ndims > 1) {
        for (int tstep = 0; tstep < var->dims[0]->size; tstep++) {
            void *values = sds_timestep(var, &buf, tstep);
            print_some_values(var->type, values, count_per_tstep);
            fputs(opts.separator, stdout);
        }
    } else {
        void *values = sds_read(var, &buf);
        if (var->ndims == 0) {
            print_value(var->type, values, 0);
        } else {
            print_some_values(var->type, values, var->dims[0]->size);
        }
    }
    sds_buffer_free(buf);

    if (!opts.single_column)
        puts("");
}

static const char *USAGE =
    "Usage: %s [OPTION]... INFILE\n"
    "Dumps part or all of INFILE, producing a colorful summary of its contents\n"
    "by default.\n"
    "\n"
    "Options:\n"
    "  -1             output values in a single column\n"
    "  -a [VAR][@ATT] prints attribute values.  If a variable name is given, that\n"
    "                 variable is selected instead of global attributes.  If an\n"
    "                 attribute name is given (identified with the '@'), just that\n"
    "                 attribute's value(s) are printed.\n"
    "  -c             print variable dimensions in C order and format\n"
    "  -d [VAR]       print dimension sizes for the whole file or the specified\n"
    "                 variable, if given\n"
    "  -f             print variable dimensions in Fortran order and format\n"
    "                 (default)\n"
    "  -g             never color the output\n"
    "  -G             always color the output\n"
    "  -h             print this help and exit\n"
    "  -la [VAR]      list the attributes in the file or for the specified\n"
    "                 variable if given\n"
    "  -ld [VAR]      list the dimensions in the file or for the specified\n"
    "                 variable if given\n"
    "  -lv            list the variables in the file\n"
    "  -v VAR         print the specified variable's values\n"
    "  -v VAR[RANGE]\n"
    "  -v VAR(RANGE)  print a subset of the specified variable's values\n"
    "\n"
    "Where RANGE is an expression in one of two forms.  A Fortran-style range uses parentheses and looks like '(1:3,:6,:)'; an equivalent C-style range uses square brackets and looks like '[0:2][:5][:]'."
;

static void usage(const char *progname, const char *message, ...)
{
    char *pname = strrchr(progname, '/');
    if (pname) {
        pname++;
    } else {
        pname = (char *)progname;
    }
    fprintf(stderr, "%s: ", pname);

    va_list ap;
    va_start(ap, message);
    vfprintf(stderr, message, ap);
    va_end(ap);
    fputs("\n", stderr);

    fprintf(stderr, USAGE, pname);
    exit(-1);
}

/* For command line options that take optional non-file-name arguments
 * (typically variable names), return the name if given and NULL otherwise.
 * Increments ip if the optional argument is found.
 */
static char *get_optional_arg(int argc, char **argv, int *ip)
{
    if (*ip + 1 >= argc) {
        return NULL; // past end of argv
    }
    char *arg = argv[*ip + 1];
    if (arg[0] != '-' && !file_exists(arg)) {
        (*ip)++;
        return arg; // looks like a non-file argument!
    }
    return NULL;
}

static void parse_arg(int argc, char **argv, int *ip)
{
    char *opt = argv[*ip]+1;

    if (!strcmp(opt, "1")) { // single-column output mode
        opts.single_column = 1;
        opts.separator = "\n";
    } else if (!strcmp(opt, "a")) { // print attribute values
        opts.out_type = PRINT_ATTS;
        char *s = get_optional_arg(argc, argv, ip);
        if (s) {
            char *at = strchr(s, '@');
            if (at) {
                *at = '\0';
                opts.att = at+1;
            }
            if (strlen(s) > 0) {
                opts.name = s;
            }
        }
    } else if (!strcmp(opt, "c")) { // C-style var dimensions
        opts.dim_style = C_DIM_STYLE;
    } else if (!strcmp(opt, "d")) { // list dim sizes (for var)
        opts.out_type = LIST_DIM_SIZES;
        opts.name = get_optional_arg(argc, argv, ip);
    } else if (!strcmp(opt, "f")) { // Fortran-style var dimensions
        opts.dim_style = FORTRAN_DIM_STYLE;
    } else if (!strcmp(opt, "g")) { // force color off
        opts.color = 0;
    } else if (!strcmp(opt, "G")) { // force color on
        opts.color = 1;
    } else if (!strcmp(opt, "h")) { // help
        printf(USAGE, argv[0]);
        exit(0);
    } else if (!strcmp(opt, "la")) { // list atts (for var)
        opts.out_type = LIST_ATTS;
        opts.name = get_optional_arg(argc, argv, ip);
    } else if (!strcmp(opt, "ld")) { // list dims (for var)
        opts.out_type = LIST_DIMS;
        opts.name = get_optional_arg(argc, argv, ip);
    } else if (!strcmp(opt, "lv")) { // list vars
        opts.out_type = LIST_VARS;
    } else if (!strcmp(opt, "v")) { // print var's values
        opts.out_type = PRINT_VAR;
        opts.name = get_optional_arg(argc, argv, ip);
        if (!opts.name)
            usage(argv[0], "missing variable name argument to -v");
    } else {
        usage(argv[0], "unrecognized command line option '%s'", argv[*ip]);
    }
}

static void parse_args(int argc, char **argv)
{
    if (isatty(STDOUT_FILENO))
        opts.color = 1; // default to color when printing to the terminal

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') { // parse the option
            parse_arg(argc, argv, &i);
        } else { // this is the infile
            if (opts.infile) {
                usage(argv[0], "only one input file is allowed");
            }
            opts.infile = argv[i];
        }
    }

    if (!opts.infile) {
        usage(argv[0], "you need to specify an input file");
    }
}

int main(int argc, char **argv)
{
    parse_args(argc, argv);

    SDSInfo *sds = sds_open(opts.infile);
    if (!sds) {
        fesc_bold(stderr);
        fputs(opts.infile, stderr);
        fesc_stop(stderr);
        fputs(": error opening file\n", stderr);
        return -2;
    }

    switch (opts.out_type) {
    case FULL_SUMMARY:
        print_full_summary(sds);
        break;
    case LIST_ATTS:
        print_list_atts(sds);
        break;
    case LIST_DIMS:
        print_list_dims(sds);
        break;
    case LIST_VARS:
        print_list_vars(sds->vars);
        break;
    case LIST_DIM_SIZES:
        if (opts.name)
            print_var_dim_sizes(sds);
        else
            print_dim_sizes(sds);
        break;
    case PRINT_ATTS:
        print_atts_values(sds);
        break;
    case PRINT_VAR:
        print_var_values(sds);
        break;
    default:
        abort();
    }

    sds_close(sds);

    return 0;
}
