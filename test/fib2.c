static unsigned long
fib2(unsigned long n)
{
    if (n < 2) {
        return 1;
    }
    return fib2(n - 2) + fib2(n - 1);
}

int main()
{
    int n = 42;
    int res = fib2(n);
    return res;
}
