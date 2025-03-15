#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;


#define NBPORT 20
#define NPACKET 16
static struct bport
{
  uint16 port;
  char* buf[NPACKET];
  int head, tail;
} bport_list[NBPORT];


void
netinit(void)
{
  initlock(&netlock, "netlock");
}


//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//
uint64
sys_bind(void)
{
  //
  // Your code here.
  //
  int port;
  argint(0, &port);
  if (0<port && port<65536) {
    acquire(&netlock);
    struct bport* bp;
    for (bp=bport_list; bp<&bport_list[NBPORT]; bp++) {
      if (bp->port == port) {
        release(&netlock);
        return -1; // 已经绑定过
      }
    }
    for (bp=bport_list; bp<&bport_list[NBPORT]; bp++) {
      if (bp->port == 0) break;
    }
    if (bp == &bport_list[NBPORT]) {
      release(&netlock);
      return -1; // 端口绑定数目超过最大数
    }
    bp->port = port;
    release(&netlock);
    return 0;
  }
  return -1;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  //
  // Optional: Your code here.
  //
  int port;
  argint(0, &port);
  acquire(&netlock);
  struct bport* bp;
  for (bp=bport_list; bp<&bport_list[NBPORT]; bp++) {
    if (bp->port == port) {
      // 释放未读取的数据包
      bp->port = 0;
      while (bp->head != bp->tail) {
        kfree(bp->buf[bp->head]);
        bp->head = (bp->head+1)%NPACKET;
      }
    }
  }
  release(&netlock);
  return 0;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  //
  // Your code here.
  //
  struct proc *p = myproc();
  int dport;
  uint64 src;
  uint64 sport;
  uint64 buf;
  int maxlen;

  argint(0, &dport);
  argaddr(1, &src);
  argaddr(2, &sport);
  argaddr(3, &buf);
  argint(4, &maxlen);

  acquire(&netlock);
  struct bport* bp;
  for (bp=bport_list; bp<&bport_list[NBPORT]; bp++) {
    if (bp->port == dport) {
      break;
    }
  }
  if (bp == &bport_list[NBPORT]) {
    goto err; // 此端口没有绑定
  }
  if (bp->tail == bp->head) {// 无数据 睡眠
    // printf("queue [%d, %d] sleep pid %d\n", bp->head, bp->tail, myproc()->pid);
    sleep(bp, &netlock);
    if (killed(myproc())) goto err;
  }
  char* bf = bp->buf[bp->head];
  
  // 解析协议
  struct eth *eth = (struct eth *) bf;
  struct ip *ip = (struct ip *)(eth + 1);
  struct udp *udp = (struct udp *)(ip + 1);
  char *payload = (char *)(udp + 1);
  int ksrc = htonl(ip->ip_src); // 大端转小端
  short ksport = htons(udp->sport);
  uint64 len = htons(udp->ulen) - sizeof(struct udp);
  
  // printf("[recv] src:%d sport:%d len:%ld payload:\n", ksrc, ksport, len);
  // for (int i=0; i<len; i++) {
  //   printf("%x\t", payload[i]);
  // } 
  // printf("\n");
  
  if (copyout(p->pagetable, src, (char*)&ksrc, 4)<0) goto err;
  
  if (copyout(p->pagetable, sport, (char*)&ksport, 2)<0) goto err;
  
  if (len > maxlen) len = maxlen;
  if (copyout(p->pagetable, buf, payload, len)<0) goto err;
  // printf("queue [%d,%d]==>[%d,%d]\n", bp->head, bp->tail, (bp->head+1)%NPACKET, bp->tail);
  bp->head = (bp->head+1)%NPACKET;
  kfree(bf);
  release(&netlock);
  return len;

err:
  release(&netlock);
  return -1;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);
  /*
    | eth                          | ip                                                                                                     | udp                                         |
    | dhost(48) shost(48) type(16) | ip_vhl(8) ip_tos(8) ip_len(16) ip_id(16) ip_off(16) ip_ttl(8) ip_p(8) ip_sum(16) ip_src(32) ip_dst(32) | sport(16) dport(16) ulen(16) sum(16) payload|
    
    struct eth {
      uint8  dhost[ETHADDR_LEN];
      uint8  shost[ETHADDR_LEN];
      uint16 type;
    } 
    struct ip {
      uint8  ip_vhl; // version << 4 | header length >> 2
      uint8  ip_tos; // type of service
      uint16 ip_len; // total length, including this IP header
      uint16 ip_id;  // identification
      uint16 ip_off; // fragment offset field
      uint8  ip_ttl; // time to live
      uint8  ip_p;   // protocol
      uint16 ip_sum; // checksum, covers just IP header
      uint32 ip_src, ip_dst;
    };
    struct udp {
      uint16 sport; // source port
      uint16 dport; // destination port
      uint16 ulen;  // length, including udp header, not including IP header
      uint16 sum;   // checksum
    };
    payload
  */
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  e1000_transmit(buf, total);

  return 0;
}

void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  //
  // Your code here.
  //
  struct eth *eth = (struct eth *) buf;
  struct ip *ip = (struct ip *)(eth + 1);
  struct udp *udp = (struct udp *)(ip + 1);
  acquire(&netlock);
  struct bport* bp;
  for (bp=bport_list; bp<&bport_list[NBPORT]; bp++) {
    if (bp->port == htons(udp->dport)) break;
  }
  if (ip->ip_p == IPPROTO_UDP && bp != &bport_list[NBPORT] && (bp->tail + 1)%NPACKET != bp->head) {
    bp->buf[bp->tail] = buf;
    bp->tail = (bp->tail + 1)%NPACKET;
    wakeup(bp);
  } else {
    kfree(buf);
  }
  release(&netlock);
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY); // replies with the hw addr of the protocol addr

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp)); // 无论是谁发来的arp，都返回本机mac地址

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}
