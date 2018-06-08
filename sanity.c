#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"

#define DEBUG 0

char name[3];
char *echoargv[] = { "echo", "ALL", "TESTS", "PASSED", 0 };
int stdout = 1;

void
testwrite(int amount)
{
  int fd;

  if (DEBUG) {
    printf(1, "Writing %d bytes... ", amount);
  }

  // Create text to write to the file.
  char * text = "0123456789abcdef";

  unlink("testfile");
  fd = open("testfile", O_CREATE | O_RDWR);
  if(fd < 0) {
    printf(1, "cannot create testfile");
    exit();
  }

  for (int index = 0; index < (amount / 16); index++) {

    if(write(fd, text, 16) != 16){
      printf(1, "write testfile failed\n");
      exit();
    }
  }

  if (DEBUG) {
    printf(1,"Wrote %d bytes\n", (amount / 16) * 16);
  }

  close(fd);
}

int
main(int argc, char *argv[])
{
  printf(1, "Sanity test starting\n");

  // Write 6KB.
  testwrite(6144);
  printf(1, "Finished writing 6KB (direct)\n");
  // Write 70KB.
  testwrite(71680);
  printf(1, "Finished writing 70KB (single indirect)\n");
  // Write 8MB.
  testwrite(8388608);
  printf(1, "Finished writing 8MB (double indirect)\n");
  exit();
}
