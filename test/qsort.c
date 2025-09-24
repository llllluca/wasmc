#define LEN 337

int x[337] = {19, 12, 423, 23, 56, 78, 34, 90, 5, 67, 102, 12, 45, 88, 3, 76, 21, 0, 39, 44, 15, 67, 89, 22, 33, 54, 11, 9, 27, 48, 72, 15, 60, 99, 30, 14, 83, 1, 25, 37, 92, 50, 8, 66, 41, 18, 73, 20, 5, 31, 7, 49, 64, 11, 38, 86, 4, 17, 55, 100, 29, 13, 42, 91, 19, 24, 68, 6, 80, 3, 57, 16, 30, 74, 2, 46, 82, 25, 10, 63, 1, 35, 8, 75, 26, 59, 14, 32, 87, 20, 4, 70, 35, 12, 65, 9, 18, 53, 27, 94, 81, 40, 2, 71, 33, 44, 95, 17, 23, 58, 10, 97, 15, 24, 69, 12, 36, 13, 78, 99, 3, 88, 46, 21, 62, 29, 28, 84, 7, 12, 93, 16, 5, 47, 79, 34, 20, 11, 8, 39, 90, 15, 1, 66, 2, 54, 72, 19, 33, 85, 6, 10, 41, 11, 76, 24, 18, 60, 5, 7, 37, 92, 4, 14, 68, 1, 49, 80, 30, 3, 58, 25, 22, 99, 14, 17, 73, 9, 12, 44, 20, 8, 61, 3, 35, 90, 16, 19, 77, 12, 2, 64, 5, 50, 82, 22, 11, 39, 8, 27, 45, 10, 70, 1, 15, 36, 88, 4, 18, 54, 19, 9, 63, 7, 29, 75, 2, 42, 91, 11, 20, 66, 3, 38, 84, 14, 12, 57, 8, 46, 99, 5, 15, 73, 20, 4, 60, 1, 32, 78, 9, 21, 88, 12, 10, 41, 6, 39, 92, 3, 14, 68, 1, 49, 80, 30, 3, 58, 25, 22, 99, 14, 17, 73, 9, 12, 44, 20, 8, 61, 3, 35, 90, 16, 19, 77, 12, 2, 64, 5, 50, 82, 22, 11, 39, 8, 27, 45, 10, 70, 1, 15, 36, 88, 4, 18, 54, 19, 9, 63, 7, 29, 75, 2, 42, 91, 11, 20, 66, 3, 38, 84, 14, 12, 57, 8, 46, 99, 5, 15, 73, 20, 4, 60, 1, 32, 78, 9, 21, 88, 12, 10, 41, 6, 39, 92, 3};

void swap(int* a, int* b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

int partition(int arr[], int low, int high) {

    // Initialize pivot to be the first element
    int p = arr[low];
    int i = low;
    int j = high;

    while (i < j) {

        // Find the first element greater than
        // the pivot (from starting)
        while (arr[i] <= p && i <= high - 1) {
            i++;
        }

        // Find the first element smaller than
        // the pivot (from last)
        while (arr[j] > p && j >= low + 1) {
            j--;
        }
        if (i < j) {
            swap(&arr[i], &arr[j]);
        }
    }
    swap(&arr[low], &arr[j]);
    return j;
}

void _quickSort(int arr[], int low, int high) {
    if (low < high) {

        // call partition function to find Partition Index
        int pi = partition(arr, low, high);

        // Recursively call quickSort() for left and right
        // half based on Partition Index
        _quickSort(arr, low, pi - 1);
        _quickSort(arr, pi + 1, high);
    }
}

void quickSort(int arr[], int len) {
    _quickSort(arr, 0, len-1);
}

int main(void) {
    quickSort(x, LEN);
    return x[LEN-2];
}
