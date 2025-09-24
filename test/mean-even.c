#define LEN 10

int x[LEN] = {10, 34, 123, 2391, 23, 0, 21, 67, 32, 87};

int main(void) {
    int sum = 0;
    unsigned int even_count = 0;
    for (unsigned int i = 0; i < LEN; i++) {
        if (x[i] % 2 == 0) {
            sum += x[i];
            even_count++;
        }
    }
    return sum / even_count;
}
