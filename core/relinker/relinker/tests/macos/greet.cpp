// A bundled guest module: dynamic TLS (__tls_get_addr), initial-exec TLS (TPOFF64 through %fs) and an
// initializer.
extern "C" int puts(const char*);

static thread_local int counter = 5;
[[gnu::visibility("hidden"), gnu::tls_model("initial-exec")]] thread_local int initialExec = 7;
static volatile int initialValue = 10;
static int initialized = initialValue;

extern "C" int greet(int value) {
    counter += value;
    puts("hello from a guest module");
    return counter + initialized + initialExec;
}
