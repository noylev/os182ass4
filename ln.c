#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if((argc != 3) && (argc != 4)){
    printf(2, "Usage: ln [-s] old new\n");
    exit();
  }
  if (argc == 3) {
    if(link(argv[1], argv[2]) < 0)
      printf(2, "link %s %s: failed\n", argv[1], argv[2]);
    exit();
  }
  if ((argv[1][0] == '-') && (argv[1][1] == 's')
    && (argv[1][2] == 0))
    {
      if (strlen(argv[2]) > 50) {
        printf(2, "link failed , %s too long(max length %d)\n", argv[2], 50);
      }
      if (symlink(argv[2], argv[3]) < 0)
        printf(2, "link -s %s %s: failed\n", argv[2], argv[3]);
    }
  else
    printf(2, "Usage: ln [-s] old new\n");
  exit();
}
