int ackermann(int M, int N)
{
    if (M == 0)
    {
        return N + 1;
    }
    if (N == 0)
    {
        return ackermann(M - 1, 1);
    }
    return ackermann(M - 1, ackermann(M, (N - 1)));
}

int main()
{
    int M = 4;
    int N = 8;
    int result = ackermann(M, N);
    return result;
}
