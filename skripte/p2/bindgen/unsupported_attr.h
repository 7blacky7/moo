/* A function attribute not in the explicit ABI-neutral allowlist. */
int p2_annotated(int value) __attribute__((annotate("unmodelled")));
