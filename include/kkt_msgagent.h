#ifndef __KKT_MSG_AGENT____H__
#define __KKT_MSG_AGENT____H__

typedef enum {
    N_MSG_PRIORITY_HIGH,
    N_MSG_PRIORITY_MIDDLE,
    N_MSG_PRIORITY_LOW
} kkt_msg_priority_e_t;

typedef enum {
    N_MSG_FEED_MYSELF,
    N_MSG_SEND_TOOTHER
} kkt_msg_type_e_t;

typedef struct {
    int             sender_pid;
    int             receiver_pid;
    int             msg;
    void           *arg;
    unsigned char   priority:   4,
                    broadcast:  1,
                    response:   1;
} kkt_msg_s_t;

typedef int (*kkt_msg_feeder_f_t)(kkt_msg_s_t *msg);

#ifdef __cplusplus
extern "C" {
#endif

int     kkt_msg_agent_init(int max_tasks, int internal_sched);
int     kkt_msg_agent_start(void);

int     kkt_msg_agent_create_bidirect_buf(unsigned long key,
            int send_msgbuf_size, int recv_msgbuf_size,
            kkt_msg_feeder_f_t msg_feeder);
int     kkt_msg_agent_destroy_bidirect_buf(int pid);
int     kkt_msg_agent_sendmsg(int sender_pid, int receiver_pid,
            int msg, void *arg, kkt_msg_priority_e_t priority,
            kkt_msg_type_e_t type, int response, int broadcast);
int     kkt_msg_agent_sendmsg_toother(int sender_pid, int reciver_pid,
            int msg, void *arg, kkt_msg_priority_e_t priority,
            int response, int broadcast);
int     kkt_msg_agent_feedmsg_myself(int my_pid, kkt_msg_s_t *msg);
int     kkt_msg_agent_recvmsg(int pid, kkt_msg_s_t *msg);

void    kkt_msg_agent_stop(void);
void    kkt_msg_agent_final(void);

#ifdef __cplusplus
}
#endif

#endif
