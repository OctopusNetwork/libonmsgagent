#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "kkt_msgagent.h"

static int g_running = 0;

static void __sigint_handler(int sig)
{
    g_running = 0;
}

int main(int argc, char *argv[])
{
    int msgbuf_pid = -1;
    int send_count = 0;

    if (kkt_msg_agent_init(32, 1) < 0) {
        return -1;
    }

    if ((msgbuf_pid = kkt_msg_agent_create_bidirect_buf(0x9876, 20, 20, NULL)) < 0) {
        kkt_msg_agent_final();
        return -1;
    }

    signal(SIGINT, __sigint_handler);
    g_running = 1;

    kkt_msg_agent_start();

    do {
        if (kkt_msg_agent_sendmsg_toother(msgbuf_pid, msgbuf_pid, 5, NULL,
                N_MSG_PRIORITY_HIGH, 0, 0) < 0) {
            printf("Full...\n");
        } else {
            send_count++;
            printf("Send %d msgs\n", send_count);
        }
        sleep(1);
    } while (1 == g_running);

    kkt_msg_agent_stop();

    kkt_msg_agent_destroy_bidirect_buf(msgbuf_pid);
    kkt_msg_agent_final();

    return 0;
}
