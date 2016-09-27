//=====================================================================
//
// KCP - A Better ARQ Protocol Implementation
// skywind3000 (at) gmail.com, 2010-2011
//  
// Features:
// + Average RTT reduce 30% - 40% vs traditional ARQ like tcp.
// + Maximum RTT reduce three times vs tcp.
// + Lightweight, distributed as a single source file.
//
//=====================================================================
#ifndef __IKCP_H__
#define __IKCP_H__

#include <stddef.h>
#include <stdlib.h>
#include <assert.h>

/*
建议不使用ikcp，原因：
一，实现思路和我们的需求不一样
ikcp是面向字节流的，必须保证每个字节都传到；
我们要求实时数据，每xx毫秒若干帧数据，优先发下一个时间片要播放的数据。之前的数据能重传则重传，如果已经过了播放时间则直接丢弃。
如果用ikcp，则遇到丢包，乱序的情况就会导致随着连线时间，延时累积增大。
因为涉及整个实现方案，所以改ikcp来做实时传输工作量也不小，而且代码会比较纠结。

二，ikcp代码写的很蛋疼，虽然没有发现明显bug，但是有很多性能低下/代码不合理的地方。
1 官方文档描述kcp优势时，主要理由包括：tcp没有sack功能，快速重传，nodelay所以性能不行，其实tcp都有
  其实kcp的主要优势是加大了拥塞窗口，加快了重传。前者通过改/proc/下的tcp参数也能有类似的效果。不知道作者在测延时对比的时候是否有优化tcp默认参数。
  因为kcp是应用程序，受进程调度周期的影响，会附带上更大的延时，这一点作者也没有做过分析和评测
2 没有任何对数据包头格式，传输协议的描述。代码几乎无注释
3 send时不会立即触发发包，只能通过调用ikcp_update触发，ikcp_update里面又设置了最短触发时间（代码写死最小不能小于10ms）
  这里就额外引入了10ms延时。再加上应用程序调度有延时，在机器负载较高时，还会引入更多的延时
  send应该有一个触发立即发包的机制，“小数据包合并发送”可以作为一个附加机制，而不是要求所有数据包都延迟发送尝试合并
4 所有数据包不挂定时器，而是需要我们定时遍历所有socket的所有收发包队列判断是否要触发超时......当非活跃连接较多时性能会差一个数量级
  估计这也是限制ikcp_flush最小10ms的原因
5 发包时需要从外层buff拷贝到包buff，再拷贝到发送buff，额外增加了两次拷贝，影响性能
6 协议设计采用'每个协议包只干自己的事'，然后'一个底层包可以发多个协议包'的方式。一般设计的好的协议会做成'一个协议包可以干多件事'
  例如要发10个ack，在kcp里面要发10个ack包（可合并到一个底层包）；而设计良好时应该是一个kcp包就带了10个ack，以及在数据包里顺手就把ack捎回去了
7 很奇怪为什么不用ntoh做大小端转码

 */



//=====================================================================
// 32BIT INTEGER DEFINITION 
//=====================================================================
#ifndef __INTEGER_32_BITS__
#define __INTEGER_32_BITS__
#if defined(_WIN64) || defined(WIN64) || defined(__amd64__) || \
	defined(__x86_64) || defined(__x86_64__) || defined(_M_IA64) || \
	defined(_M_AMD64)
	typedef unsigned int ISTDUINT32;
	typedef int ISTDINT32;
#elif defined(_WIN32) || defined(WIN32) || defined(__i386__) || \
	defined(__i386) || defined(_M_X86)
	typedef unsigned long ISTDUINT32;
	typedef long ISTDINT32;
#elif defined(__MACOS__)
	typedef UInt32 ISTDUINT32;
	typedef SInt32 ISTDINT32;
#elif defined(__APPLE__) && defined(__MACH__)
	#include <sys/types.h>
	typedef u_int32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#elif defined(__BEOS__)
	#include <sys/inttypes.h>
	typedef u_int32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#elif (defined(_MSC_VER) || defined(__BORLANDC__)) && (!defined(__MSDOS__))
	typedef unsigned __int32 ISTDUINT32;
	typedef __int32 ISTDINT32;
#elif defined(__GNUC__)
	#include <stdint.h>
	typedef uint32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#else 
	typedef unsigned long ISTDUINT32; 
	typedef long ISTDINT32;
#endif
#endif


//=====================================================================
// Integer Definition
//=====================================================================
#ifndef __IINT8_DEFINED
#define __IINT8_DEFINED
typedef char IINT8;
#endif

#ifndef __IUINT8_DEFINED
#define __IUINT8_DEFINED
typedef unsigned char IUINT8;
#endif

#ifndef __IUINT16_DEFINED
#define __IUINT16_DEFINED
typedef unsigned short IUINT16;
#endif

#ifndef __IINT16_DEFINED
#define __IINT16_DEFINED
typedef short IINT16;
#endif

#ifndef __IINT32_DEFINED
#define __IINT32_DEFINED
typedef ISTDINT32 IINT32;
#endif

#ifndef __IUINT32_DEFINED
#define __IUINT32_DEFINED
typedef ISTDUINT32 IUINT32;
#endif

#ifndef __IINT64_DEFINED
#define __IINT64_DEFINED
#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef __int64 IINT64;
#else
typedef long long IINT64;
#endif
#endif

#ifndef __IUINT64_DEFINED
#define __IUINT64_DEFINED
#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef unsigned __int64 IUINT64;
#else
typedef unsigned long long IUINT64;
#endif
#endif

#ifndef INLINE
#if defined(__GNUC__)

#if (__GNUC__ > 3) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1))
#define INLINE         __inline__ __attribute__((always_inline))
#else
#define INLINE         __inline__
#endif

#elif (defined(_MSC_VER) || defined(__BORLANDC__) || defined(__WATCOMC__))
#define INLINE __inline
#else
#define INLINE 
#endif
#endif

#if (!defined(__cplusplus)) && (!defined(inline))
#define inline INLINE
#endif


//=====================================================================
// QUEUE DEFINITION                                                  
//=====================================================================
#ifndef __IQUEUE_DEF__
#define __IQUEUE_DEF__

struct IQUEUEHEAD {
	struct IQUEUEHEAD *next, *prev;
};

typedef struct IQUEUEHEAD iqueue_head;


//---------------------------------------------------------------------
// queue init                                                         
//---------------------------------------------------------------------
#define IQUEUE_HEAD_INIT(name) { &(name), &(name) }
#define IQUEUE_HEAD(name) \
	struct IQUEUEHEAD name = IQUEUE_HEAD_INIT(name)

#define IQUEUE_INIT(ptr) ( \
	(ptr)->next = (ptr), (ptr)->prev = (ptr))

#define IOFFSETOF(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#define ICONTAINEROF(ptr, type, member) ( \
		(type*)( ((char*)((type*)ptr)) - IOFFSETOF(type, member)) )

#define IQUEUE_ENTRY(ptr, type, member) ICONTAINEROF(ptr, type, member)


//---------------------------------------------------------------------
// queue operation                     
//---------------------------------------------------------------------
#define IQUEUE_ADD(node, head) ( \
	(node)->prev = (head), (node)->next = (head)->next, \
	(head)->next->prev = (node), (head)->next = (node))

#define IQUEUE_ADD_TAIL(node, head) ( \
	(node)->prev = (head)->prev, (node)->next = (head), \
	(head)->prev->next = (node), (head)->prev = (node))

#define IQUEUE_DEL_BETWEEN(p, n) ((n)->prev = (p), (p)->next = (n))

#define IQUEUE_DEL(entry) (\
	(entry)->next->prev = (entry)->prev, \
	(entry)->prev->next = (entry)->next, \
	(entry)->next = 0, (entry)->prev = 0)

#define IQUEUE_DEL_INIT(entry) do { \
	IQUEUE_DEL(entry); IQUEUE_INIT(entry); } while (0)

#define IQUEUE_IS_EMPTY(entry) ((entry) == (entry)->next)

#define iqueue_init		IQUEUE_INIT
#define iqueue_entry	IQUEUE_ENTRY
#define iqueue_add		IQUEUE_ADD
#define iqueue_add_tail	IQUEUE_ADD_TAIL
#define iqueue_del		IQUEUE_DEL
#define iqueue_del_init	IQUEUE_DEL_INIT
#define iqueue_is_empty IQUEUE_IS_EMPTY

#define IQUEUE_FOREACH(iterator, head, TYPE, MEMBER) \
	for ((iterator) = iqueue_entry((head)->next, TYPE, MEMBER); \
		&((iterator)->MEMBER) != (head); \
		(iterator) = iqueue_entry((iterator)->MEMBER.next, TYPE, MEMBER))

#define iqueue_foreach(iterator, head, TYPE, MEMBER) \
	IQUEUE_FOREACH(iterator, head, TYPE, MEMBER)

#define iqueue_foreach_entry(pos, head) \
	for( (pos) = (head)->next; (pos) != (head) ; (pos) = (pos)->next )
	

#define __iqueue_splice(list, head) do {	\
		iqueue_head *first = (list)->next, *last = (list)->prev; \
		iqueue_head *at = (head)->next; \
		(first)->prev = (head), (head)->next = (first);		\
		(last)->next = (at), (at)->prev = (last); }	while (0)

#define iqueue_splice(list, head) do { \
	if (!iqueue_is_empty(list)) __iqueue_splice(list, head); } while (0)

#define iqueue_splice_init(list, head) do {	\
	iqueue_splice(list, head);	iqueue_init(list); } while (0)


#ifdef _MSC_VER
#pragma warning(disable:4311)
#pragma warning(disable:4312)
#pragma warning(disable:4996)
#endif

#endif


//---------------------------------------------------------------------
// WORD ORDER
//---------------------------------------------------------------------
#ifndef IWORDS_BIG_ENDIAN
    #ifdef _BIG_ENDIAN_
        #if _BIG_ENDIAN_
            #define IWORDS_BIG_ENDIAN 1
        #endif
    #endif
    #ifndef IWORDS_BIG_ENDIAN
        #if defined(__hppa__) || \
            defined(__m68k__) || defined(mc68000) || defined(_M_M68K) || \
            (defined(__MIPS__) && defined(__MISPEB__)) || \
            defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
            defined(__sparc__) || defined(__powerpc__) || \
            defined(__mc68000__) || defined(__s390x__) || defined(__s390__)
            #define IWORDS_BIG_ENDIAN 1
        #endif
    #endif
    #ifndef IWORDS_BIG_ENDIAN
        #define IWORDS_BIG_ENDIAN  0
    #endif
#endif



//=====================================================================
// SEGMENT
//=====================================================================
struct IKCPSEG
{
	struct IQUEUEHEAD node;
	IUINT32 conv;
	IUINT32 cmd;
	IUINT32 frg;
	IUINT32 wnd;
	IUINT32 ts;
	IUINT32 sn;
	IUINT32 una;
	IUINT32 len;
	IUINT32 resendts;
	IUINT32 rto;
	IUINT32 fastack;
	IUINT32 xmit;
	char data[1];
};


//---------------------------------------------------------------------
// IKCPCB
//---------------------------------------------------------------------
struct IKCPCB
{
	IUINT32 conv, mtu, mss, state;
	IUINT32 snd_una, snd_nxt, rcv_nxt;
	//snd_nxt 下一个要发送的序列号
	//snd_una 已确认序列号
	//rcv_nxt 下一个满足序列号，要收的包
	IUINT32 ts_recent, ts_lastack, ssthresh;
	//ssthresh 慢启动阈值，控制cwnd是快速增长还是慢增长
	//ts_recent 完全没有用到，逗你玩的变量
	//ts_lastack 同上
	IINT32 rx_rttval, rx_srtt, rx_rto, rx_minrto;
	//rx_rto 控制重传时间，标准算法是rx_srtt + rx_rttval，这里用的是rx_srtt + max(1, rx_rttval)
	//假设网络延迟服从正态分布，这里接近50%都需要重传
	//rx_rttval 平滑后的偏差时间
	//rx_srtt 平滑后的rtt
	IUINT32 snd_wnd, rcv_wnd, rmt_wnd, cwnd, probe;
	//rmt_wnd 对端接收窗口，满了就不能再发包了，会向对端发一个请求扩大窗口的包
	//snd_wnd 发送窗口，限制一次ikcp_flush不要发太多了，并没有什么用的东西
	//rcv_wnd 接收窗口，rcv_queue只能保持这么大，不然就要放在rcv_buf中，完全没有任何好处的东西
	//cwnd 拥塞窗口，每次收到包++，我操增长这么慢；
	//在触发了快速重传，cwnd会追加上fastresend的大小1
	IUINT32 current, interval, ts_flush, xmit;
	//xmit 重传次数，仅用于统计
	//current 最后一次调ikcp_update的时间
	//ts_flush 在这个时间点之后才能ikcp_flush
	//interval 触发ts_flush的时间间隔，最短10毫秒
	IUINT32 nrcv_buf, nsnd_buf;
	//nrcv_buf 残留在rcv_buf的包数量
	//nsnd_buf 残留在snd_buf的包数量
	IUINT32 nrcv_que, nsnd_que;
	//nrcv_que 残留在rcv_queue的包数量
	//nsnd_que 残留在snd_queue的包数量
	IUINT32 nodelay, updated;
	//updated 是否第一次触发ikcp_flush
	IUINT32 ts_probe, probe_wait;	//ts_probe发送窗口扩大请求的时间；probe_wait需要等待多久才发送，每次发送之后会按1.5倍增长。下次需要发送时重置
	IUINT32 dead_link, incr;
	struct IQUEUEHEAD snd_queue;	//待发送的数据，只存了原始数据没有构造完整包头；在ikcp_flush中会把满足各种窗口的包扔到snd_buf里面
	struct IQUEUEHEAD rcv_queue;	//收到的数据包，并且已经排序成功没有空洞的挂在这里
	struct IQUEUEHEAD snd_buf;		//构造好了包头，满足滑动缓冲区的数据包。只要进了这里就不会管拥塞等了，每次ikcp_flush会遍历这里所有包，没法直接发，有发的判断重传等
	struct IQUEUEHEAD rcv_buf;		//收到的数据包先存在这里
	IUINT32 *acklist;				//待回复的ack，最后会把每个ack构造成完成协议包；但是会把多个协议包合并在一个udp中发出.....
	IUINT32 ackcount;				//待回复ack数量
	IUINT32 ackblock;				//acklist大小
	void *user;
	char *buffer;					//发包时用的缓冲区，在这里构造要发的数据包，然后调发送函数
	int fastresend;
	int nocwnd, stream;
	int logmask;
	int (*output)(const char *buf, int len, struct IKCPCB *kcp, void *user);	//发送函数
	void (*writelog)(const char *log, struct IKCPCB *kcp, void *user);
};


typedef struct IKCPCB ikcpcb;

#define IKCP_LOG_OUTPUT			1
#define IKCP_LOG_INPUT			2
#define IKCP_LOG_SEND			4
#define IKCP_LOG_RECV			8
#define IKCP_LOG_IN_DATA		16
#define IKCP_LOG_IN_ACK			32
#define IKCP_LOG_IN_PROBE		64
#define IKCP_LOG_IN_WINS		128
#define IKCP_LOG_OUT_DATA		256
#define IKCP_LOG_OUT_ACK		512
#define IKCP_LOG_OUT_PROBE		1024
#define IKCP_LOG_OUT_WINS		2048

#ifdef __cplusplus
extern "C" {
#endif

//---------------------------------------------------------------------
// interface
//---------------------------------------------------------------------

// create a new kcp control object, 'conv' must equal in two endpoint
// from the same connection. 'user' will be passed to the output callback
// output callback can be setup like this: 'kcp->output = my_udp_output'
ikcpcb* ikcp_create(IUINT32 conv, void *user);

// release kcp control object
void ikcp_release(ikcpcb *kcp);

// set output callback, which will be invoked by kcp
void ikcp_setoutput(ikcpcb *kcp, int (*output)(const char *buf, int len, 
	ikcpcb *kcp, void *user));

// user/upper level recv: returns size, returns below zero for EAGAIN
int ikcp_recv(ikcpcb *kcp, char *buffer, int len);

// user/upper level send, returns below zero for error
int ikcp_send(ikcpcb *kcp, const char *buffer, int len);

// update state (call it repeatedly, every 10ms-100ms), or you can ask 
// ikcp_check when to call it again (without ikcp_input/_send calling).
// 'current' - current timestamp in millisec. 
void ikcp_update(ikcpcb *kcp, IUINT32 current);

// Determine when should you invoke ikcp_update:
// returns when you should invoke ikcp_update in millisec, if there 
// is no ikcp_input/_send calling. you can call ikcp_update in that
// time, instead of call update repeatly.
// Important to reduce unnacessary ikcp_update invoking. use it to 
// schedule ikcp_update (eg. implementing an epoll-like mechanism, 
// or optimize ikcp_update when handling massive kcp connections)
IUINT32 ikcp_check(const ikcpcb *kcp, IUINT32 current);

// when you received a low level packet (eg. UDP packet), call it
int ikcp_input(ikcpcb *kcp, const char *data, long size);

// flush pending data
void ikcp_flush(ikcpcb *kcp);

// check the size of next message in the recv queue
int ikcp_peeksize(const ikcpcb *kcp);

// change MTU size, default is 1400
int ikcp_setmtu(ikcpcb *kcp, int mtu);

// set maximum window size: sndwnd=32, rcvwnd=32 by default
int ikcp_wndsize(ikcpcb *kcp, int sndwnd, int rcvwnd);

// get how many packet is waiting to be sent
int ikcp_waitsnd(const ikcpcb *kcp);

// fastest: ikcp_nodelay(kcp, 1, 20, 2, 1)
// nodelay: 0:disable(default), 1:enable
// interval: internal update timer interval in millisec, default is 100ms 
// resend: 0:disable fast resend(default), 1:enable fast resend
// nc: 0:normal congestion control(default), 1:disable congestion control
int ikcp_nodelay(ikcpcb *kcp, int nodelay, int interval, int resend, int nc);

int ikcp_rcvbuf_count(const ikcpcb *kcp);
int ikcp_sndbuf_count(const ikcpcb *kcp);

void ikcp_log(ikcpcb *kcp, int mask, const char *fmt, ...);

// setup allocator
void ikcp_allocator(void* (*new_malloc)(size_t), void (*new_free)(void*));

// read conv
IUINT32 ikcp_getconv(const void *ptr);


#ifdef __cplusplus
}
#endif

#endif


