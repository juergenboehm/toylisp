/* A native FASL fixture: resolve the interpreter's exported allocator. */
typedef long int lval;
extern lval ma(lval *, int, ...);

lval init(lval *f) {
    return ma(f, 2, 116L, ((1L << 40) * 32) | 16, 16L);
}
