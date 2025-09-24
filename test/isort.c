#define LEN 10

int x[LEN] = {10, 34, 123, 2391, 23, 0, 21, 67, 32, 87};

void insertionSort(int arr[], int len) {
    for (int i = 1; i < len; i++) {
        int key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j = j - 1;
        }
        arr[j + 1] = key;
    }
}

int main(void) {
    insertionSort(x, LEN);
    return x[0];
}
