#include<stdio.h>
typedef __uint64_t uint64_t;
typedef long int int64_t;
int main()
{
    uint64_t now=98247031;
    uint64_t sc_next_tick_time=98249030;
    printf("%ld\n", (int64_t)now-(int64_t)sc_next_tick_time);
    return 0;
}

