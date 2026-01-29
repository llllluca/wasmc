#define N 16384

void* memset(void* ptr, int value, unsigned long num) {
    unsigned char* p = ptr;
    while (num--) {
        *p++ = (unsigned char)value;
    }
    return ptr;
}

int main(void) {
    static char flags[N + 1];
    unsigned long res;
    long i, k;

    res = 0;
    for (i = 2; i <= N; i++) {
        flags[i] = 1;
    }
    for (i = 2; i <= N; i++) {
        if (flags[i]) {
            /* remove all multiples of prime: i */
            for (k = i + i; k <= N; k += i) {
                flags[k] = 0;
            }
            res++;
        }
    }
    return res;
}
