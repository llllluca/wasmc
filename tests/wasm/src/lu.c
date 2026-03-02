#define N 50

int A[N][N];
int L[N][N];
int U[N][N];

void *memcpy(void *dest, const void *src, unsigned long n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    for (unsigned int i = 0; i < n; i++) {
        d[i] = s[i];
    }

    return dest;
}

/* Generate a well-conditioned integer matrix A = L*U */
void generate_matrix() {
    int i, j, k;

    // Initialize L (unit lower triangular)
    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {
            if (i > j)
                L[i][j] = (i + j) % 5 + 1;   // small integers
            else if (i == j)
                L[i][j] = 1;
            else
                L[i][j] = 0;
        }
    }

    // Initialize U (upper triangular)
    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {
            if (i <= j)
                U[i][j] = (i * j) % 7 + 1;   // non-zero diagonal
            else
                U[i][j] = 0;
        }
    }

    // Compute A = L * U
    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {
            A[i][j] = 0;
            for (k = 0; k < N; k++) {
                A[i][j] += L[i][k] * U[k][j];
            }
        }
    }
}

/* LU decomposition (Doolittle, integer-only) */
void lu_decomposition() {
    int i, j, k, sum;

    for (i = 0; i < N; i++) {

        // Compute U
        for (j = i; j < N; j++) {
            sum = 0;
            for (k = 0; k < i; k++)
                sum += L[i][k] * U[k][j];
            U[i][j] = A[i][j] - sum;
        }

        // Compute L
        for (j = i + 1; j < N; j++) {
            sum = 0;
            for (k = 0; k < i; k++)
                sum += L[j][k] * U[k][i];

            L[j][i] = (A[j][i] - sum) / U[i][i]; // exact division guaranteed
        }
    }
}

int main() {
    generate_matrix();
    lu_decomposition();

    return L[N-1][N-1] + U[N-1][N-1];
}
