#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

// #define FILEPATH "/tmp/sxb/testfile"
// #define FILESIZE 1<<16
// #define FILESIZE 1<<20
#define FILESIZE 1<<12


// cd test;gcc -o write_file write_file.c;chmod +x write_file;ls -alh /tmp/testfile
int main(int argc, char *argv[])
{
  char *file_path = "/tmp/sxb/testfile";
  if(argc == 2)
    file_path = argv[1];
  printf("file_path:%s\n", file_path);
  FILE * f = fopen(file_path, "w");
  for(int i = 0; i < FILESIZE; i++){
      int temp = fputs("a", f);
      if(temp > -1){
          //Sucess!
      }
      else{
          printf("err\n");
      }
  }
  fclose(f);
  printf("ls -alh %s\n", file_path);
  return 0;
}