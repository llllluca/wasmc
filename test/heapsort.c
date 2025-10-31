//https://en.wikipedia.org/wiki/Heapsort
#define LENGTH 10000
#define ITERATIONS 1000

#define IM 139968
#define IA 3877
#define IC 29573

static int
gen_random()
{
    static long last = 42;
    return last = (last * IA + IC) % IM;
}

static void
my_heapsort(int n, int* ra)
{
    int    i, j;
    int    ir = n;
    int    l = (n >> 1) + 1;
    int rra;

    for (;;) {
        if (l > 1) {
            rra = ra[--l];
        }
        else {
            rra = ra[ir];
            ra[ir] = ra[1];
            if (--ir == 1) {
                ra[1] = rra;
                return;
            }
        }

        i = l;
        j = l << 1;
        while (j <= ir) {
            if (j < ir && ra[j] < ra[j + 1]) {
                ++j;
            }
            if (rra < ra[j]) {
                ra[i] = ra[j];
                j += (i = j);
            }
            else {
                j = ir + 1;
            }
        }
        ra[i] = rra;
    }
}

int ary[LENGTH];

int main()
{
    int n = LENGTH;
    int res;

    int i, j;
    for (i = 0; i < ITERATIONS; i++) {
        for (j = 1; j <= n; j++) {
            ary[j] = gen_random();
        }
        my_heapsort(n, ary);
        res = ary[n-1];
        return res;
    }
}
