/**
 * This is an example of integrate.c seen on the previous lectures with some features added :
 * 1. Argument processing is now done using getopt
 *    Other commonly used alternatives to getopt are:
 *      - Argp (https://www.gnu.org/software/libc/manual/html_node/Argp.html). We will utilize it
 *        in further examples
 *      - GApplication from the GIO lib (https://developer.gnome.org/gio/2.64/application.html).
 *        It has higer level of abstraction and involves a lot of stuff like Glib and Gobject.
 *        We won't cover it in details, but will see how it works together with GUI apps
 *        based on GTK
 *    For now, let us start with the simplest argument processing example -- getopt from libc
 *    (https://www.gnu.org/software/libc/manual/html_node/Getopt.html)
 * 2. Some struct techniques used here. This may seem obscure at first glance,
 *    but the Linux kernel is full of such powerful stuff being used.
 *    Struct inheritance requires to build with -fms-extensions
 * 3. Integrated function is loaded dynamically from a shared library
 *    This allows to have numerous functions built separately with no need to rebuild
 *    the core executable.
 *    This example demonstrates how plugins could be implemented.
 *    Dynamic linkage support at runtime requires to build with -ldl
 * 4. POSIX threads are used in this example. Threads are usually understood as a way of
 *    accomplishing parallelism to speed up the code. Of course, their use is not limited
 *    to providing better performance.
 *    Threading is a way way more complicated than being shown in this example,
 *    proper utilization of threading requires knowledge far beyond current scope
 *    and includes understanding of topics such as how the scheduler works,
 *    how locking mechanisms are implemeted, when and why they are needed,
 *    how to use IPC and inter-thread communications.
 *    Using POSIX threads requires -pthread flag
 * 5. Some simple logging techniques
 *    Build with -DDEBUG=1 if you need logging enabled
 * 
 */

// gcc -DDEBUG=0 -O2 -fms-extensions -std=gnu18 integrate.c -lm -pthread -ldl -o ./int

#define _GNU_SOURCE
#include <dlfcn.h>              /* dynamic loading support           */
#include <float.h>              /* take float epsillon from here     */
#include <libgen.h>             /* used for basename()               */
#include <math.h>               /* used for floating point macrodefs */
#include <pthread.h>            /* POSIX threading                   */
#include <sysexits.h>           /* we want some exit codes from it   */
#include <sys/sysinfo.h>        /* get nproc as reported by sysinfo  */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>               /* timing-related stuff is here      */
#include <unistd.h>             /* getopt() resides here             */

// this is for our function to load
#define FUNC_PREFIX "dlfunc_"

// Simple logging facility
// When compiled with -DDEBUG=1, the logging will appear on stderr
#if defined(DEBUG) && DEBUG
#define log(x, ...) fprintf(stderr, " >> LOG %s @ L%d: " x "\n", __func__, __LINE__,##__VA_ARGS__)
#else
#define log(x, ...)
#endif

#define except(condition, seterr, errptr, gotoptr) \
    {                                              \
        bool cond = (condition);                   \
        if (cond) {                                \
            errptr = (seterr);                     \
            goto gotoptr;                          \
        }                                          \
        cond;                                      \
    }

// Change this type to float, double, long double and see how the consumed time
// changes and don't forget to change sin to sinf or sinl
typedef double farg_t;
typedef farg_t (*funcptr_t)(farg_t);

static const farg_t EPSILON = FLT_EPSILON;

// We want all our errrors structured and will use this for handsome
// error messages declaration below.
//
// Enum is implemented as arbitrary integer type (picked by the compiler),
// But we may want to pin-point compiler which type to pick, e.g.:
//     PE_OK = (unsigned char) 0
//
// The first error code means no error here and is given as 0 explicitly
// so the following ones will have numbers 1, 2, ...
enum error {
    PE_OK = 0,          /* No error                                        */
    PE_NOARGS,          /* No arguments provided                           */
    PE_WRONGARG,        /* Argument has wrong format                       */
    PE_NENARGS,         /* Not enough arguments                            */
    PE_TMARGS,          /* Too much arguments                              */
    PE_CVGERR,          /* Convergence error                               */
    PE_MALLOC,          /* Memory allocation error                         */
    PE_THREAD,          /* Threading error                                 */
    PE_DLERR,           /* Dynamic loader error                            */
    __PE_LAST           /* Last item. So that array sizes match everywhere */
};


// And here we rely on designated init GCC extension
static const char *const error_msg[] = {
    [PE_OK]       = "",
    [PE_NOARGS]   = "No arguments provided",
    [PE_WRONGARG] = "Wrong argument '%s'",
    [PE_NENARGS]  = "Not enough arguments",
    [PE_TMARGS]   = "Too many arguments",
    [PE_CVGERR]   = "Convergence unreachable",
    [PE_MALLOC]   = "Could not allocate memory",
    [PE_THREAD]   = "Threading error",
    [PE_DLERR]    = "Dynamic loader (%s)",
    [__PE_LAST]   = NULL
};


// And we want useful error codes returned
// We could assign these error codes to enum values directly
// but that would have been a waste of space. And also would
// have given us problems when return codes have same values
static const int error_retcodes[] = {
    [PE_OK]       = EXIT_SUCCESS,
    [PE_NOARGS]   = EX_USAGE,         /* sysexits.h: command line usage error */
    [PE_WRONGARG] = EX_USAGE,
    [PE_NENARGS]  = EX_USAGE,
    [PE_TMARGS]   = EX_USAGE,
    [PE_CVGERR]   = EX_DATAERR,       /* sysexits.h: data format error        */
    [PE_MALLOC]   = EX_OSERR,         /* sysexits.h: internal OS error        */
    [PE_THREAD]   = EX_OSERR,
    [PE_DLERR]    = EX_SOFTWARE,      /* sysexits.h: internal software error  */
    [__PE_LAST]   = EXIT_FAILURE
};


// Example of how defaults or other behavior defining stuff could be
// well structured.
// "Namespaces are one honking great idea -- let's do more of those!"
// So, it is better and cleaner than having tons of stuff here and there.
// Another alternative is using global enums
struct defaults {
    long long nsteps;
    long long nthreads;
};

static const struct defaults DEFAULTS = {
    .nsteps = 10e6,
    .nthreads = 1
};


// Here we will store arguments after parsing
struct args {
    struct defaults;    // DRY
    farg_t start;
    farg_t end;
    const char *funcname;
    unsigned int isverbose : 1;
    unsigned int ishelp : 1;
};


// This should still be done manually as the getopt() does not provide any kind of help
// messages (in contrast to more extensive parsers like Argp)
//
// See https://www.gnu.org/software/libc/manual/html_node/Argument-Syntax.html
// for more details on common POSIX-style (or GNU-style) arguments notation
//
// Notice how we're using POSIX format extension here:
// %1$s prints first argument as string. This allows to provide an argument only once
static const char *const USAGE = (
    "Integrate user function using the left squares method.\n\n"
    "Usage: %s [-v] [-t threads] [-n steps] -F funcname start end\n"
    "Try '%1$s -h' for more information\n"
    /* Of course the previous line will end up appearing in -h help -- it is an example */
);

// Don't forget the nsteps default during printing this
// Again, use Argp if you want all this stuff automated
static const char *const HELP = (
    "\n*  The program dynamically loads function " FUNC_PREFIX "funcname given"
    " as -F funcname argument from the " FUNC_PREFIX "funcname.so shared"
    " library and integrates it.\n"
    "*  -v argument if provided results in verbose output with measured"
    " exec time.\n"
    "*  -t argument if provided specifies number of threads (%lld thread%s is"
    " used by default). If given as -t 0, the number of threads will"
    " be automatically picked as the number of CPUs available on the"
    " system, as reported by nproc.\n"
    "*  -n argument if provided specifies number of integration steps"
    " (%lld steps is used by default)\n"
    "*  start and stop arguments are required positionals specifing the"
    " integration interval as [start; stop]\n"
);


// Here by design we could pass everything we need via void *
static void err_handler(enum error err, void *etc)
{
    // Nested function declarations -- GCC extension
    // could allow for good code reuse without namespace collisions
    void report(const char *msg)
    {
        fprintf(stderr, "Error: %s.\n", msg ?: error_msg[err]);
        exit(error_retcodes[err]);
    }

    switch (err) {
    case PE_OK:
        break;
    case PE_WRONGARG: ;
        char *optarg = etc;
        char *errstr;    // this one is allocated
        asprintf(&errstr, error_msg[err], optarg);
        report(errstr);
        // free(errstr);  -- not needed as exit is near
        break;
    case PE_DLERR: ;
        char *dlerrstr;
        // dlerror() provides its own error string containing err info
        asprintf(&dlerrstr, error_msg[err], dlerror());
        report(dlerrstr);
        break;
    case PE_NOARGS: ;
        char *progname = etc;
        fprintf(stderr, USAGE, progname);
        /* fall-through */
    default:
        report(0);
    }
}


static inline bool ld_conv(const char *str, long double *dstptr)
{
    char etc;
    return (sscanf(str, "%Lg%c", dstptr, &etc) == 1) && (isfinite(*dstptr));
}


static long long diff_us(struct timespec t1, struct timespec t2)
{
    return (long long)(t2.tv_sec - t1.tv_sec) * 1000000LL + (t2.tv_nsec - t1.tv_nsec) / 1000;
}


static struct args parse_args(int argc, char *argv[])
{
    int _opterr = opterr;
    opterr = 0;
    // 'struct args' is not the same as 'args'
    struct args args = { DEFAULTS, .funcname = NULL };

    log("Init args:"
        "\n\tnsteps: %lld,\n\tnthreads: %lld,\n\tstart: %Lg,\n\tstop: %Lg,"
        "\n\tfuncname: '%s',\n\tisverbose: %c,\n\tishelp: %c",
        (long long)args.nsteps, (long long)args.nthreads, (long double)args.start,
        (long double)args.end, args.funcname, args.isverbose ? 'T' : 'F',    // notice how
        args.ishelp ? 'T' : 'F');                                            // it is converted

    // Why not while? Not critical, but an example of keeping scope ns clean
    for (int sym; (sym = getopt(argc, argv, "hvt:n:F:")) != -1;) {
        switch (sym) {
        case 'h': /* help: print message and exit peacefully */
            args.ishelp = true;    // just to show
            fprintf(stderr, USAGE, argv[0]);
            fprintf(stderr, HELP, DEFAULTS.nthreads, DEFAULTS.nthreads > 1 ? "s" : "",
                    DEFAULTS.nsteps);
            exit(error_retcodes[PE_OK]);
        case 'v':
            args.isverbose = true;
            break;
        case 't':;
            long double threads;
            if (!ld_conv(optarg, &threads) || ((long long)threads < 0))
                err_handler(PE_WRONGARG, optarg);
            args.nthreads = threads;
            break;
        case 'n':;
            long double steps;
            if (!ld_conv(optarg, &steps) || ((long long)steps <= 0))
                err_handler(PE_WRONGARG, optarg);
            args.nsteps = steps;
            break;
        case 'F': /* func: sets function name */
            // optarg is available globally
            // here we don't need to allocate as getopt
            // reorders argv and passes valid pointer to
            // what is already in memory
            args.funcname = optarg;
            break;
        case '?': /* this character indicates an error */
            /* fall through */
        default:
            err_handler(PE_WRONGARG, argv[optind - 1]);
            break;    // never reached, shuts up linter
        }
    }

    // Restore. Not needed here -- just a showcase
    opterr = _opterr;

    log("optind value after parsing: %d", optind);

    if ((NULL == args.funcname) || (argc - optind < 2))
        err_handler(PE_NENARGS, NULL);    // not enough args

    if (argc - optind > 2)
        err_handler(PE_TMARGS, NULL);    // too much args

    // now we're sure on number of positional arguments -- consume them
    // again, this is not needed with more feature-rich parsers like Argp
    long double start, end;
    if (!ld_conv(argv[optind], &start) || !ld_conv(argv[++optind], &end)) {
        err_handler(PE_WRONGARG, argv[optind]);
    }

    if ((end - start) <= EPSILON) {
        fprintf(stderr, "Can not left-integrate from %Lg to %Lg\n", start, end);
        err_handler(PE_CVGERR, NULL);
    }

    args.start = start;
    args.end = end;

    // some additional steps -- fill in npoc autodetection if nthreads is 0
    // this could be also done with pthread procedures
    args.nthreads = args.nthreads ?: get_nprocs();

    log("Parsed args:"
        "\n\tnsteps: %lld,\n\tnthreads: %lld,\n\tstart: %Lg,\n\tstop: %Lg,"
        "\n\tfuncname: '%s',\n\tisverbose: %c,\n\tishelp: %c",
        (long long)args.nsteps, (long long)args.nthreads, (long double)args.start,
        (long double)args.end, args.funcname,
        args.isverbose ? 'T' : 'F', args.ishelp ? 'T' : 'F');

    return args;
}


struct thread_args {
    farg_t start;
    farg_t end;
    farg_t step;
    funcptr_t funcptr;
    volatile farg_t *resptr;
};

static void *integrate_worker(void *argsptr)
{
    // convert passed arguments back since we know what they are
    // we passed them during thread creation
    struct thread_args *args = argsptr;
    funcptr_t func = args->funcptr;
    farg_t start = args->start, end = args->end, step = args->step;

    log("Thread started with args: %Lg %Lg %Lg",
        (long double)start, (long double)end, (long double)step);

    farg_t res = .0;
    while (start < end) {
        res += func(start) * step;
        start += step;
    }

    *(args->resptr) = res;
    pthread_exit(0);
}


int main(int argc, char *argv[])
{
    enum error err = PE_OK;
    log("Got %d arguments", argc);
    log("Progname: '%s'", argv[0]);

    // First let's gather the programname from ./some/path/programname
    // exactly as Argp does it. We will utilize it in USAGE outputs
    // This breaks the string by '/' delimiter and returns pointer
    // to the last part. We assign it back to argv[0] since
    // the original name the program was called is not needed anymore

    char *bindir = strdup(argv[0]);
    except(NULL == bindir, PE_MALLOC, err, bindir_alloc);
    bindir = dirname(bindir);    // save for future

    argv[0] = basename(argv[0]);

    // Keep in mind. This is only for purpose of current example
    // Usually we don't need so much logging verbosity
    log("Transformed progname: '%s', dir: '%s'", argv[0], bindir);

    if (1 == argc)
        err_handler(PE_NOARGS, argv[0]);

    struct args args = parse_args(argc, argv);

    char *funcname;
    asprintf(&funcname, "%s%s", FUNC_PREFIX, args.funcname);
    except(NULL == funcname, PE_MALLOC, err, funcname_alloc);

    char *ldname;
    asprintf(&ldname, "%s/%s.so", bindir, funcname);
    except(NULL == funcname, PE_MALLOC, err, ldname_alloc);

    log("Trying to load func '%s' from '%s'", funcname, ldname);

    void *handle = dlopen(ldname, RTLD_NOW | RTLD_LOCAL);
    except(NULL == handle, PE_DLERR, err, dlopen_exc);

    funcptr_t dlfunc = dlsym(handle, funcname);
    except(NULL == dlfunc, PE_DLERR, err, dlsym_exc);

    // I use alloca here cause I'm feeling lazy to manage all deallocs
    pthread_t *threads = alloca(args.nthreads * sizeof *threads);

    struct thread_args *thargs = alloca(args.nthreads * sizeof *thargs);

    farg_t *results = alloca(args.nthreads * sizeof *results);
    memset(results, 0, args.nthreads * sizeof *results);

    except((NULL == threads) || (NULL == thargs) || (NULL == results), PE_MALLOC, err, dlsym_exc);

    farg_t interval = args.end - args.start;
    farg_t step = interval / args.nsteps;
    farg_t part = interval / args.nthreads;

    for (long long i = 0; i < args.nthreads; i++) {
        thargs[i] = (struct thread_args){ .start = part * i,
                                          .end = part * (i + 1),
                                          .step = step,
                                          .funcptr = dlfunc,
                                          .resptr = &results[i] };

        log("Thread #%lld args <[%Lg; %Lg] / %Lg>", i + 1, 
            (long double)thargs[i].start,
            (long double)thargs[i].end,
            (long double)thargs[i].step);
    }

    struct timespec tstart, tstop;

    // (!) make sure you know what happens if you use CLOCK_PROCESS_CPUTIME_ID
    // with threads
    clock_gettime(CLOCK_REALTIME, &tstart);
    // start threads
    for (long long i = 0; i < args.nthreads; i++) {
        // running each thread is like running:
        //      integrate_worker(&thargs[i]);
        // except that we're running it in thread xD

        int code = pthread_create(&threads[i], NULL, &integrate_worker, &thargs[i]);

        // dealloc if something goes wrong
        if (code) {
            for (long long k = i; k >= 0; k--) {
                pthread_cancel(threads[i]);
            }
            err = PE_THREAD;
            goto dlsym_exc;
        }
    }

    // wait for threads to finish
    for (long long i = 0; i < args.nthreads; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_REALTIME, &tstop);

    long long took_us = diff_us(tstart, tstop);

    farg_t sum = .0;
    for (long long i = 0; i < args.nthreads; i++) {
        sum += results[i];
    }

    printf("%Lg\n", (long double)sum);

    if (args.isverbose)
        fprintf(stderr, "Took %.8Lg s\n", took_us / 1e6L);

dlsym_exc:
    dlclose(handle);
dlopen_exc:
    free(ldname);
ldname_alloc:
    free(funcname);
funcname_alloc:
    free(bindir);
bindir_alloc:
    err_handler(err, NULL);
    return 0;
}
