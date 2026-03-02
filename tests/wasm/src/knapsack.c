#define N 5000
#define W 5000

int weights[N];
int values[N];
int dp[W + 1];

void* memset(void* ptr, int value, unsigned long num) {
    unsigned char* p = ptr;
    while (num--) {
        *p++ = (unsigned char)value;
    }
    return ptr;
}

/* Initialize deterministic test data */
void init_data() {
    for (int i = 0; i < N; i++) {
        weights[i] = (i % 15) + 1;       // weights 1..15
        values[i]  = (i * 7) % 100 + 10; // values 10..109
    }

    for (int w = 0; w <= W; w++)
        dp[w] = 0;
}

int knapsack() {
    for (int i = 0; i < N; i++) {
        for (int w = W; w >= weights[i]; w--) {

            int candidate = values[i] + dp[w - weights[i]];
            if (candidate > dp[w])
                dp[w] = candidate;
        }
    }
    return dp[W];
}

int main() {
    init_data();
    return knapsack();
}
