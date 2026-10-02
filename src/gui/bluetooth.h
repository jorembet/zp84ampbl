#ifndef ZP_BLUETOOTH_H
#define ZP_BLUETOOTH_H
#include <sys/socket.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
/* A single owned helper. Requests use bounded, binary IPC; no shell involved. */
static pid_t bluetooth_pid=-1;
static int bluetooth_selected,bluetooth_pending;
static unsigned char bluetooth_header[4];
static size_t bluetooth_header_size;
static struct timespec bluetooth_started;
static int bluetooth_elapsed(void)
{
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    return (int)((now.tv_sec-bluetooth_started.tv_sec)*1000+(now.tv_nsec-bluetooth_started.tv_nsec)/1000000);
}
static int bluetooth_read(int fd,void *buf,size_t length,int timeout)
{
    unsigned char *p=buf;
    while(length) {
        struct pollfd pollfd={fd,POLLIN,0};int r;
        do{r=poll(&pollfd,1,timeout);}while(r<0&&errno==EINTR);
        if(r<=0)return ZP_ERR_TIMEOUT;
        ssize_t n=read(fd,p,length);if(n<=0)return ZP_ERR_IO;
        p+=n;length-=(size_t)n;
    }
    return 0;
}
static void bluetooth_close(zp_conn *c)
{
    bluetooth_pending=0;
    if(c->fd>=0){close(c->fd);c->fd=-1;}
    if(bluetooth_pid>0){kill(bluetooth_pid,SIGKILL);waitpid(bluetooth_pid,NULL,0);bluetooth_pid=-1;}
}
static int bluetooth_xfer(zp_conn *c,const uint8_t *payload,size_t len,uint8_t cmd,uint8_t *rsp,uint8_t *out,size_t cap,size_t *count)
{
    if(count)*count=0;
    if(len>128||!payload||!out)return ZP_ERR_ARG;
    uint8_t req[131]={cmd,(uint8_t)(len>>8),(uint8_t)len},header[4];memcpy(req+3,payload,len);
    if(send(c->fd,req,len+3,MSG_NOSIGNAL)!=(ssize_t)(len+3))goto failed;
    /* A BLE read may spend several seconds retrying a missed notification or
       re-establishing GATT. Do not kill the worker before its bounded retry. */
    int budget=cmd==ZP_CMD_ID?25000:10000;
    int err=bluetooth_read(c->fd,header,4,budget);
    if(err)goto failed;
    if(header[0]) {
        if((header[0]==ZP_ERR_TIMEOUT||header[0]==ZP_ERR_ARG)&&!header[1]&&!header[2]&&!header[3]) {
            c->last_error=header[0];return header[0];
        }
        goto failed;
    }
    size_t n=((size_t)header[2]<<8)|header[3];if(n>cap)goto failed;
    if(bluetooth_read(c->fd,out,n,5000))goto failed;
    if(rsp)*rsp=header[1];
    if(count)*count=n;
    return ZP_OK;
failed:
    c->last_error=ZP_ERR_IO;bluetooth_close(c);return ZP_ERR_IO;
}
static int bluetooth_open(zp_conn *c)
{
    char path[4096];ssize_t size=readlink("/proc/self/exe",path,sizeof(path)-1);
    if(size<=0)return ZP_ERR_OPEN;
    path[size]=0;
    char *slash=strrchr(path,'/');if(!slash)return ZP_ERR_OPEN;
    if((size_t)(slash-path)+sizeof("/zp84-ble.py")>sizeof(path))return ZP_ERR_OPEN;
    strcpy(slash,"/zp84-ble.py");
    int fd[2];if(socketpair(AF_UNIX,SOCK_STREAM,0,fd))return ZP_ERR_OPEN;
    pid_t pid=fork();
    if(pid<0){close(fd[0]);close(fd[1]);return ZP_ERR_OPEN;}
    if(!pid) {
        close(fd[0]);dup2(fd[1],STDIN_FILENO);dup2(fd[1],STDOUT_FILENO);close(fd[1]);
        /* Do not inherit USB, display or application lock descriptors. */
        long max=sysconf(_SC_OPEN_MAX);if(max<0)max=65536;
        for(int n=3;n<max;n++)close(n);
        execlp("python3","python3","-u",path,(char *)NULL);_exit(127);
    }
    close(fd[1]);bluetooth_pid=pid;memset(c,0,sizeof(*c));c->fd=fd[0];
    c->custom_xfer=bluetooth_xfer;c->custom_close=bluetooth_close;
    bluetooth_header_size=0;bluetooth_pending=1;clock_gettime(CLOCK_MONOTONIC,&bluetooth_started);
    return ZP_OK;
}
/* 0 pending, 1 ready (worker already verified DSP query), -1 failed. */
static int bluetooth_poll(zp_conn *c)
{
    if(!bluetooth_pending)return 0;
    if(bluetooth_elapsed()>25000){c->last_error=ZP_ERR_TIMEOUT;bluetooth_close(c);return -1;}
    ssize_t n=recv(c->fd,bluetooth_header+bluetooth_header_size,4-bluetooth_header_size,MSG_DONTWAIT);
    if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return 0;
    if(n<=0){c->last_error=ZP_ERR_IO;bluetooth_close(c);return -1;}
    bluetooth_header_size+=(size_t)n;
    if(bluetooth_header_size<4)return 0;
    if(bluetooth_header[0]||bluetooth_header[1]||bluetooth_header[2]||bluetooth_header[3]){c->last_error=bluetooth_header[0];bluetooth_close(c);return -1;}
    bluetooth_pending=0;return 1;
}
#endif
