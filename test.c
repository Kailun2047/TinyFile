#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>

int main() {
    key_t key = ftok("test", 1);

    return 0;
}
