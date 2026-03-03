#define N 100
#define INF 1000000000   // Large value for "infinity"

static int dist[N][N];

/* Initialize deterministic weighted graph */
void init_graph() {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {

            if (i == j)
                dist[i][j] = 0;
            else if ((i + j) % 7 == 0)
                dist[i][j] = (i * j) % 20 + 1;  // small positive weight
            else
                dist[i][j] = INF;
        }
    }
}

/* Floyd–Warshall algorithm */
void floyd_warshall() {
    for (int k = 0; k < N; k++) {
        for (int i = 0; i < N; i++) {
            for (int j = 0; j < N; j++) {

                if (dist[i][k] != INF && dist[k][j] != INF) {
                    int new_dist = dist[i][k] + dist[k][j];

                    if (new_dist < dist[i][j]) {
                        dist[i][j] = new_dist;
                    }
                }
            }
        }
    }
}

int main() {
    init_graph();
    floyd_warshall();
    return dist[N-1][N-1];
}
