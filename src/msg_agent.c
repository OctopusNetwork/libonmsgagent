#include <stdlib.h>

#include "kkt_math.h"
#include "kkt_malloc.h"
#include "kkt_thread.h"

#include "kktlfds.h"
#include "kktevent.h"

#include "kkt_msgagent.h"

#define KKT_MAX_BUF_MSG_CNT     (20 * 1024)
#define KKT_DEFAULT_MSGWAIT     5000000

typedef struct {
    kkt_msg_s_t    *msgbuf;
    /* Already feed index */
    int             feed_index;
    /* To be processed index */
    int             process_index;

    int             msg_count;
    void           *cnt_mutex;
} kkt_msgcon_s_t;

typedef struct {
    unsigned char   used:   1;

    unsigned long   key;
    int             sendbuf_size;
    int             recvbuf_size;

    kkt_msgcon_s_t  msg_sendbuf_h;
    kkt_msgcon_s_t  msg_sendbuf_m;
    kkt_msgcon_s_t  msg_sendbuf_l;

    kkt_msgcon_s_t  msg_recvbuf_h;
    kkt_msgcon_s_t  msg_recvbuf_m;
    kkt_msgcon_s_t  msg_recvbuf_l;

    kkt_msg_feeder_f_t    feeder;
} kkt_msgbuf_s_t;

typedef struct {
    int             max_tasks;
    kkt_msgbuf_s_t *msgbufs;

    unsigned char   running:        1,
                    internal_sched: 1;

    void           *sched_thread;
    void           *event;

    void           *cnt_mutex;
    int             msg_total_count;
} kkt_msgagent_s_t;

static kkt_msgagent_s_t   g_msg_agent;

static int __peek_msg(kkt_msgcon_s_t *msgcon,
        int buf_size, kkt_msg_s_t *msg)
{
    kkt_msg_s_t *message = &msgcon->msgbuf[msgcon->process_index];

    msg->sender_pid = message->sender_pid;
    msg->msg = message->msg;
    msg->arg = message->arg;
    msg->priority = message->priority;
    msg->receiver_pid = message->receiver_pid;
    msg->response = message->response;

    msgcon->process_index++;
    msgcon->process_index %= buf_size;

    kkt_mutex_lock(g_msg_agent.cnt_mutex);
    g_msg_agent.msg_total_count--;
    kkt_mutex_unlock(g_msg_agent.cnt_mutex);

    kkt_mutex_lock(msgcon->cnt_mutex);
    msgcon->msg_count--;
    kkt_mutex_unlock(msgcon->cnt_mutex);

    return 0;
}

static int __msgbuf_full(kkt_msgbuf_s_t *msgbuf,
        kkt_msgcon_s_t *msgcon, int buf_size, int msg_count)
{
    return ((msgcon->feed_index == msgcon->process_index) &&
            (msg_count == buf_size));
}

static int __msgbuf_empty(kkt_msgbuf_s_t *msgbuf,
        kkt_msgcon_s_t *msgcon)
{
    int msg_count = 0;

    kkt_mutex_lock(msgcon->cnt_mutex);
    msg_count = msgcon->msg_count;
    kkt_mutex_unlock(msgcon->cnt_mutex);

    return ((msgcon->feed_index == msgcon->process_index) &&
            (0 == msg_count));
}

static int __peek_recv_hp_msg(kkt_msgbuf_s_t *msgbuf, kkt_msg_s_t *msg)
{
    kkt_msgcon_s_t *msgcon = &msgbuf->msg_recvbuf_h;

    if (0 == __msgbuf_empty(msgbuf, msgcon)) {
        return __peek_msg(msgcon, msgbuf->recvbuf_size, msg);
    }

    return -1;
}

static int __peek_recv_mp_msg(kkt_msgbuf_s_t *msgbuf, kkt_msg_s_t *msg)
{
    kkt_msgcon_s_t *msgcon = &msgbuf->msg_recvbuf_m;

    if (0 == __msgbuf_empty(msgbuf, msgcon)) {
        return __peek_msg(msgcon, msgbuf->recvbuf_size, msg);
    }

    return -1;
}

static int __peek_recv_lp_msg(kkt_msgbuf_s_t *msgbuf, kkt_msg_s_t *msg)
{
    kkt_msgcon_s_t *msgcon = &msgbuf->msg_recvbuf_l;

    if (0 == __msgbuf_empty(msgbuf, msgcon)) {
        return __peek_msg(msgcon, msgbuf->recvbuf_size, msg);
    }

    return -1;
}

static int __peek_send_hp_msg(kkt_msgbuf_s_t *msgbuf, kkt_msg_s_t *msg)
{
    kkt_msgcon_s_t *msgcon = &msgbuf->msg_sendbuf_h;

    if (0 == __msgbuf_empty(msgbuf, msgcon)) {
        return __peek_msg(msgcon, msgbuf->sendbuf_size, msg);
    }

    return -1;
}

static int __peek_send_mp_msg(kkt_msgbuf_s_t *msgbuf, kkt_msg_s_t *msg)
{
    kkt_msgcon_s_t *msgcon = &msgbuf->msg_sendbuf_m;
    if (0 == __msgbuf_empty(msgbuf, msgcon)) {
        return __peek_msg(msgcon, msgbuf->sendbuf_size, msg);
    }

    return -1;
}

static int __peek_send_lp_msg(kkt_msgbuf_s_t *msgbuf, kkt_msg_s_t *msg)
{
    kkt_msgcon_s_t *msgcon = &msgbuf->msg_sendbuf_l;
    if (0 == __msgbuf_empty(msgbuf, msgcon)) {
        return __peek_msg(msgcon, msgbuf->sendbuf_size, msg);
    }

    return -1;
}

static int ___peek_send_msg(kkt_msgbuf_s_t *msgbuf,
        kkt_msg_priority_e_t priority, kkt_msg_s_t *msg)
{
    switch (priority) {
    case N_MSG_PRIORITY_HIGH:
        if (0 == __peek_send_hp_msg(msgbuf, msg)) {
            return 0;
        }
        break;
    case N_MSG_PRIORITY_MIDDLE:
        if (0 == __peek_send_mp_msg(msgbuf, msg)) {
            return 0;
        }
        break;
    case N_MSG_PRIORITY_LOW:
        if (0 == __peek_send_lp_msg(msgbuf, msg)) {
            return 0;
        }
        break;
    }

    return -1;
}

/**
 * Return the last processed index
 */
static int __peek_send_msg(kkt_msgagent_s_t *msgagent,
        kkt_msg_priority_e_t priority, kkt_msg_s_t *msg,
        int *next_process_index)
{
    int i = *next_process_index;

    for (; i < msgagent->max_tasks; i++) {
        kkt_msgbuf_s_t *msgbuf = &msgagent->msgbufs[i];
        if (msgbuf->used) {
            if (0 == ___peek_send_msg(msgbuf, priority, msg)) {
                *next_process_index = i;
                return 0;
            }
        }
    }

    return -1;
}

static int __fetch_msg(kkt_msgagent_s_t *msgagent,
        int *next_process_index, kkt_msg_s_t *msg)
{
    if (__peek_send_msg(msgagent, N_MSG_PRIORITY_HIGH,
                msg, next_process_index) < 0) {
        if (__peek_send_msg(msgagent, N_MSG_PRIORITY_MIDDLE,
                    msg, next_process_index) < 0) {
            if (__peek_send_msg(msgagent, N_MSG_PRIORITY_LOW,
                        msg, next_process_index) < 0) {
                /* Empty message, here we listen event again */
                *next_process_index += 1;
                *next_process_index %= msgagent->max_tasks;
                return -1;
            }
        }
    }

    return 0;
}

static int __process_msg(kkt_msgagent_s_t *msgagent, kkt_msg_s_t *msg)
{
    int i = 0;
    kkt_msgbuf_s_t *msgbuf = NULL;

    /* Response to specific app */
    if (1 == msg->response) {
        msgbuf = &msgagent->msgbufs[msg->receiver_pid];
        if (NULL != msgbuf->feeder) {
            return msgbuf->feeder(msg);
        }
        return -1;
    }

    /* Dispatch to service who can process it */
    for (i = 0; i < msgagent->max_tasks; i++) {
        msgbuf = &msgagent->msgbufs[i];
        if (NULL != msgbuf->feeder) {
            int rc = 0;
            rc = msgbuf->feeder(msg);
            if (0 == msg->broadcast && 0 == rc) {
                return rc;
            }
        }
    }

    return -1;
}

static void *__schedule(void *arg)
{
    kkt_msgagent_s_t *msgagent = (kkt_msgagent_s_t *)arg;
    int next_process_index = 0;
    int rc = 0;
    int wait = KKT_DEFAULT_MSGWAIT;
    void *lfds = kkt_lfds_new();

    if (NULL == lfds) {
        return NULL;
    }

    do {
        kkt_msg_s_t msg;
        rc = kkt_event_wait(msgagent->event, lfds, wait);
        if (rc < 0) {
        } else {
            if (__fetch_msg(msgagent, &next_process_index, &msg) < 0) {
                kkt_mutex_lock(g_msg_agent.cnt_mutex);
                if (0 == g_msg_agent.msg_total_count) {
                    wait = KKT_DEFAULT_MSGWAIT;
                } else {
                    wait = 0;
                }
                kkt_mutex_unlock(g_msg_agent.cnt_mutex);
                continue;
            }
            /* Got a message, let different module to feed to themselves */
            __process_msg(msgagent, &msg);
            wait = 0;
        }
    } while (1 == msgagent->running);

    kkt_lfds_del(lfds);

    return NULL;
}

int kkt_msg_agent_init(int max_tasks, int internal_sched)
{
    kkt_memset(&g_msg_agent, 0x0, sizeof(g_msg_agent));
    g_msg_agent.max_tasks = max_tasks;

    g_msg_agent.msgbufs = kkt_malloc(max_tasks * sizeof(kkt_msgbuf_s_t));
    if (NULL == g_msg_agent.msgbufs) {
        return -1;
    }
    kkt_memset(g_msg_agent.msgbufs, 0x0, max_tasks * sizeof(kkt_msgbuf_s_t));
    g_msg_agent.event = kkt_event_create(1,
            KKT_EVENT_READ | KKT_EVENT_ERROR, 1, 0, 0);
    if (NULL == g_msg_agent.event) {
        kkt_free(g_msg_agent.msgbufs);
        return -1;
    }

    g_msg_agent.cnt_mutex = kkt_mutex_init();
    if (NULL == g_msg_agent.cnt_mutex) {
        kkt_event_destroy(g_msg_agent.event);
        kkt_free(g_msg_agent.msgbufs);
    }

    g_msg_agent.internal_sched = internal_sched;
    return 0;
}

int kkt_msg_agent_start(void)
{
    if (0 != g_msg_agent.internal_sched) {
        g_msg_agent.running = 1;
        g_msg_agent.sched_thread = kkt_thread_create(__schedule, &g_msg_agent);
        if (NULL == g_msg_agent.sched_thread) {
            return -1;
        }
    }

    return 0;
}

static int ___create_msgbuf(kkt_msgcon_s_t *msgcon,
        int sendbuf_msg_cnt, int recvbuf_msg_cnt)
{
    msgcon->msgbuf = kkt_malloc(
            recvbuf_msg_cnt * sizeof(kkt_msg_s_t));
    if (NULL == msgcon->msgbuf) {
        return -1;
    }

    msgcon->msg_count = 0;
    msgcon->cnt_mutex = kkt_mutex_init();
    if (NULL == msgcon->cnt_mutex) {
        kkt_free(msgcon->msgbuf);
        return -1;
    }

    return 0;
}

static void ___destroy_msgbuf(kkt_msgcon_s_t *msgcon)
{
    kkt_mutex_destroy(msgcon->cnt_mutex);
    kkt_free(msgcon->msgbuf);
}

static int __create_msgbuf(
        kkt_msgbuf_s_t *msgbuf, unsigned long key,
        int sendbuf_msg_cnt, int recvbuf_msg_cnt,
        kkt_msg_feeder_f_t feeder)
{
    msgbuf->sendbuf_size = sendbuf_msg_cnt;
    msgbuf->recvbuf_size = recvbuf_msg_cnt;

    msgbuf->key = key;

    if (___create_msgbuf(&msgbuf->msg_recvbuf_h,
                sendbuf_msg_cnt, recvbuf_msg_cnt) < 0) {
        return -1;
    }

    if (___create_msgbuf(&msgbuf->msg_recvbuf_m,
                sendbuf_msg_cnt, recvbuf_msg_cnt) < 0) {
        goto L_ERROR_RECVBUF_M_CREATE;
    }

    if (___create_msgbuf(&msgbuf->msg_recvbuf_l,
                sendbuf_msg_cnt, recvbuf_msg_cnt) < 0) {
        goto L_ERROR_RECVBUF_L_CREATE;
    }

    if (___create_msgbuf(&msgbuf->msg_sendbuf_h,
                sendbuf_msg_cnt, recvbuf_msg_cnt) < 0) {
        goto L_ERROR_SENDBUF_H_CREATE;
    }

    if (___create_msgbuf(&msgbuf->msg_sendbuf_m,
                sendbuf_msg_cnt, recvbuf_msg_cnt) < 0) {
        goto L_ERROR_SENDBUF_M_CREATE;
    }

    if (___create_msgbuf(&msgbuf->msg_sendbuf_l,
                sendbuf_msg_cnt, recvbuf_msg_cnt) < 0) {
        goto L_ERROR_SENDBUF_L_CREATE;
    }

    msgbuf->feeder = feeder;
    msgbuf->used = 1;

    return 0;

L_ERROR_SENDBUF_L_CREATE:
    ___destroy_msgbuf(&msgbuf->msg_sendbuf_m);
L_ERROR_SENDBUF_M_CREATE:
    ___destroy_msgbuf(&msgbuf->msg_sendbuf_h);
L_ERROR_SENDBUF_H_CREATE:
    ___destroy_msgbuf(&msgbuf->msg_recvbuf_l);
L_ERROR_RECVBUF_L_CREATE:
    ___destroy_msgbuf(&msgbuf->msg_recvbuf_m);
L_ERROR_RECVBUF_M_CREATE:
    ___destroy_msgbuf(&msgbuf->msg_recvbuf_h);
    return -1;
}

static int __destroy_msgbuf(kkt_msgbuf_s_t *msgbuf)
{
    ___destroy_msgbuf(&msgbuf->msg_sendbuf_l);
    ___destroy_msgbuf(&msgbuf->msg_sendbuf_m);
    ___destroy_msgbuf(&msgbuf->msg_sendbuf_h);
    ___destroy_msgbuf(&msgbuf->msg_recvbuf_l);
    ___destroy_msgbuf(&msgbuf->msg_recvbuf_m);
    ___destroy_msgbuf(&msgbuf->msg_recvbuf_h);

    return 0;
}

int kkt_msg_agent_create_bidirect_buf(unsigned long key,
        int sendbuf_msg_cnt, int recvbuf_msg_cnt,
        kkt_msg_feeder_f_t msg_feeder)
{
    int i = 0;

    sendbuf_msg_cnt = __MIN2(KKT_MAX_BUF_MSG_CNT, sendbuf_msg_cnt);
    recvbuf_msg_cnt = __MIN2(KKT_MAX_BUF_MSG_CNT, recvbuf_msg_cnt);

    for (i = 0; i < g_msg_agent.max_tasks; i++) {
        kkt_msgbuf_s_t *msgbuf = &g_msg_agent.msgbufs[i];
        if (0 == msgbuf->used) {
            if (__create_msgbuf(msgbuf, key,
                        sendbuf_msg_cnt,
                        recvbuf_msg_cnt,
                        msg_feeder) < 0) {
                return -1;
            }

            break;
        }
    }

    return i;
}

int kkt_msg_agent_destroy_bidirect_buf(int pid)
{
    kkt_msgbuf_s_t *msgbuf = &g_msg_agent.msgbufs[pid];
    return __destroy_msgbuf(msgbuf);
}

static kkt_msgcon_s_t *__get_msgcon(kkt_msgbuf_s_t *msgbuf,
        kkt_msg_type_e_t type, kkt_msg_priority_e_t priority)
{
    kkt_msgcon_s_t *msgcon = NULL;

    switch (priority) {
    case N_MSG_PRIORITY_HIGH:
        msgcon = (N_MSG_FEED_MYSELF == type) ?
            &msgbuf->msg_recvbuf_h :
            &msgbuf->msg_sendbuf_h;
        break;
    case N_MSG_PRIORITY_MIDDLE:
        msgcon = (N_MSG_FEED_MYSELF == type) ?
            &msgbuf->msg_recvbuf_m :
            &msgbuf->msg_sendbuf_m;
        break;
    case N_MSG_PRIORITY_LOW:
        msgcon = (N_MSG_FEED_MYSELF == type) ?
            &msgbuf->msg_recvbuf_l :
            &msgbuf->msg_sendbuf_l;
        break;
    default:
        return NULL;
    }

    return msgcon;
}

int kkt_msg_agent_sendmsg(int sender_pid, int receiver_pid,
        int msg, void *arg, kkt_msg_priority_e_t priority,
        kkt_msg_type_e_t type, int response, int broadcast)
{
    kkt_msgbuf_s_t *msgbuf = &g_msg_agent.msgbufs[sender_pid];
    kkt_msgcon_s_t *msgcon = NULL;
    kkt_msg_s_t *message = NULL;
    int buf_size = 0;
    int msg_count = 0;

    if (N_MSG_FEED_MYSELF == type) {
        msgbuf = &g_msg_agent.msgbufs[receiver_pid];
        buf_size = msgbuf->recvbuf_size;
    } else if (N_MSG_SEND_TOOTHER == type) {
        msgbuf = &g_msg_agent.msgbufs[sender_pid];
        buf_size = msgbuf->sendbuf_size;
    } else {
        return -1;
    }

    if (NULL == (msgcon = __get_msgcon(msgbuf, type, priority))) {
        return -1;
    }

    kkt_mutex_lock(msgcon->cnt_mutex);
    msg_count = msgcon->msg_count;
    kkt_mutex_unlock(msgcon->cnt_mutex);

    if (1 == __msgbuf_full(msgbuf, msgcon, buf_size, msg_count)) {
        return -1;
    }

    message = &msgcon->msgbuf[msgcon->feed_index];
    message->sender_pid = sender_pid;
    message->receiver_pid = receiver_pid;
    message->msg = msg;
    message->arg = arg;
    message->priority = priority;
    message->response = response;

    /* Single producer and single consumer firstly */
    msgcon->feed_index += 1;
    msgcon->feed_index %= buf_size;

    kkt_mutex_lock(g_msg_agent.cnt_mutex);
    g_msg_agent.msg_total_count++;
    kkt_mutex_unlock(g_msg_agent.cnt_mutex);

    kkt_mutex_lock(msgcon->cnt_mutex);
    msgcon->msg_count++;
    kkt_mutex_unlock(msgcon->cnt_mutex);

    kkt_event_wakeup(g_msg_agent.event);

    return 0;
}

int kkt_msg_agent_sendmsg_toother(int sender_pid, int receiver_pid,
        int msg, void *arg, kkt_msg_priority_e_t priority,
        int response, int broadcast)
{
    return kkt_msg_agent_sendmsg(sender_pid, receiver_pid,
            msg, arg, priority, N_MSG_SEND_TOOTHER,
            response, broadcast);
}

int kkt_msg_agent_feedmsg_myself(int my_pid, kkt_msg_s_t *msg)
{
    return kkt_msg_agent_sendmsg(msg->sender_pid, my_pid, msg->msg,
            msg->arg, msg->priority, N_MSG_FEED_MYSELF, 0, 0);
}

int kkt_msg_agent_recvmsg(int pid, kkt_msg_s_t *msg)
{
    kkt_msgbuf_s_t *msgbuf = &g_msg_agent.msgbufs[pid];

    if (0 == __peek_recv_hp_msg(msgbuf, msg)) {
        return 0;
    }

    if (0 == __peek_recv_mp_msg(msgbuf, msg)) {
        return 0;
    }

    if (0 == __peek_recv_lp_msg(msgbuf, msg)) {
        return 0;
    }

    return -1;
}

void kkt_msg_agent_stop(void)
{
    g_msg_agent.running = 0;
    if (1 == g_msg_agent.internal_sched) {
        kkt_event_wakeup(g_msg_agent.event);
        kkt_thread_join(g_msg_agent.sched_thread);
    }
}

void kkt_msg_agent_final(void)
{
    kkt_mutex_destroy(g_msg_agent.cnt_mutex);
    kkt_event_destroy(g_msg_agent.event);
    kkt_free(g_msg_agent.msgbufs);
}
