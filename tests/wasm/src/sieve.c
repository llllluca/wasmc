#define LENGTH 17000

int main()
{
    static char flags[8192 + 1];
    unsigned long res;
    int n = LENGTH;
    long i, k;
    int count;

    res = 0;
    while (n--)
    {
        count = 0;
        for (i = 2; i <= 8192; i++)
        {
            flags[i] = 1;
        }
        for (i = 2; i <= 8192; i++)
        {
            if (flags[i])
            {
                /* remove all multiples of prime: i */
                for (k = i + i; k <= 8192; k += i)
                {
                    flags[k] = 0;
                }
                count++;
            }
        }
        res += count;
    }
    return res;
}
