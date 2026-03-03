#define N 100

static int A[N][N];
static int b[N];
static int x[N];
static int x_known[N];  // reference solution

void* memset(void* ptr, int value, unsigned long num) {
    unsigned char* p = ptr;
    while (num--) {
        *p++ = (unsigned char)value;
    }
    return ptr;
}

void generate_matrix() {
    // Initialize x_known and x
    for (int i = 0; i < N; i++) {
        x_known[i] = (i % 10) + 1;
        x[i] = 0;
    }

    // Compute A = L * U without storing L and U
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {

            int sum = 0;
            int limit = (i < j) ? i : j;

            for (int k = 0; k <= limit; k++) {

                // Compute L[i][k]
                int L_ik;
                if (i == k)
                    L_ik = 1;
                else
                    L_ik = (i + k) % 5 + 1;

                // Compute U[k][j]
                int U_kj = (k * j) % 7 + 1;

                sum += L_ik * U_kj;
            }

            A[i][j] = sum;
        }
    }

    // Compute b = A * x_known
    for (int i = 0; i < N; i++) {
        int sum = 0;
        for (int j = 0; j < N; j++) {
            sum += A[i][j] * x_known[j];
        }
        b[i] = sum;
    }
}

/* Integer-only Gauss elimination */
void gauss_elimination() {
    for (int i = 0; i < N; i++) {
        if (A[i][i] == 0) {
            // Zero pivot at row i
            return;
        }

        // Eliminate lower rows
        for (int j = i + 1; j < N; j++) {
            int factor = A[j][i] / A[i][i];

            for (int k = i; k < N; k++) {
                A[j][k] -= factor * A[i][k];
            }

            b[j] -= factor * b[i];
        }
    }

    // Back substitution
    for (int i = N - 1; i >= 0; i--) {
        int sum = 0;
        for (int j = i + 1; j < N; j++) {
            sum += A[i][j] * x[j];
        }
        x[i] = (b[i] - sum) / A[i][i];  // exact division guaranteed
    }
}

int main() {
    generate_matrix();
    gauss_elimination();

    // Optional: verify solution
    int correct = 1;
    for (int i = 0; i < N; i++) {
        if (x[i] != x_known[i]) {
            correct = 0;
            break;
        }
    }

    return correct;
}
