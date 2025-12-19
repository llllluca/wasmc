//https://en.wikipedia.org/wiki/Matrix_multiplication

#define SIZE 50

static void
fillmatrix(int *m, int rows, int cols)
{
    int i, j, count = 1;
    for (i = 0; i < rows; i++) {
        for (j = 0; j < cols; j++) {
            m[i * rows + j] = count++;
        }
    }
}

static void
mmult(int rows, int cols, int *m1, int *m2, int *m3)
{
    int i, j, k, val;

    for (i = 0; i < rows; i++) {
        for (j = 0; j < cols; j++) {
            val = 0;
            for (k = 0; k < cols; k++) {
                val += m1[i * rows + k] * m2[k * rows + j];
            }
            m3[i * rows + j] = val;
        }
    }
}

int m1[SIZE * SIZE];
int m2[SIZE * SIZE];
int mm[SIZE * SIZE];

int main()
{
    // SETUP
    int rows = SIZE;
    int cols = SIZE;
    fillmatrix(m1, rows, cols);
    fillmatrix(m2, rows, cols);
    fillmatrix(mm, rows, cols);

    int i;
    mmult(rows, cols, m1, m2, mm);
    int res = mm[0 * rows + 0] + mm[2* rows + 3] + mm[3 * rows + 2] + mm[4 * rows + 4];
    return res;
}
