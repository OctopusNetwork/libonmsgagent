#ifndef __KKT_MSG_AGENT____H__
#define __KKT_MSG_AGENT____H__

typedef enum {
    N_MSG_PRIORITY_HIGH,
    N_MSG_PRIORITY_MIDDLE,
    N_MSG_PRIORITY_LOW
} ocnet_msg_priority_t;

typedef enum {
    N_MSG_FEED_MYSELF,
    N_MSG_SEND_TOOTHER
} ocnet_msg_type_t;

typedef struct {
    int             sender_pid;
    int             receiver_pid;
    int             msg;
    void           *arg;
    unsigned char   priority:   4,
                    broadcast:  1,
                    response:   1;
} ocnet_msg_t;

typedef int (*ocnet_msg_feeder_f)(ocnet_msg_t *msg);

#ifdef __cplusplus
extern "C" {
#endif

int     ocnet_msg_agent_init(int max_tasks, int internal_sched);
int     ocnet_msg_agent_start(void);

int     ocnet_msg_agent_create_bidirect_buf(unsigned long key,
            int send_msgbuf_size, int recv_msgbuf_size,
            ocnet_msg_feeder_f msg_feeder);
int     ocnet_msg_agent_destroy_bidirect_buf(int pid);
int     ocnet_msg_agent_sendmsg(int sender_pid, int receiver_pid,
            int msg, void *arg, ocnet_msg_priority_t priority,
            ocnet_msg_type_t type, int response, int broadcast);
int     ocnet_msg_agent_sendmsg_toother(int sender_pid, int reciver_pid,
            int msg, void *arg, ocnet_msg_priority_t priority,
            int response, int broadcast);
int     ocnet_msg_agent_feedmsg_myself(int my_pid, ocnet_msg_t *msg);
int     ocnet_msg_agent_recvmsg(int pid, ocnet_msg_t *msg);

void    ocnet_msg_agent_stop(void);
void    ocnet_msg_agent_final(void);

#ifdef __cplusplus
}
#endif

#endif
