#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    /* (Final) TODO BEGIN */
    if (argc < 2) {
        printf("Error: command `mkdir` requires at least 2 arguments, but 1 given.\n");
        return 1;
    }
    for (int i = 1; i < argc; i++){
        if (mkdirat(AT_FDCWD, argv[i], 0) < 0){
            printf("%s failed\n", argv[i]);
        }
    }
    return 0;
    /* (Final) TODO END */
    exit(0);
}