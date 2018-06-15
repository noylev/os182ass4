#include "types.h"
#include "user.h"
#include "fcntl.h"

#define DEBUG 0
#define RESULT_LIMIT 50

int results[RESULT_LIMIT];
int result_index;

void add_result(int result) {
  if (result_index == RESULT_LIMIT) {
    printf(2, "error adding result\n");
    return;
  }
  results[result_index] = result;
  result_index++;
}

int checkuntag(int fd,char* key)
{
  if (funtag(fd, key) == 0) {
    if (DEBUG) {
      printf(1, "untagging key = %s successful\n",key);
    }
    return 1;
  }
  else {
    if(DEBUG) {
      printf(2, "untagging key = %s failed \n",key);
    }
    return 0;
  }
}

int checkgettag(int fd,char* key)
{
    char buf[50];
    if (gettag(fd, key,buf) > 0) {
      if (DEBUG) {
        printf(1, "gettag  successful, key =  %s, val = %s\n",key,buf);
      }
      return 1;
    }
    else {
      if (DEBUG) {
        printf(2, "gettag failed,key = %s \n",key);
      }
      return 0;
    }
}

int main(int argc, char *argv[])
{
    int fd;

    if ((fd = open("find", O_CREATE | O_RDWR)) < 0) {
        printf(2,"open failed\n");
        exit();
    }
    int expected[RESULT_LIMIT] = {1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};


    ftag(fd, "z", "x");
    add_result(checkgettag(fd,"z"));
    ftag(fd, "c", "v");
    add_result(checkgettag(fd,"c"));

    ftag(fd, "key1", "value1");
    ftag(fd, "key2", "value2");
    ftag(fd, "key3", "value3");
    ftag(fd, "key4", "value4");
    ftag(fd, "key5", "value5");
    ftag(fd, "key6", "value6");
    ftag(fd, "key7", "value7");

    add_result(checkgettag(fd,"key1"));
    add_result(checkgettag(fd,"key2"));
    add_result(checkgettag(fd,"key3"));
    add_result(checkgettag(fd,"key4"));
    add_result(checkgettag(fd,"key5"));
    add_result(checkgettag(fd,"key6"));
    add_result(checkgettag(fd,"key7"));

    ftag(fd, "key3", "c3");
    add_result(checkgettag(fd,"key3"));

    add_result(checkuntag(fd,"key1"));
    add_result(checkuntag(fd,"key2"));
    add_result(checkuntag(fd,"key3"));
    add_result(checkgettag(fd,"key1"));
    add_result(checkgettag(fd,"key2"));
    add_result(checkgettag(fd,"key3"));
    add_result(checkgettag(fd,"key4"));
    add_result(checkgettag(fd,"key5"));
    add_result(checkgettag(fd,"key6"));
    add_result(checkgettag(fd,"key7"));
    add_result(checkgettag(fd,"key3"));

    close(fd);

    int failed = 0;

    for (int index= 0; index != result_index; index++) {
      if (results[index] != expected[index]) {
        // Failed.
        failed++;
      }
    }

    if (failed) {
      printf(2, "Failed %d tests.\n", failed);
    }
    else {
      printf(2, "All tests passed successfully.\n");
    }

    exit();
}
