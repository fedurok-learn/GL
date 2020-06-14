// gcc -O2 ./dlfunc_cos.c -lm -shared -o dlfunc_cos.so
#include <math.h>

double dlfunc_cos(double x)
{
    return cos(x);
}

