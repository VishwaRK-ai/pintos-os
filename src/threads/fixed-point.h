#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

/*We using a 17.14 fixed point representation.*/
/*1:sign bit; 17 bits:whole number ; 14 bits:fractional part*/


#define F (1 << 14)

/*Convert integer to fixed point*/
#define INT_TO_FP(n) ((n) * F)

/*Convert fixed-point to integer (rounding towards zero)*/
#define FP_TO_INT_ZERO(x) ((x) / F)

/*Convert fixed-point to integer (rounding to nearest intger)*/
#define FP_TO_INT_NEAREST(x) ((x) >= 0 ? ((x) + F / 2) / F : ((x) - F / 2) / F)

/*Add two fp numbers*/
#define ADD_FP(x, y) ((x) + (y))

/*Subtract two fp numbers*/
#define SUB_FP(x, y) ((x) - (y))

/*Add fp number x to integer n*/
#define ADD_FP_INT(x, n) ((x) + (n) * F)

/*Subtract an integer n from a fp number x */
#define SUB_FP_INT(x, n) ((x) - (n) * F)

/*Multiply two fp numbers */
#define MULT_FP(x, y) (((int64_t)(x)) * (y) / F)

/* Multiply a fp number x by integer n */
#define MULT_FP_INT(x, n) ((x) * (n))

/* Divide two fp numbers */
#define DIV_FP(x, y) (((int64_t)(x)) * F / (y))

/* Divide a fp number x by integer n */
#define DIV_FP_INT(x, n) ((x) / (n))

#endif /* threads/fixed-point.h */