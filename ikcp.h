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
// BYTE ORDER & ALIGNMENT
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
            (defined(__MIPS__) && defined(__MIPSEB__)) || \
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

#ifndef IWORDS_MUST_ALIGN
	#if defined(__i386__) || defined(__i386) || defined(_i386_)
		#define IWORDS_MUST_ALIGN 0
	#elif defined(_M_IX86) || defined(_X86_) || defined(__x86_64__)
		#define IWORDS_MUST_ALIGN 0
	#elif defined(__amd64) || defined(__amd64__)
		#define IWORDS_MUST_ALIGN 0
	#else
		#define IWORDS_MUST_ALIGN 1
	#endif
#endif


//=====================================================================
// SEGMENT
//=====================================================================
struct IKCPSEG
{
    // 队列节点，用于将KCP segment插入到发送队列、接收队列、发送缓冲区和接收缓冲区等链表中
	struct IQUEUEHEAD node;
    // 会话ID，用于标识属于同一个连接的数据包。发送方和接收方的conv值必须相同
	IUINT32 conv;
    // 数据包的类型，根据不同的cmd值，KCP协议会采取不同的处理策略
 	// IKCP_CMD_PUSH	cmd: push data
	// IKCP_CMD_ACK		cmd: ack
	// IKCP_CMD_WASK	cmd: window probe (ask)
	// IKCP_CMD_WINS	cmd: window size (tell)
	IUINT32 cmd;
    // 分段序号，当数据包被分成多个片段进行传输时，每个片段都会被赋予一个frg值
    // 从0开始递增，表示数据的分段顺序，逆序
    // 接收方会根据frg值对数据包进行重组
	IUINT32 frg;
    // 发送或者接收方的窗口大小，窗口大小用于实现流量控制和拥塞控制
	IUINT32 wnd;
    // 数据包的发送时间戳，时间戳用于计算RTT和超时重传
	IUINT32 ts;
    // 数据包的序号，用于保证有序性和可靠性
	IUINT32 sn;
    // una字段表示发送方期望接收到的下一个确认序号
    // 接收方会根据una值来确定哪些数据包已经被确认，从而更新发送缓冲区
	IUINT32 una;
    // 数据包的实际数据大小，字节
	IUINT32 len;
    // 数据包的重传时间戳，当当前时间超过重传时间戳时，发送方会触发重传
	IUINT32 resendts;
    // 数据包的重传超时时间，rto值会根据网络状况动态调整
	IUINT32 rto;
    // 快速重传计数器
    // 当接收到的最大确认序号大于待发送最小数据包序号时，fastack值会递增
    // 当fastack值达到一定阈值时，触发快速重传
	IUINT32 fastack;
    // 数据包的发送次数，当xmit值超过一定阈值时，KCP会认为连接已断开
	IUINT32 xmit;
    // 数据包中实际的数据内容，这是一个长度可变的数组，实际长度由len字段指定
    // 通过如下函数分配内存
    // static IKCPSEG* ikcp_segment_new(ikcpcb *kcp, int size)
    // {
    // 	return (IKCPSEG*)ikcp_malloc(sizeof(IKCPSEG) + size);
    // }
	char data[1];
};

//---------------------------------------------------------------------
// IKCPCB
//---------------------------------------------------------------------
struct IKCPCB
{
    // 会话id, mtu最大传输单元
    // mss最大报文段长度,segment数据最大长度, 通常mss=mtu-overhead
    // kcp当前状态
	IUINT32 conv, mtu, mss, state;
    // snd_una 作为发送方已发送但未确认的最小序号
    // snd_nxt 作为发送方下一个要发送的序号
    // rcv_nxt 作为接收方期望收到的下一个序号
	IUINT32 snd_una, snd_nxt, rcv_nxt;
    // ts_recent 最近一次收到数据包的时间戳
    // ts_lastack 最近一次发送确认包的时间戳
    // ssthresh 慢启动阈值, 用于拥塞控制算法
	IUINT32 ts_recent, ts_lastack, ssthresh;
    // RTT值, 加权平均RTT值, 超时重传值, 超时重传值下限
	IINT32 rx_rttval, rx_srtt, rx_rto, rx_minrto;
    // 发送窗口大小, 接收窗口大小, 远程窗口大小, 拥塞窗口大小(控制向外发送的字节数), 探测标志
	IUINT32 snd_wnd, rcv_wnd, rmt_wnd, cwnd, probe;
    // 当前时间戳, 内部更新时间间隔, 下一次刷新的时间戳, 总数据包发送次数
	IUINT32 current, interval, ts_flush, xmit;
    // 接收缓冲区中的数据包数量, 发送缓冲区中的数据包数量
	IUINT32 nrcv_buf, nsnd_buf;
    // 接收队列中的数据包数量, 发送队列中的数据包数量
	IUINT32 nrcv_que, nsnd_que;
    // nodelay标志, KCP状态是否已更新
	IUINT32 nodelay, updated;
    // 下次窗口探测的时间戳, 探测等待时间
	IUINT32 ts_probe, probe_wait;
    // dead_link 连接断开的阈值, 当数据包的发送次数超过此阈值时, KCP会认为连接已断开
    // 拥塞窗口的增量
	IUINT32 dead_link, incr;
    // 发送队列
	struct IQUEUEHEAD snd_queue;
	// 接收队列
    struct IQUEUEHEAD rcv_queue;
	// 发送缓冲区
    struct IQUEUEHEAD snd_buf;
	// 接收缓冲区
    struct IQUEUEHEAD rcv_buf;
	// 用于存储待发送的ack信息
    // 数组中的元素成对存储, 每对元素分别表示一个序号(sn)和对应的时间戳(ts)
    // sn = [2*i+0], ts = [2*i+1] 
    // 在ikcp_flush函数中, KCP会根据acklist中的信息发送确认包(ACK)
    IUINT32 *acklist;
    // acklist中的ack数量, acklist的长度等于 ackcount*2
	IUINT32 ackcount;
    // acklist的内存容量
	IUINT32 ackblock;
    // 指向用户自定义数据的指针, 可在callback中传递特定上下文信息
	void *user;
    // 储存kcp协议的缓冲区, 用于构造和发送数据包
	char *buffer;
    // 快速重传的阈值
    // 当一个数据包的ack被跳过的次数达到fastresend值时, 会触发快速重传
	int fastresend;
    // 快速重传上限, 当数据发送次数超过fastlimit将不触发快速重传
    // 设置为0代表不设置上限
	int fastlimit;
    // 是否禁用拥塞窗口的标志, 是否启用流模式的标志
	int nocwnd, stream;
    // 日志记录的掩码
    // 根据logmask的值, 可以选择性地记录不同类型的日志信息, 如输出日志, 输入日志, 发送日志等
	int logmask;
    // 指向输出回调函数的指针
    // kcp发送数据包时会调用的回调函数, 用户需要在此函数中实现实际发送逻辑如UDP
	int (*output)(const char *buf, int len, struct IKCPCB *kcp, void *user);
    // 指向记录日志回调函数的指针
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
int ikcp_setConfig(ikcpcb *kcp, int nodelay, int interval, int resend, int nc);


void ikcp_log(ikcpcb *kcp, int mask, const char *fmt, ...);

// setup allocator
void ikcp_allocator(void* (*new_malloc)(size_t), void (*new_free)(void*));

// read conv
IUINT32 ikcp_getconv(const void *ptr);


#ifdef __cplusplus
}
#endif

#endif


