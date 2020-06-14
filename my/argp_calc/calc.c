/**
 * Simple command line 
 * calculator to test 
 * and learn argp
 */

#include "stdio.h"
#include "stdlib.h"

#include "argp.h"

const char *argp_program_version =
  "argp-ex2 1.0";
const char *argp_program_bug_address =
  "<bug-gnu-utils@gnu.org>";

static char *const doc = "Simple calculator";
static char *const args_doc = "ARG1 ARG2";

static struct argp_option options[] = {
    { "arg1", '1', "ARG1", 0, "First argument" },
    { "arg2", '2', "ARG2", 0, "Second argument" },
    { "add", 'a', 0, 0, "Add two numbers" },
    { "sub", 's', 0, 0, "Subtract two numbers" },
    { 0 }
};

struct arguments {
    int num1, num2;
    unsigned int add : 1;
    unsigned int sub : 1;
};

static error_t 
parse_opt(int key, char* arg, struct argp_state *state) 
{
    struct arguments* arguments = state->input;
    
    switch (key) { 
    case '1':
        sscanf(arg, "%d", &(arguments->num1));
        break;
    case '2':
        sscanf(arg, "%d", &(arguments->num2));
        break;
    case 'a': ;
        arguments->add = 1;
        break;
    case 's': ;
        arguments->sub = 1;
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc };

int main(int argc, char** argv) {
    struct arguments arguments;

    arguments.add = 0;
    arguments.sub = 0;

    argp_parse(&argp, argc, argv, 0, 0, &arguments);

    if (arguments.add == 1) {
        printf("%d\n", arguments.num1 + arguments.num2);
    } else if (arguments.sub == 1) {
        printf("%d\n", arguments.num1 - arguments.num2);
    } else {
        fprintf(stderr, "%s", "No args specified");
    }

    return 0;
}

