// gcc -O2 ./dlfunc_sin.c -lm -shared -o dlfunc_sin.so
#include <math.h>

double dlfunc_sin(double x)
{
    return sin(x);
}

