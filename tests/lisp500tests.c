#define _POSIX_C_SOURCE 200809L
#define main lisp500Main
#include "../src/lisp500/lisp500a64.c"
#undef main

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "Failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static const char *activeExpression;

static lval failDebugger(lval *f) {
    fprintf(stderr, "Unexpected Lisp condition in: %s\n", activeExpression);
    print(f[1]);
    exit(1);
}

static lval evaluate(const char *text) {
    activeExpression = text;
    FILE *input = tmpfile();
    FILE *savedIns = ins;
    lval result;
    CHECK(input);
    CHECK(fputs(text, input) >= 0);
    rewind(input);
    ins = input;
    memset(stack, 0, sizeof(lval) * 64 * 1024);
    if (setjmp(top_jmp)) {
        fprintf(stderr, "Unexpected interpreter error: %s\n", text);
        exit(1);
    }
    result = eval(stack + 5, lread(stack + 5));
    if (lread(stack + 5) != 8) {
        fprintf(stderr, "Trailing input in: %s\n", text);
        exit(1);
    }
    CHECK(dyns == 0);
    ins = savedIns;
    fclose(input);
    return result;
}

static void expectNumber(const char *text, double expected) {
    lval result = evaluate(text);
    if (o2d(result) != expected) {
        fprintf(stderr, "Expression: %s\nExpected: %g\nActual: ", text, expected);
        print(result);
        exit(1);
    }
}

static void expectPrinted(const char *text, const char *expected) {
    lval result = evaluate(text);
    FILE *output = tmpfile();
    FILE *savedStdout = stdout;
    char buffer[512] = {0};
    CHECK(output);
    fflush(stdout);
    stdout = output;
    print(result);
    fflush(stdout);
    stdout = savedStdout;
    rewind(output);
    CHECK(fread(buffer, 1, sizeof(buffer) - 1, output) < sizeof(buffer) - 1);
    fclose(output);
    if (strcmp(buffer, expected)) {
        fprintf(stderr, "Expression: %s\nExpected: %s\nActual: %s\n",
                text, expected, buffer);
        exit(1);
    }
}

static void expectValues(const char *text, const double *expected, int count) {
    lval result = evaluate(text);
    lval values = xvalues;
    CHECK(count == 0 ? result == 0 : o2d(result) == expected[0]);
    CHECK(values != 8);
    for (int i = 0; i < count; ++i, values = cdr(values)) {
        CHECK(cp(values));
        CHECK(o2d(car(values)) == expected[i]);
    }
    CHECK(values == 0);
}

static void testEvaluation(void) {
    expectNumber("(+ 1 2)", 3);
    expectNumber("(- 1 4)", -3);
    expectNumber("(/ 1 2)", 0.5);
    expectNumber("(floor 7 2)", 3);
    expectNumber("(let ((x 3)) (let* ((y (+ x 1)) (z (* y 2))) z))", 8);
    expectNumber("(flet ((inc (x) (+ x 1))) (inc 41))", 42);
    expectNumber("(labels ((sum (x) (if (= x 0) 0 (+ x (sum (- x 1)))))) (sum 20))", 210);
    expectNumber("(funcall #'(lambda (x) (+ x 1)) 41)", 42);
    expectNumber("(apply #'+ '(1 2 3 4))", 10);
    lval system = evaluate("(uname)");
    for (int i = 0; i < 4; ++i, system = cdr(system)) {
        CHECK(cp(system) && sp(car(system)) && o2s(car(system))[1] == 20);
    }
    CHECK(system == 0);
}

static void testNumbersAndWords(void) {
    lval *g = stack + 5;
    CHECK(sizeof(lval) == 8);
    CHECK(!sp(d2o(g, -3)));
    CHECK(o2d(d2o(g, -(double) (1UL << 58))) == -(double) (1UL << 58));
    CHECK(sp(d2o(g, (double) (1UL << 58))));
    CHECK(o2d(d2o(g, 1e100)) == 1e100);
    CHECK(isinf(o2d(d2o(g, INFINITY))));
    CHECK(isnan(o2d(d2o(g, NAN))));
    expectPrinted("1099511627776", "1099511627776");
    expectPrinted("(- 0 1099511627776)", "-1099511627776");
    expectNumber("(ldb (cons 40 0) 1099511627775)", 1099511627775.0);
    expectNumber("(dpb 1023 (cons 10 40) 7)", 1124800395214855.0);
    expectNumber("(ldb (cons 10 40) 1124800395214855)", 1023);
    expectNumber("(ldb (cons 0 64) 123)", 0);
    expectNumber("(dpb 123 (cons 0 64) 456)", 456);
    expectNumber("(dpb 1099511627776 (cons 64 0) 0)", 1099511627776.0);
    expectNumber("(ldb (cons 8 0) (jref \"ABCDEFGH\" (cons 2 0)))", 65);
    expectNumber("(ldb (cons 8 0) (jref \"ABCDEFGH\" 2))", 65);
    expectNumber("(ldb (cons 8 0) (jref \"ABCDEFGH\" (cons 2 32)))", 69);
    expectNumber("(progn (setq j (makej 64 116)) "
                 "(_setf (jref j (cons 2 0)) 4294967295) "
                 "(_setf (jref j (cons 2 32)) 2147483649) "
                 "(jref j (cons 2 0)))", 4294967295.0);
    expectNumber("(jref j (cons 2 32))", 2147483649.0);
    expectNumber("(progn (setq word (jref j 2)) (gc) "
                 "(_setf (jref j (cons 2 0)) 0) (_setf (jref j 2) word) "
                 "(jref j (cons 2 0)))", 4294967295.0);
    expectNumber("(ldb (cons 32 32) word)", 2147483649.0);
    expectNumber("(ldb (cons 32 0) (dpb 5 (cons 3 0) word))", 4294967293.0);
    expectNumber("(ldb (cons 32 0) (ldb (cons 64 0) word))", 4294967295.0);
    expectNumber("(ldb (cons 5 0) (ival 9007199254740992))", 16);
    expectNumber("(ldb (cons 5 0) (ival 1099511627776000))", 16);
    expectNumber("(progn (setq j (makej 128 20)) (_setf (jref j 2) 1099511627776) (jref j 2))",
                 1099511627776.0);
    /* Inspect call sites in the actual bootstrap, including register-passed -1. */
    lval fn = o2a(symi[53].sym)[5];
    CHECK(o2s(o2a(fn)[2])[4] == -1);
    CHECK(o2a(symi[53].sym)[7] == 53 * 8);
}

static void testStringsAndLayouts(void) {
    lval *g = stack + 5;
    for (int length = 0; length <= 130; ++length) {
        char text[132];
        memset(text, 'a' + length % 26, length);
        text[length] = 0;
        lval string = strf(g, text);
        CHECK(o2s(string)[1] == 20);
        CHECK(o2s(string)[0] / 32 - 8 == length);
        CHECK((o2s(string)[0] & 4) == 0);
        CHECK(!strcmp(o2z(string), text));
        stack[6] = string;
        gc(stack + 6);
        CHECK(!strcmp(o2z(string), text));
        stack[6] = 0;
    }
    expectPrinted("(string (code-char 65) (code-char 66) (code-char 67))", "\"ABC\"");
    expectPrinted("(string)", "\"\"");
    expectPrinted("'(\"\" \"1234567\" \"12345678\" \"123456789\")",
                  "(\"\" \"1234567\" \"12345678\" \"123456789\")");
    expectPrinted("(string= \"abcdefghijklmno\" \"abcdefghijklmno\")", "T");
    expectPrinted("(string= \"abcdefghijklmno\" \"abcdefghijklmnp\")", "nil");
    expectPrinted("(gensym)", "#:g000");
    for (int bits = 0; bits <= 130; ++bits) {
        lval *buffer = mb0(g, bits);
        buffer[1] = 20;
        CHECK(buffer[0] >> 8 == (bits + 63) / 64);
        CHECK((buffer[0] >> 8) * 64 + ((buffer[0] >> 3) & 31) * 2 +
              (buffer[0] & 1) - 63 == bits);
        CHECK(!(buffer[0] & 4));
        memset(buffer + 2, 0xa5, (buffer[0] >> 8) * sizeof(lval));
        stack[6] = s2o(buffer);
        gc(stack + 6);
        if (bits) CHECK((unsigned long) buffer[2] == 0xa5a5a5a5a5a5a5a5UL);
        stack[6] = 0;
    }
    /* Poison reused storage: MAKEI must clear complete 64-bit payload words. */
    lval *array = ma0(g, 12);
    array[1] = 116;
    memset(array + 2, 0xa5, 12 * sizeof(lval));
    gc(g);
    lval args[4] = {0, d2o(g, 12), d2o(g, 112), 0};
    array = o2a(lmakei(args, args + 3));
    for (int i = 0; i < 12; ++i) CHECK(array[i + 2] == 0);
}

static void testNonlocalJumps(void) {
    const double pair[] = {1099511627776.0, -7};
    expectValues("(block done (return-from done (values 1099511627776 (- 0 7))) 0)", pair, 2);
    expectValues("(catch 'done (throw 'done (values 1099511627776 (- 0 7))))", pair, 2);
    expectValues("(funcall #'(lambda () (block done (return-from done (values 1099511627776 (- 0 7))))))",
                 pair, 2);
    expectValues("(flet ((done () (return-from done (values 1099511627776 (- 0 7))))) (done))",
                 pair, 2);
    expectValues("(block done (return-from done (values)))", NULL, 0);
    expectValues("(catch 'done (throw 'done (values)))", NULL, 0);
    expectPrinted("(block done (return-from done nil))", "nil");
    expectPrinted("(catch 'done (throw 'done nil))", "nil");
    expectPrinted("(catch 'done (throw 'done '(1 2 3)))", "(1 2 3)");
    expectPrinted("(block outer (block inner (return-from outer \"escaped\")))", "\"escaped\"");
    expectNumber("(catch 'outer (catch 'inner (throw 'outer 42)))", 42);
    expectNumber("(progn (setq count 0) (tagbody start (setq count (+ count 1)) "
                 "(if (< count 5) (go start))) count)", 5);
    expectNumber("(progn (setq count 0) (tagbody (go end) (setq count 99) end "
                 "(setq count 42)) count)", 42);
    expectPrinted("(tagbody (go done) done)", "nil");
    CHECK(xvalues == 8);
    expectNumber("(progn (setq count 0) (tagbody start (tagbody (setq count (+ count 1)) "
                 "(if (< count 3) (go start)))) count)", 3);
    expectNumber("(progn (setq cleanup 0) (block done (unwind-protect "
                 "(return-from done 42) (setq cleanup (+ cleanup 1)))) cleanup)", 1);
    expectNumber("(progn (setq cleanup 0) (catch 'done (unwind-protect "
                 "(throw 'done 42) (setq cleanup (+ cleanup 1)))) cleanup)", 1);
    expectNumber("(progn (setq cleanup 0) (tagbody (unwind-protect "
                 "(go done) (setq cleanup (+ cleanup 1))) done) cleanup)", 1);
    expectValues("(catch 'done (unwind-protect (throw 'done (values 1099511627776 (- 0 7))) "
                 "(gc) (values 1 2 3)))", pair, 2);
    expectValues("(block done (unwind-protect (return-from done (values 1099511627776 (- 0 7))) "
                 "(gc) (values 1 2 3)))", pair, 2);
}

static void testFiles(void) {
    char path[] = "/tmp/lisp500-test-XXXXXX";
    char expression[1024];
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    close(fd);
    snprintf(expression, sizeof(expression),
             "(progn (setq file (make-file-stream \"%s\" t)) "
             "(write-file-stream file \"0123456789ABCDEF\" 0 16) "
             "(finish-file-stream file) (close-file-stream file))", path);
    expectPrinted(expression, "nil");
    snprintf(expression, sizeof(expression),
             "(progn (setq file (make-file-stream \"%s\" nil)) "
             "(setq buffer \"xxxxxxxxxxxxxxxx\") (read-file-stream file buffer 0) "
             "(close-file-stream file) buffer)", path);
    expectPrinted(expression, "\"0123456789ABCDEF\"");
    CHECK(unlink(path) == 0);
}

static void testFasl(const char *modulePath) {
    lval *g = stack + 5;
    lval packages[2] = {0, pkg};
    lval symbols[2] = {0, TRUE};
    lval cells[2] = {0, symi[53].sym};
    lval objects[4] = {23L << 5, 20};
    lval values[6] = {2L << 8, 116, 4 * sizeof(lval) + 1, 3,
                      ((1L << 40) * 32) | 16, 0};
    lval *rv, *ro;
    memcpy(objects + 2, "123456789ABCDEF", 16);
    CHECK(fr(objects, packages, symbols, cells, values,
             (lval) ((1UL << 62) | (sizeof(lval) + 1))) == cells[1]);
    CHECK(fr(objects, packages, symbols, cells, values,
             (lval) ((2UL << 62) | (sizeof(lval) + 2))) == symbols[1]);
    CHECK(fr(objects, packages, symbols, cells, values,
             (lval) ((3UL << 62) | (sizeof(lval) + 3))) == packages[1]);
    CHECK(fr(objects, packages, symbols, cells, values, -80) == -80);
    CHECK(fasr(g, NULL, 0, NULL, NULL, 0, NULL, 0, values, 6, objects, 4, &rv, &ro) == 0);
    CHECK(rv[2] == c2o(rv + 4));
    CHECK(rv[3] == s2o(ro));
    CHECK(o2d(car(rv[2])) == 1099511627776.0);
    CHECK(!strcmp(o2z(rv[3]), "123456789ABCDEF"));
    stack[6] = a2o(rv);
    gc(stack + 6);
    CHECK(o2d(car(rv[2])) == 1099511627776.0);
    CHECK(!strcmp(o2z(rv[3]), "123456789ABCDEF"));
    stack[6] = 0;
    char expression[1024];
    snprintf(expression, sizeof(expression), "(fasl \"%s\")", modulePath);
    expectPrinted(expression, "#(1099511627776 0)");
}

static void testGarbageCollection(void) {
    expectPrinted("(progn (setq keep '(\"abcdefghijklmno\" 1.25 1099511627776)) (gc) keep)",
                  "(\"abcdefghijklmno\" 1.25 1099511627776)");
    /* Allocate beyond the original heap capacity while retaining only 32 cells. */
    memset(stack, 0, sizeof(lval) * 64 * 1024);
    lval *g = stack + 40;
    for (int i = 0; i < 2200000; ++i) {
        stack[6 + i % 32] = cons(g, d2o(g, i), 0);
    }
    gc(g);
    for (int i = 0; i < 32; ++i) {
        int last = 2199999 - (2199999 - i) % 32;
        CHECK(o2d(car(stack[6 + i])) == last);
    }
    memset(stack, 0, sizeof(lval) * 64 * 1024);
    gc(stack + 5);
    lval freeWords = 0;
    for (lval *freeBlock = memf; freeBlock; freeBlock = (lval *) freeBlock[0]) {
        CHECK(freeBlock >= memory && freeBlock < memory + memory_size / sizeof(lval));
        CHECK(freeBlock[1] > 0);
        freeWords += freeBlock[1];
    }
    CHECK(freeWords > (lval) (memory_size / sizeof(lval)) - 20000);
    expectNumber("(+ 20 22)", 42);
}

static void testLibrary(const char *libraryPath) {
    FILE *emptyInput = tmpfile();
    int savedInput = dup(STDIN_FILENO);
    CHECK(emptyInput && savedInput >= 0);
    CHECK(dup2(fileno(emptyInput), STDIN_FILENO) >= 0);
    memset(stack, 0, sizeof(lval) * 64 * 1024);
    if (setjmp(top_jmp)) {
        fprintf(stderr, "Unexpected error loading %s\n", libraryPath);
        exit(1);
    }
    load(stack + 5, (char *) libraryPath);
    CHECK(dup2(savedInput, STDIN_FILENO) >= 0);
    close(savedInput);
    fclose(emptyInput);
    lval debugger = is(stack + 5, pkg, strf(stack + 5, "INVOKE-DEBUGGER"));
    o2a(debugger)[5] = ma(stack + 5, 5, 212L,
                        ms(stack + 5, 3, 212L, (lval) failDebugger, 1L, 1L),
                        0L, 0L, 0L, debugger);
    char expression[2048];
    expectPrinted("(type-of 9007199254740992)", "FIXNUM");
    for (int length = 0; length <= 130; ++length) {
        snprintf(expression, sizeof(expression),
                 "(length (make-string %d :initial-element (code-char 255)))", length);
        expectNumber(expression, length);
        snprintf(expression, sizeof(expression),
                 "(let ((s (make-string %d :initial-element (code-char 255))) (sum 0)) "
                 "(dotimes (i %d) (incf sum (char-code (char s i)))) sum)", length, length);
        expectNumber(expression, length * 255);
        snprintf(expression, sizeof(expression),
                 "(let ((b (make-array %d :element-type 'bit :initial-element 1)) (sum 0)) "
                 "(dotimes (i %d) (when (evenp i) (setf (aref b i) 0))) (gc) "
                 "(dotimes (i %d) (incf sum (aref b i))) (+ (* 1000 (length b)) sum))",
                 length, length, length);
        expectNumber(expression, length * 1000 + length / 2);
    }
    expectPrinted("(let ((s \"ABCDEFGHIJKLMNOPQ\") (codes nil)) "
                  "(dotimes (i (length s)) (push (char-code (char s i)) codes)) (nreverse codes))",
                  "(65 66 67 68 69 70 71 72 73 74 75 76 77 78 79 80 81)");
    expectPrinted("(let ((s (make-string 17 :initial-element (code-char 65)))) "
                  "(setf (char s 0) (code-char 66)) (setf (char s 3) (code-char 67)) "
                  "(setf (char s 4) (code-char 68)) (setf (char s 7) (code-char 69)) "
                  "(setf (char s 8) (code-char 70)) (setf (char s 15) (code-char 71)) "
                  "(setf (char s 16) (code-char 72)) (gc) s)", "\"BAACDAAEFAAAAAAGH\"");
    expectPrinted("(subseq \"0123456789ABCDEF\" 3 13)", "\"3456789ABC\"");
    expectPrinted("(conc-string \"ABCD\" \"EFGHI\" \"JKLMNOPQ\")",
                  "\"ABCDEFGHIJKLMNOPQ\"");
    expectPrinted("(let* ((s (subseq \"0123456789ABCDEF\" 0)) "
                  "(a (make-array 5 :element-type 'character :displaced-to s "
                  ":displaced-index-offset 6))) "
                  "(multiple-value-list (array-displacement a)))", "(\"0123456789ABCDEF\" 6)");
    expectPrinted("(array-dimensions (make-array '(2 3)))", "(2 3)");
    expectNumber("(let ((a (make-array '(2 3)))) (setf (aref a 1 2) 42) (aref a 1 2))", 42);
    expectNumber("(let ((b (make-array 130 :element-type 'bit :initial-element 0))) "
                 "(dolist (i '(0 31 32 63 64 127 128 129)) (setf (aref b i) 1)) "
                 "(count 1 (subseq b 30 130)))", 7);
    expectPrinted("(read-from-string \"ABCDEFGH\")", "ABCDEFGH");
    expectNumber("(read-from-string \"42\")", 42);
    expectNumber("(read-from-string \"   \" nil 123)", 123);
    expectPrinted("(with-output-to-string (s) (write-string \"ABCDEFGH\" s) "
                  "(write-string \"IJKLMNOPQ\" s))", "\"ABCDEFGHIJKLMNOPQ\"");
    expectNumber("(progn (defstruct (port-structure (:constructor make-port-structure (field))) field) "
                 "(port-structure-field (make-port-structure 42)))", 42);
    expectNumber("(progn (defclass port-class () ((field :initarg :field "
                 ":accessor port-field))) (port-field (make-instance 'port-class :field 42)))", 42);
    expectNumber("(let ((h (make-hash-table :test 'equal :size 3))) "
                 "(dotimes (i 20) (setf (gethash (make-string (+ i 1) "
                 ":initial-element (code-char 65)) h) i)) "
                 "(gethash (make-string 17 :initial-element (code-char 65)) h))", 16);
    expectNumber("(restart-case (invoke-restart 'port-restart 42) (port-restart (x) x))", 42);
    expectNumber("(let ((n 0)) (block done (unwind-protect "
                 "(return-from done 42) (incf n))) n)", 1);

    /* Exercise the actual Lisp compiler, C compiler and native loader in an
     * isolated directory. Quoted data covers every relocation class and raw halves. */
    char directory[] = "/tmp/lisp500-library-XXXXXX";
    int savedDirectory = open(".", O_RDONLY);
    CHECK(savedDirectory >= 0 && mkdtemp(directory));
    CHECK(chdir(directory) == 0);
    expectNumber("(test-compilation (list 'setq 'port-constant (list 'quote "
                 "(list \"ABCDEFGH\" '(1 2 3) 1099511627776000 (/ 5 4) "
                 "(find-package \"CL\") 'car (make-port-structure 42) "
                 "(make-array 65 :element-type 'bit :initial-element 1)))))", 0);
    CHECK(rename("test.so", "constants.so") == 0);
    evaluate("(fasl \"./constants.so\")");
    expectPrinted("(subseq port-constant 0 3)", "(\"ABCDEFGH\" (1 2 3) 1099511627776000)");
    expectNumber("(fourth port-constant)", 1.25);
    expectPrinted("(eq (fifth port-constant) (find-package \"CL\"))", "T");
    expectPrinted("(eq (sixth port-constant) 'car)", "T");
    expectNumber("(port-structure-field (seventh port-constant))", 42);
    expectNumber("(count 1 (eighth port-constant))", 65);
    expectNumber("(test-compilation '(progn "
                 "(setq port-function #'(lambda (x) (+ x 2))) "
                 "(setq port-data-function #'(lambda () '(\"ABCDEFGHIJKLMNOPQ\" (4 5))))))", 0);
    CHECK(rename("test.so", "function.so") == 0);
    evaluate("(fasl \"./function.so\")");
    expectNumber("(funcall port-function 40)", 42);
    expectPrinted("(funcall port-data-function)", "(\"ABCDEFGHIJKLMNOPQ\" (4 5))");
    evaluate("(gc)");
    expectPrinted("(subseq port-constant 0 3)", "(\"ABCDEFGH\" (1 2 3) 1099511627776000)");
    expectNumber("(funcall port-function 40)", 42);
    expectPrinted("(funcall port-data-function)", "(\"ABCDEFGHIJKLMNOPQ\" (4 5))");
    CHECK(unlink("test.c") == 0 && unlink("constants.so") == 0 && unlink("function.so") == 0);
    CHECK(fchdir(savedDirectory) == 0);
    close(savedDirectory);
    CHECK(rmdir(directory) == 0);
}

int main(int argc, char *argv[]) {
    CHECK(argc == 3);
    FILE *savedStdin = stdin;
    FILE *emptyInput = tmpfile();
    char *arguments[] = {"lisp500", NULL};
    CHECK(emptyInput);
    stdin = emptyInput;
    CHECK(lisp500Main(1, arguments) == 0);
    stdin = savedStdin;
    fclose(emptyInput);
    ins = stdin;
    testEvaluation();
    testNumbersAndWords();
    testStringsAndLayouts();
    testNonlocalJumps();
    testFiles();
    testFasl(argv[1]);
    testGarbageCollection();
    testLibrary(argv[2]);
    puts("lisp500 64-bit regression tests passed");
    free(stack);
    free(memory);
    return 0;
}
