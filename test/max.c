#define LEN 10

int x[LEN] = {10, 34, 123, 255, 23, 0, 21, 67, 32, 87};

int main(void) {
    int max = 0;
    for (unsigned int i = 0; i < LEN; i++) {
        if (x[i] > max) {
            max = x[i];
        }
    }
    return max;
}
