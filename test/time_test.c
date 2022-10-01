#include<stdio.h>
#include<time.h>
#include<sys/time.h>


int get_time_want(char *time_in, size_t len, int sec_add){
  time_t t = time(NULL);
  if (sec_add > 0)
    t += sec_add;
  struct tm tm = *localtime(&t);
  struct timeval tv;
  (void)gettimeofday(&tv, 0);
  snprintf(time_in, len, "%02d/%02d-%02d:%02d:%02d.%02ld\n",
    tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (long int)tv.tv_usec / 10000);
  return 0;
}


int main()
{
    char time_want[18];
    get_time(time_want, 18, 3600);
    printf("time_want:%s\n", time_want);
    return 0;
}

