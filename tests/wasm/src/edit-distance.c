#define MAX_LEN 100   // Maximum string length
static int dp[MAX_LEN + 1][MAX_LEN + 1];

/* Compute minimum of three integers */
static int min3(int a, int b, int c) {
    int m = (a < b) ? a : b;
    return (m < c) ? m : c;
}

void *memcpy(void *dest, const void *src, unsigned long n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    for (unsigned int i = 0; i < n; i++) {
        d[i] = s[i];
    }

    return dest;
}

unsigned int my_strlen(const char *s) {
    unsigned int len = 0;

    while (s[len] != '\0') {
        len++;
    }
    return len;
}

/* Levenshtein Edit Distance */
int edit_distance(const char *s1, const char *s2) {
    int len1 = my_strlen(s1);
    int len2 = my_strlen(s2);

    // Initialize base cases
    for (int i = 0; i <= len1; i++)
        dp[i][0] = i;
    for (int j = 0; j <= len2; j++)
        dp[0][j] = j;

    // Fill DP table
    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            dp[i][j] = min3(
                dp[i - 1][j] + 1,        // deletion
                dp[i][j - 1] + 1,        // insertion
                dp[i - 1][j - 1] + cost  // substitution
            );
        }
    }

    return dp[len1][len2];
}

int main() {
    // Example longer strings (200 characters)
    const char s1[MAX_LEN + 1] =
        "benchmarkAOTembeddedoptimizationtestinaydete"
        "floydwarshallsparseapsackCRC32DPmatrixeditdisise";

    const char s2[MAX_LEN + 1] =
        "benchmarkintegerAOTembeddetesaticarraydeteic"
        "arshallixluKnapsRC32DPmatreittancebitwps";

    return edit_distance(s1, s2);
}
