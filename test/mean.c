#define LEN 10

int x[LEN];

int main(void) {
    for (unsigned int i = 0; i < LEN; i++) {
        x[i] = i+1;
    }
    int sum = 0;
    for (unsigned int i = 0; i < LEN; i++) {
        sum += x[i];
    }
    return sum / LEN;
}
