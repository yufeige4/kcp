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
#include "ikcp.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>



//=====================================================================
// KCP BASIC
//=====================================================================
const IUINT32 IKCP_RTO_NDL = 30;		// no delay min rto
const IUINT32 IKCP_RTO_MIN = 100;		// normal min rto
const IUINT32 IKCP_RTO_DEF = 200;
const IUINT32 IKCP_RTO_MAX = 60000;
const IUINT32 IKCP_CMD_PUSH = 81;		// cmd: push data
const IUINT32 IKCP_CMD_ACK  = 82;		// cmd: ack
const IUINT32 IKCP_CMD_WASK = 83;		// cmd: window probe (ask)
const IUINT32 IKCP_CMD_WINS = 84;		// cmd: window size (tell)
const IUINT32 IKCP_ASK_SEND = 1;		// need to send IKCP_CMD_WASK
const IUINT32 IKCP_ASK_TELL = 2;		// need to send IKCP_CMD_WINS
const IUINT32 IKCP_WND_SND = 32;
const IUINT32 IKCP_WND_RCV = 128;       // must >= max fragment size
const IUINT32 IKCP_MTU_DEF = 1400;
const IUINT32 IKCP_ACK_FAST	= 3;
const IUINT32 IKCP_INTERVAL	= 100;
// 头大小
const IUINT32 IKCP_OVERHEAD = 24;
const IUINT32 IKCP_DEADLINK = 20;
const IUINT32 IKCP_THRESH_INIT = 2;
const IUINT32 IKCP_THRESH_MIN = 2;
const IUINT32 IKCP_PROBE_INIT = 7000;		// 7 secs to probe window size
const IUINT32 IKCP_PROBE_LIMIT = 120000;	// up to 120 secs to probe window
const IUINT32 IKCP_FASTACK_LIMIT = 5;		// max times to trigger fastack


//---------------------------------------------------------------------
// encode / decode
//---------------------------------------------------------------------

/* encode 8 bits unsigned int */
static inline char *ikcp_encode8u(char *p, unsigned char c)
{
	*(unsigned char*)p++ = c;
	return p;
}

/* decode 8 bits unsigned int */
static inline const char *ikcp_decode8u(const char *p, unsigned char *c)
{
	*c = *(unsigned char*)p++;
	return p;
}

/* encode 16 bits unsigned int (lsb) */
static inline char *ikcp_encode16u(char *p, unsigned short w)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*(unsigned char*)(p + 0) = (w & 255);
	*(unsigned char*)(p + 1) = (w >> 8);
#else
	memcpy(p, &w, 2);
#endif
	p += 2;
	return p;
}

/* decode 16 bits unsigned int (lsb) */
static inline const char *ikcp_decode16u(const char *p, unsigned short *w)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*w = *(const unsigned char*)(p + 1);
	*w = *(const unsigned char*)(p + 0) + (*w << 8);
#else
	memcpy(w, p, 2);
#endif
	p += 2;
	return p;
}

/* encode 32 bits unsigned int (lsb) */
static inline char *ikcp_encode32u(char *p, IUINT32 l)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*(unsigned char*)(p + 0) = (unsigned char)((l >>  0) & 0xff);
	*(unsigned char*)(p + 1) = (unsigned char)((l >>  8) & 0xff);
	*(unsigned char*)(p + 2) = (unsigned char)((l >> 16) & 0xff);
	*(unsigned char*)(p + 3) = (unsigned char)((l >> 24) & 0xff);
#else
	memcpy(p, &l, 4);
#endif
	p += 4;
	return p;
}

/* decode 32 bits unsigned int (lsb) */
static inline const char *ikcp_decode32u(const char *p, IUINT32 *l)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*l = *(const unsigned char*)(p + 3);
	*l = *(const unsigned char*)(p + 2) + (*l << 8);
	*l = *(const unsigned char*)(p + 1) + (*l << 8);
	*l = *(const unsigned char*)(p + 0) + (*l << 8);
#else 
	memcpy(l, p, 4);
#endif
	p += 4;
	return p;
}

static inline IUINT32 _imin_(IUINT32 a, IUINT32 b) {
	return a <= b ? a : b;
}

static inline IUINT32 _imax_(IUINT32 a, IUINT32 b) {
	return a >= b ? a : b;
}

static inline IUINT32 _ibound_(IUINT32 lower, IUINT32 middle, IUINT32 upper) 
{
	return _imin_(_imax_(lower, middle), upper);
}

static inline long _itimediff(IUINT32 later, IUINT32 earlier) 
{
	return ((IINT32)(later - earlier));
}

//---------------------------------------------------------------------
// manage segment
//---------------------------------------------------------------------
typedef struct IKCPSEG IKCPSEG;

static void* (*ikcp_malloc_hook)(size_t) = NULL;
static void (*ikcp_free_hook)(void *) = NULL;

// internal malloc
static void* ikcp_malloc(size_t size) {
	if (ikcp_malloc_hook) 
		return ikcp_malloc_hook(size);
	return malloc(size);
}

// internal free
static void ikcp_free(void *ptr) {
	if (ikcp_free_hook) {
		ikcp_free_hook(ptr);
	}	else {
		free(ptr);
	}
}

// redefine allocator
void ikcp_allocator(void* (*new_malloc)(size_t), void (*new_free)(void*))
{
	ikcp_malloc_hook = new_malloc;
	ikcp_free_hook = new_free;
}

// allocate a new kcp segment
static IKCPSEG* ikcp_segment_new(ikcpcb *kcp, int size)
{
	return (IKCPSEG*)ikcp_malloc(sizeof(IKCPSEG) + size);
}

// delete a segment
static void ikcp_segment_delete(ikcpcb *kcp, IKCPSEG *seg)
{
	ikcp_free(seg);
}

// write log
void ikcp_log(ikcpcb *kcp, int mask, const char *fmt, ...)
{
	char buffer[1024];
	va_list argptr;
	if ((mask & kcp->logmask) == 0 || kcp->writelog == 0) return;
	va_start(argptr, fmt);
	vsprintf(buffer, fmt, argptr);
	va_end(argptr);
	kcp->writelog(buffer, kcp, kcp->user);
}

// check log mask
static int ikcp_canlog(const ikcpcb *kcp, int mask)
{
	if ((mask & kcp->logmask) == 0 || kcp->writelog == NULL) return 0;
	return 1;
}

// output segment
static int ikcp_output(ikcpcb *kcp, const void *data, int size)
{
	assert(kcp);
	assert(kcp->output);
	if (ikcp_canlog(kcp, IKCP_LOG_OUTPUT)) {
		ikcp_log(kcp, IKCP_LOG_OUTPUT, "[RO] %ld bytes", (long)size);
	}
	if (size == 0) return 0;
	return kcp->output((const char*)data, size, kcp, kcp->user);
}

// output queue
void ikcp_qprint(const char *name, const struct IQUEUEHEAD *head)
{
#if 0
	const struct IQUEUEHEAD *p;
	printf("<%s>: [", name);
	for (p = head->next; p != head; p = p->next) {
		const IKCPSEG *seg = iqueue_entry(p, const IKCPSEG, node);
		printf("(%lu %d)", (unsigned long)seg->sn, (int)(seg->ts % 10000));
		if (p->next != head) printf(",");
	}
	printf("]\n");
#endif
}


//---------------------------------------------------------------------
// create a new kcpcb
//---------------------------------------------------------------------
ikcpcb* ikcp_create(IUINT32 conv, void *user)
{
	ikcpcb *kcp = (ikcpcb*)ikcp_malloc(sizeof(struct IKCPCB));
	if (kcp == NULL) return NULL;
	kcp->conv = conv;
	kcp->user = user;
	kcp->snd_una = 0;
	kcp->snd_nxt = 0;
	kcp->rcv_nxt = 0;
	kcp->ts_recent = 0;
	kcp->ts_lastack = 0;
	kcp->ts_probe = 0;
	kcp->probe_wait = 0;
	kcp->snd_wnd = IKCP_WND_SND;
	kcp->rcv_wnd = IKCP_WND_RCV;
	kcp->rmt_wnd = IKCP_WND_RCV;
	kcp->cwnd = 0;
	kcp->incr = 0;
	kcp->probe = 0;
	kcp->mtu = IKCP_MTU_DEF;
	kcp->mss = kcp->mtu - IKCP_OVERHEAD;
	kcp->stream = 0;

	kcp->buffer = (char*)ikcp_malloc((kcp->mtu + IKCP_OVERHEAD) * 3);
	if (kcp->buffer == NULL) {
		ikcp_free(kcp);
		return NULL;
	}

	iqueue_init(&kcp->snd_queue);
	iqueue_init(&kcp->rcv_queue);
	iqueue_init(&kcp->snd_buf);
	iqueue_init(&kcp->rcv_buf);
	kcp->nrcv_buf = 0;
	kcp->nsnd_buf = 0;
	kcp->nrcv_que = 0;
	kcp->nsnd_que = 0;
	kcp->state = 0;
	kcp->acklist = NULL;
	kcp->ackblock = 0;
	kcp->ackcount = 0;
	kcp->rx_srtt = 0;
	kcp->rx_rttval = 0;
	kcp->rx_rto = IKCP_RTO_DEF;
	kcp->rx_minrto = IKCP_RTO_MIN;
	kcp->current = 0;
	kcp->interval = IKCP_INTERVAL;
	kcp->ts_flush = IKCP_INTERVAL;
	kcp->nodelay = 0;
	kcp->updated = 0;
	kcp->logmask = 0;
	kcp->ssthresh = IKCP_THRESH_INIT;
	kcp->fastresend = 0;
	kcp->fastlimit = IKCP_FASTACK_LIMIT;
	kcp->nocwnd = 0;
	kcp->xmit = 0;
	kcp->dead_link = IKCP_DEADLINK;
	kcp->output = NULL;
	kcp->writelog = NULL;

	return kcp;
}


//---------------------------------------------------------------------
// release a new kcpcb
//---------------------------------------------------------------------
void ikcp_release(ikcpcb *kcp)
{
	assert(kcp);
	if (kcp) {
		IKCPSEG *seg;
		while (!iqueue_is_empty(&kcp->snd_buf)) {
			seg = iqueue_entry(kcp->snd_buf.next, IKCPSEG, node);
			iqueue_del(&seg->node);
			ikcp_segment_delete(kcp, seg);
		}
		while (!iqueue_is_empty(&kcp->rcv_buf)) {
			seg = iqueue_entry(kcp->rcv_buf.next, IKCPSEG, node);
			iqueue_del(&seg->node);
			ikcp_segment_delete(kcp, seg);
		}
		while (!iqueue_is_empty(&kcp->snd_queue)) {
			seg = iqueue_entry(kcp->snd_queue.next, IKCPSEG, node);
			iqueue_del(&seg->node);
			ikcp_segment_delete(kcp, seg);
		}
		while (!iqueue_is_empty(&kcp->rcv_queue)) {
			seg = iqueue_entry(kcp->rcv_queue.next, IKCPSEG, node);
			iqueue_del(&seg->node);
			ikcp_segment_delete(kcp, seg);
		}
		if (kcp->buffer) {
			ikcp_free(kcp->buffer);
		}
		if (kcp->acklist) {
			ikcp_free(kcp->acklist);
		}

		kcp->nrcv_buf = 0;
		kcp->nsnd_buf = 0;
		kcp->nrcv_que = 0;
		kcp->nsnd_que = 0;
		kcp->ackcount = 0;
		kcp->buffer = NULL;
		kcp->acklist = NULL;
		ikcp_free(kcp);
	}
}


//---------------------------------------------------------------------
// set output callback, which will be invoked by kcp
//---------------------------------------------------------------------
void ikcp_setoutput(ikcpcb *kcp, int (*output)(const char *buf, int len,
	ikcpcb *kcp, void *user))
{
	kcp->output = output;
}


//---------------------------------------------------------------------
// user/upper level recv: returns size, returns below zero for EAGAIN
//---------------------------------------------------------------------
int ikcp_recv(ikcpcb *kcp, char *buffer, int len)
{
	struct IQUEUEHEAD *p;
	// 判断是否只是peek一下
	int ispeek = (len < 0)? 1 : 0;
	int peeksize;
	int recover = 0;
	IKCPSEG *seg;
	assert(kcp);

	if (iqueue_is_empty(&kcp->rcv_queue))
		return -1;

	if (len < 0) len = -len;
	// 总共recv queue中的总数据量(字节)
	peeksize = ikcp_peeksize(kcp);

	if (peeksize < 0) 
		return -2;
	// 大于peeksize
	if (peeksize > len) 
		return -3;
	
	//	接收窗口被阻塞了 
	if (kcp->nrcv_que >= kcp->rcv_wnd)
		recover = 1;

	// merge fragment
	for (len = 0, p = kcp->rcv_queue.next; p != &kcp->rcv_queue; ) {
		int fragment;
		seg = iqueue_entry(p, IKCPSEG, node);
		p = p->next;
		// 往upper level buffer里写fragment
		if (buffer) {
			memcpy(buffer, seg->data, seg->len);
			buffer += seg->len;
		}

		len += seg->len;
		fragment = seg->frg;

		if (ikcp_canlog(kcp, IKCP_LOG_RECV)) {
			ikcp_log(kcp, IKCP_LOG_RECV, "recv sn=%lu", (unsigned long)seg->sn);
		}
		
		// 如果是不是peek
		if (ispeek == 0) {
			// 把节点从recv queue中移除
			iqueue_del(&seg->node);
			ikcp_segment_delete(kcp, seg);
			kcp->nrcv_que--;
		}
		// 最后一个fragment
		if (fragment == 0) 
			break;
	}

	assert(len == peeksize);

	// 将receiveBuffer里中连续且符合预期的数据移到receiveQueue中
	// move available data from rcv_buf -> rcv_queue
	while (!iqueue_is_empty(&kcp->rcv_buf)) {
		// recv buffer中剩余内容
		seg = iqueue_entry(kcp->rcv_buf.next, IKCPSEG, node);
		// 满足窗口大小且是预期接收的内容
		if (seg->sn == kcp->rcv_nxt && kcp->nrcv_que < kcp->rcv_wnd) {
			iqueue_del(&seg->node);
			kcp->nrcv_buf--;
			iqueue_add_tail(&seg->node, &kcp->rcv_queue);
			kcp->nrcv_que++;
			kcp->rcv_nxt++;
		}
		else {
			break;
		}
	}
	// 快速恢复
	// fast recover
	if (kcp->nrcv_que < kcp->rcv_wnd && recover) {
		// ready to send back IKCP_CMD_WINS in ikcp_flush
		// tell remote my window size
		kcp->probe |= IKCP_ASK_TELL;
	}

	return len;
}


//---------------------------------------------------------------------
// peek data size
//---------------------------------------------------------------------
int ikcp_peeksize(const ikcpcb *kcp)
{
	struct IQUEUEHEAD *p;
	IKCPSEG *seg;
	int length = 0;

	assert(kcp);

	if (iqueue_is_empty(&kcp->rcv_queue)) return -1;

	seg = iqueue_entry(kcp->rcv_queue.next, IKCPSEG, node);
	if (seg->frg == 0) return seg->len;

	if (kcp->nrcv_que < seg->frg + 1) return -1;

	for (p = kcp->rcv_queue.next; p != &kcp->rcv_queue; p = p->next) {
		seg = iqueue_entry(p, IKCPSEG, node);
		length += seg->len;
		if (seg->frg == 0) break;
	}

	return length;
}


//---------------------------------------------------------------------
// user/upper level send, returns below zero for error
//---------------------------------------------------------------------
int ikcp_send(ikcpcb *kcp, const char *buffer, int len)
{
	IKCPSEG *seg;
	int count, i;
	int sent = 0;

	assert(kcp->mss > 0);
	if (len < 0) return -1;

#pragma region 流模式
	// append to previous segment in streaming mode (if possible)
	if (kcp->stream != 0) {
		if (!iqueue_is_empty(&kcp->snd_queue)) {
			IKCPSEG *old = iqueue_entry(kcp->snd_queue.prev, IKCPSEG, node);
			if (old->len < kcp->mss) {
				int capacity = kcp->mss - old->len;
				int extend = (len < capacity)? len : capacity;
				seg = ikcp_segment_new(kcp, old->len + extend);
				assert(seg);
				if (seg == NULL) {
					return -2;
				}
				iqueue_add_tail(&seg->node, &kcp->snd_queue);
				memcpy(seg->data, old->data, old->len);
				if (buffer) {
					memcpy(seg->data + old->len, buffer, extend);
					buffer += extend;
				}
				seg->len = old->len + extend;
				seg->frg = 0;
				len -= extend;
				iqueue_del_init(&old->node);
				ikcp_segment_delete(kcp, old);
				sent = extend;
			}
		}
		if (len <= 0) {
			return sent;
		}
	}
#pragma endregion 流模式
	// 实际数据长度小于一个fragment的mss
	if (len <= (int)kcp->mss){
		count = 1;
	}
	else{
		// len/mss向上取整
		count = (len + kcp->mss - 1) / kcp->mss;
	}
	// 分片数量如果超过接收窗口最大值
	// 不开启流模式则抛异常
	if (count >= (int)IKCP_WND_RCV) {
		if (kcp->stream != 0 && sent > 0) 
			return sent;
		return -2;
	}
	// 防止配置错误
	if (count == 0) count = 1;

	// 消息分片
	// fragment
	for (i = 0; i < count; i++) {
		// 满不满一个fragment的最大数据长度
		int size = len > (int)kcp->mss ? (int)kcp->mss : len;
		seg = ikcp_segment_new(kcp, size);
		assert(seg);
		if (seg == NULL) {
			return -2;
		}
		// 数据写入segment
		if (buffer && len > 0) {
			memcpy(seg->data, buffer, size);
		}
		seg->len = size;
		// 非流模式segment号从count-1到0
		seg->frg = (kcp->stream == 0)? (count - i - 1) : 0;
		// 加入send queue中
		iqueue_init(&seg->node);
		iqueue_add_tail(&seg->node, &kcp->snd_queue);
		kcp->nsnd_que++;
		// 指针后移
		if (buffer) {
			buffer += size;
		}
		// 剩余数据量
		len -= size;
		// 总加入queue中的数据量
		sent += size;
	}

	return sent;
}


//---------------------------------------------------------------------
// parse ack
//---------------------------------------------------------------------
static void ikcp_update_ack(ikcpcb *kcp, IINT32 rtt)
{
	IINT32 rto = 0;
	// 初始化
	if (kcp->rx_srtt == 0) {
		kcp->rx_srtt = rtt;
		kcp->rx_rttval = rtt / 2;
	}	
	else {

		long delta = rtt - kcp->rx_srtt;
		// 取delta绝对值
		if (delta < 0){
			delta = -delta;
		}
		kcp->rx_rttval = (3 * kcp->rx_rttval + delta) / 4;
		kcp->rx_srtt = (7 * kcp->rx_srtt + rtt) / 8;
		if (kcp->rx_srtt < 1) kcp->rx_srtt = 1;
	}
	rto = kcp->rx_srtt + _imax_(kcp->interval, 4 * kcp->rx_rttval);
	kcp->rx_rto = _ibound_(kcp->rx_minrto, rto, IKCP_RTO_MAX);
}

static void ikcp_shrink_buf(ikcpcb *kcp)
{
	struct IQUEUEHEAD *p = kcp->snd_buf.next;
	if (p != &kcp->snd_buf) {
		IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		kcp->snd_una = seg->sn;
	}	else {
		kcp->snd_una = kcp->snd_nxt;
	}
}

static void ikcp_parse_ack(ikcpcb *kcp, IUINT32 sn)
{
	struct IQUEUEHEAD *p, *next;
	// 无效包
	if (_itimediff(sn, kcp->snd_una) < 0 || _itimediff(sn, kcp->snd_nxt) >= 0)
		return;
	// 将该seq号的包从send buffer中移除
	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = next) {
		IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		next = p->next;
		if (sn == seg->sn) {
			iqueue_del(p);
			ikcp_segment_delete(kcp, seg);
			kcp->nsnd_buf--;
			break;
		}
		if (_itimediff(sn, seg->sn) < 0) {
			break;
		}
	}
}

// 根据该una将已经被ack的包移出send buffer
static void ikcp_parse_una(ikcpcb *kcp, IUINT32 una)
{
	struct IQUEUEHEAD *p, *next;
	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = next) {
		IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		next = p->next;
		if (_itimediff(una, seg->sn) > 0) {
			iqueue_del(p);
			ikcp_segment_delete(kcp, seg);
			kcp->nsnd_buf--;
		}	
		else {
			break;
		}
	}
}

static void ikcp_parse_fastack(ikcpcb *kcp, IUINT32 sn, IUINT32 ts)
{
	struct IQUEUEHEAD *p, *next;
	// 无效包: 已经收到过或还没发送过的包
	if (_itimediff(sn, kcp->snd_una) < 0 || _itimediff(sn, kcp->snd_nxt) >= 0)
		return;

	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = next) {
		IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		next = p->next;

		if (_itimediff(sn, seg->sn) < 0) {
			break;
		}
		else if (sn != seg->sn) {	// 当接收到的最大确认序号大于待发送最小数据包序号时，fastack值会递增
		#ifndef IKCP_FASTACK_CONSERVE
			seg->fastack++;
		#else
			if (_itimediff(ts, seg->ts) >= 0)
				seg->fastack++;
		#endif
		}
	}
}


//---------------------------------------------------------------------
// ack append
//---------------------------------------------------------------------
static void ikcp_ack_push(ikcpcb *kcp, IUINT32 sn, IUINT32 ts)
{
	IUINT32 newsize = kcp->ackcount + 1;
	IUINT32 *ptr;

	// acklist扩容
	if (newsize > kcp->ackblock) {
		IUINT32 *acklist;
		IUINT32 newblock;

		for (newblock = 8; newblock < newsize; newblock <<= 1);
		acklist = (IUINT32*)ikcp_malloc(newblock * sizeof(IUINT32) * 2);

		if (acklist == NULL) {
			assert(acklist != NULL);
			abort();
		}

		if (kcp->acklist != NULL) {
			IUINT32 x;
			for (x = 0; x < kcp->ackcount; x++) {
				acklist[x * 2 + 0] = kcp->acklist[x * 2 + 0];
				acklist[x * 2 + 1] = kcp->acklist[x * 2 + 1];
			}
			ikcp_free(kcp->acklist);
		}

		kcp->acklist = acklist;
		kcp->ackblock = newblock;
	}


	ptr = &kcp->acklist[kcp->ackcount * 2];
	ptr[0] = sn;
	ptr[1] = ts;
	kcp->ackcount++;
}

static void ikcp_ack_get(const ikcpcb *kcp, int p, IUINT32 *sn, IUINT32 *ts)
{
	// acklist
	if (sn){
		sn[0] = kcp->acklist[p * 2 + 0];
	}
	if (ts) {
		ts[0] = kcp->acklist[p * 2 + 1];
	}

}


//---------------------------------------------------------------------
// parse data
//---------------------------------------------------------------------
void ikcp_parse_data(ikcpcb *kcp, IKCPSEG *newseg)
{
	struct IQUEUEHEAD *p, *prev;
	IUINT32 sn = newseg->sn;
	int repeat = 0;
	// 窗口外的包或者过期包
	if (_itimediff(sn, kcp->rcv_nxt + kcp->rcv_wnd) >= 0 ||
		_itimediff(sn, kcp->rcv_nxt) < 0) {
		ikcp_segment_delete(kcp, newseg);
		return;
	}
	// 从后往前遍历并判断是否是重复包
	for (p = kcp->rcv_buf.prev; p != &kcp->rcv_buf; p = prev) {
		IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		prev = p->prev;
		if (seg->sn == sn) {
			repeat = 1;
			break;
		}
		if (_itimediff(sn, seg->sn) > 0) {
			break;
		}
	}
	// 插入接收队列
	if (repeat == 0) {
		iqueue_init(&newseg->node);
		iqueue_add(&newseg->node, p);
		kcp->nrcv_buf++;
	}	else {
		ikcp_segment_delete(kcp, newseg);
	}

#if 0
	ikcp_qprint("rcvbuf", &kcp->rcv_buf);
	printf("rcv_nxt=%lu\n", kcp->rcv_nxt);
#endif
	// 将receiveBuffer里中连续且符合预期的数据移到receiveQueue中
	// move available data from rcv_buf -> rcv_queue
	while (! iqueue_is_empty(&kcp->rcv_buf)) {
		IKCPSEG *seg = iqueue_entry(kcp->rcv_buf.next, IKCPSEG, node);
		if (seg->sn == kcp->rcv_nxt && kcp->nrcv_que < kcp->rcv_wnd) {
			iqueue_del(&seg->node);
			kcp->nrcv_buf--;
			iqueue_add_tail(&seg->node, &kcp->rcv_queue);
			kcp->nrcv_que++;
			kcp->rcv_nxt++;
		}	else {
			break;
		}
	}

#if 0
	ikcp_qprint("queue", &kcp->rcv_queue);
	printf("rcv_nxt=%lu\n", kcp->rcv_nxt);
#endif

#if 1
//	printf("snd(buf=%d, queue=%d)\n", kcp->nsnd_buf, kcp->nsnd_que);
//	printf("rcv(buf=%d, queue=%d)\n", kcp->nrcv_buf, kcp->nrcv_que);
#endif
}


//---------------------------------------------------------------------
// input data
//---------------------------------------------------------------------
int ikcp_input(ikcpcb *kcp, const char *data, long size)
{
	IUINT32 prev_una = kcp->snd_una;
	IUINT32 maxack = 0, latest_ts = 0;
	// 是否处理过有效ack的标记
	int flag = 0;

	if (ikcp_canlog(kcp, IKCP_LOG_INPUT)) {
		ikcp_log(kcp, IKCP_LOG_INPUT, "[RI] %d bytes", (int)size);
	}

	if (data == NULL || (int)size < (int)IKCP_OVERHEAD) return -1;
	// 处理recv buffer中的数据
	while (1) {
		IUINT32 ts, sn, len, una, conv;
		IUINT16 wnd;
		IUINT8 cmd, frg;
		IKCPSEG *seg;

		// 解析overhead信息
		if (size < (int)IKCP_OVERHEAD) break;

		// 4 bytes
		data = ikcp_decode32u(data, &conv);
		if (conv != kcp->conv) return -1;
		// 1 byte
		data = ikcp_decode8u(data, &cmd);
		// 1 byte
		data = ikcp_decode8u(data, &frg);
		// 2 bytes
		data = ikcp_decode16u(data, &wnd);
		// 4 bytes
		data = ikcp_decode32u(data, &ts);
		// 4 bytes
		data = ikcp_decode32u(data, &sn);
		// 4 bytes
		data = ikcp_decode32u(data, &una);
		// 4 bytes
		data = ikcp_decode32u(data, &len);

		size -= IKCP_OVERHEAD;

		// 实际数据长度不符合
		if ((long)size < (long)len || (int)len < 0) return -2;
		// 处理策略非法
		if (cmd != IKCP_CMD_PUSH && cmd != IKCP_CMD_ACK &&
			cmd != IKCP_CMD_WASK && cmd != IKCP_CMD_WINS) 
			return -3;

		kcp->rmt_wnd = wnd;
		// 根据una信息删除发送buffer中不需要发送的
		ikcp_parse_una(kcp, una);
		// 更新kcp的待ack序号
		ikcp_shrink_buf(kcp);

		if (cmd == IKCP_CMD_ACK) {	// ack包
			// rtt
			if (_itimediff(kcp->current, ts) >= 0) { 
				// 更新rtt,rto
				ikcp_update_ack(kcp, _itimediff(kcp->current, ts));
			}
			// 将这个序号的ack包从发送buffer中移除
			ikcp_parse_ack(kcp, sn);		
			ikcp_shrink_buf(kcp);
			// 第一个ack包
			if (flag == 0) {
				flag = 1;
				maxack = sn;
				latest_ts = ts;
			}	
			else {
				// 计算maxack
				// 和最近的接收包的时间
				if (_itimediff(sn, maxack) > 0) 
				{
				#ifndef IKCP_FASTACK_CONSERVE
					maxack = sn;
					latest_ts = ts;
				#else
					if (_itimediff(ts, latest_ts) > 0) {
						maxack = sn;
						latest_ts = ts;
					}
				#endif
				}
			}
			// log
			if (ikcp_canlog(kcp, IKCP_LOG_IN_ACK)) {
				ikcp_log(kcp, IKCP_LOG_IN_ACK, 
					"input ack: sn=%lu rtt=%ld rto=%ld", (unsigned long)sn, 
					(long)_itimediff(kcp->current, ts),
					(long)kcp->rx_rto);
			}
		}
		else if (cmd == IKCP_CMD_PUSH) {	// 数据传输
			if (ikcp_canlog(kcp, IKCP_LOG_IN_DATA)) {
				ikcp_log(kcp, IKCP_LOG_IN_DATA, 
					"input psh: sn=%lu ts=%lu", (unsigned long)sn, (unsigned long)ts);
			}
			// 在接收窗口范围内, rcv_wnd+rcv_nxt >= sn
			if (_itimediff(sn, kcp->rcv_nxt + kcp->rcv_wnd) < 0) {
				// 写入待发送ack信息列表中
				ikcp_ack_push(kcp, sn, ts);
				// seq合法
				if (_itimediff(sn, kcp->rcv_nxt) >= 0) {
					seg = ikcp_segment_new(kcp, len);
					seg->conv = conv;
					seg->cmd = cmd;
					seg->frg = frg;
					seg->wnd = wnd;
					seg->ts = ts;
					seg->sn = sn;
					seg->una = una;
					seg->len = len;

					if (len > 0) {
						memcpy(seg->data, data, len);
					}

					// 写入recv buffer中
					ikcp_parse_data(kcp, seg);
				}
			}
		}
		else if (cmd == IKCP_CMD_WASK) {
			// ready to send back IKCP_CMD_WINS in ikcp_flush
			// tell remote my window size
			// 接收到窗口探测请求, 将标志位修改为需要发送窗口探测结果
			kcp->probe |= IKCP_ASK_TELL;
			if (ikcp_canlog(kcp, IKCP_LOG_IN_PROBE)) {
				ikcp_log(kcp, IKCP_LOG_IN_PROBE, "input probe");
			}
		}
		else if (cmd == IKCP_CMD_WINS) {
			// 对方告知
			// do nothing
			if (ikcp_canlog(kcp, IKCP_LOG_IN_WINS)) {
				ikcp_log(kcp, IKCP_LOG_IN_WINS,
					"input wins: %lu", (unsigned long)(wnd));
			}
		}
		else {
			// 无效包
			return -3;
		}

		data += len;
		size -= len;
	}

	// 收到过ack包
	if (flag != 0) {
		// 快速重传计数
		ikcp_parse_fastack(kcp, maxack, latest_ts);
	}
	
	// 收到了未确认过的数据包
	if (_itimediff(kcp->snd_una, prev_una) > 0) {
		if (kcp->cwnd < kcp->rmt_wnd) {
			// 本地窗口大小小于远程窗口
			IUINT32 mss = kcp->mss;
			if (kcp->cwnd < kcp->ssthresh) {	// 慢启动
				kcp->cwnd++;
				kcp->incr += mss;
			}
			else {
				// incr最小值为一个单元的数据大小
				if (kcp->incr < mss) {
					kcp->incr = mss;
				}
				// 对数级增长
				kcp->incr += (mss * mss) / kcp->incr + (mss / 16);
				if ((kcp->cwnd + 1) * mss <= kcp->incr) {
				#if 1
					kcp->cwnd = (kcp->incr + mss - 1) / ((mss > 0)? mss : 1);
				#else
					kcp->cwnd++;
				#endif
				}
			}
			// 不超过远程窗口大小
			if (kcp->cwnd > kcp->rmt_wnd) {
				kcp->cwnd = kcp->rmt_wnd;
				kcp->incr = kcp->rmt_wnd * mss;
			}
		}
	}

	return 0;
}


//---------------------------------------------------------------------
// ikcp_encode_seg
//---------------------------------------------------------------------
static char *ikcp_encode_seg(char *ptr, const IKCPSEG *seg)
{
	ptr = ikcp_encode32u(ptr, seg->conv);
	ptr = ikcp_encode8u(ptr, (IUINT8)seg->cmd);
	ptr = ikcp_encode8u(ptr, (IUINT8)seg->frg);
	ptr = ikcp_encode16u(ptr, (IUINT16)seg->wnd);
	ptr = ikcp_encode32u(ptr, seg->ts);
	ptr = ikcp_encode32u(ptr, seg->sn);
	ptr = ikcp_encode32u(ptr, seg->una);
	ptr = ikcp_encode32u(ptr, seg->len);
	return ptr;
}

static int ikcp_wnd_unused(const ikcpcb *kcp)
{
	if (kcp->nrcv_que < kcp->rcv_wnd) {
		return kcp->rcv_wnd - kcp->nrcv_que;
	}
	return 0;
}


//---------------------------------------------------------------------
// ikcp_flush
//---------------------------------------------------------------------
// 发送数据给传输层
void ikcp_flush(ikcpcb *kcp)
{
	// 当前时间
	IUINT32 current = kcp->current;
	// kcp对应的缓冲区
	char *buffer = kcp->buffer;
	// 当前地址指针
	char *ptr = buffer;

	int count, size, i;
	IUINT32 resent, cwnd;
	IUINT32 rtomin;
	struct IQUEUEHEAD *p;
	int change = 0;
	int lost = 0;
	IKCPSEG seg;

	// 'ikcp_update' haven't been called. 
	if (kcp->updated == 0) return;

	seg.conv = kcp->conv;
	seg.cmd = IKCP_CMD_ACK;
	seg.frg = 0;
	seg.wnd = ikcp_wnd_unused(kcp);
	seg.una = kcp->rcv_nxt;
	seg.len = 0;
	seg.sn = 0;
	seg.ts = 0;

	// flush acknowledges
	// 处理ack相关内容
	#pragma region ack
	count = kcp->ackcount;
	for (i = 0; i < count; i++) {
		size = (int)(ptr - buffer);
		// 检测是否达到MTU, 如果达到，直接发
		if (size + (int)IKCP_OVERHEAD > (int)kcp->mtu) {
			ikcp_output(kcp, buffer, size);
			ptr = buffer;
		}
		// 将信息从acklist中取出
		ikcp_ack_get(kcp, i, &seg.sn, &seg.ts);
		// 写入数据，函数内部移动ptr指针位置
		ptr = ikcp_encode_seg(ptr, &seg);
	}
	// 将待发送ack列表中的数据清空
	kcp->ackcount = 0;

	#pragma endregion ack
	#pragma region window probe
	// 当远程窗口大小为0时进行窗口探测
	// probe window size (if remote window size equals zero)
	if (kcp->rmt_wnd == 0) {
		if (kcp->probe_wait == 0) {
			// 窗口探测参数未初始化
			// 第一次探测时间为默认值
			kcp->probe_wait = IKCP_PROBE_INIT;
			kcp->ts_probe = kcp->current + kcp->probe_wait;
		}	
		else {
			// 该窗口探测了
			if (_itimediff(kcp->current, kcp->ts_probe) >= 0) {
				// 小于默认探测间隔则恢复成默认间隔
				if (kcp->probe_wait < IKCP_PROBE_INIT) 
					kcp->probe_wait = IKCP_PROBE_INIT;
				// 每次探测间隔变成原来的1.5倍
				kcp->probe_wait += kcp->probe_wait / 2;
				// 如果探测间隔大于上限
				if (kcp->probe_wait > IKCP_PROBE_LIMIT)
					kcp->probe_wait = IKCP_PROBE_LIMIT;
				// 修改探测标志(发送探测), 设置下次探测时间
				kcp->ts_probe = kcp->current + kcp->probe_wait;
				kcp->probe |= IKCP_ASK_SEND;
			}
		}
	}
	else {
		// 远程窗口大小为非0, 初始化探测参数
		kcp->ts_probe = 0;
		kcp->probe_wait = 0;
	}

	// flush window probing commands
	// 实际发送探测请求
	if (kcp->probe & IKCP_ASK_SEND) {
		seg.cmd = IKCP_CMD_WASK;
		size = (int)(ptr - buffer);
		if (size + (int)IKCP_OVERHEAD > (int)kcp->mtu) {
			ikcp_output(kcp, buffer, size);
			ptr = buffer;
		}
		ptr = ikcp_encode_seg(ptr, &seg);
	}

	// 将自己的窗口大小发过去
	// flush window probing commands
	if (kcp->probe & IKCP_ASK_TELL) {
		seg.cmd = IKCP_CMD_WINS;
		size = (int)(ptr - buffer);
		if (size + (int)IKCP_OVERHEAD > (int)kcp->mtu) {
			ikcp_output(kcp, buffer, size);
			ptr = buffer;
		}
		ptr = ikcp_encode_seg(ptr, &seg);
	}
	// 重置探测标志位
	kcp->probe = 0;
#pragma endregion window probe
	// 计算窗口大小
	// calculate window size
	// 拥塞窗口大小取发送窗口和远程窗口的最小值
	cwnd = _imin_(kcp->snd_wnd, kcp->rmt_wnd);
	// 如果开启了拥塞窗口功能
	if (kcp->nocwnd == 0) 
		// 修改拥塞窗口值, 不大于发送窗口和远程窗口
		cwnd = _imin_(kcp->cwnd, cwnd);

	// 将用户需要发送的数据移至需要发送的buffer中
	// seg全是新的
	// move data from snd_queue to snd_buf
	#pragma region 将待发送新数据移至发送缓冲区
	while (_itimediff(kcp->snd_nxt, kcp->snd_una + cwnd) < 0) {	// 根据拥塞窗口大小决定总移动数据的大小
		IKCPSEG *newseg;
		if (iqueue_is_empty(&kcp->snd_queue)) break;

		// 拿到snd_queue中的节点
		newseg = iqueue_entry(kcp->snd_queue.next, IKCPSEG, node);
		// 将节点pop出来
		iqueue_del(&newseg->node);
		// 将node插到sendbuff中
		iqueue_add_tail(&newseg->node, &kcp->snd_buf);
		// 更新计数
		kcp->nsnd_que--;
		kcp->nsnd_buf++;

		newseg->conv = kcp->conv;
		newseg->cmd = IKCP_CMD_PUSH;
		newseg->wnd = seg.wnd;
		newseg->ts = current;
		// seq号赋值并自增
		newseg->sn = kcp->snd_nxt++;
		// 期望收到的下一个序号
		newseg->una = kcp->rcv_nxt;
		// 立刻需要发送
		newseg->resendts = current;
		newseg->rto = kcp->rx_rto;
		// 初始化
		newseg->fastack = 0;
		newseg->xmit = 0;
	}
	#pragma endregion 将待发送新数据移至发送缓冲区
	// calculate resent
	// 设置快速重传阈值
	resent = (kcp->fastresend > 0)? (IUINT32)kcp->fastresend : 0xffffffff;
	// 如果开启delayed ack
	rtomin = (kcp->nodelay == 0)? (kcp->rx_rto >> 3) : 0;

	// flush data segments
	#pragma region 将发送缓冲区的数据进行分块并发送
	// 将发送buffer中所有的seg拿出来
	// 从head开始顺序取出
	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = p->next) {
		IKCPSEG *segment = iqueue_entry(p, IKCPSEG, node);
		int needsend = 0;	// 是否需要发送
		if (segment->xmit == 0) {	// 完全没发送过的包
			needsend = 1;
			segment->xmit++;
			segment->rto = kcp->rx_rto;
			// 重发时间 = 当前时间 + RTO超时重传 + RTO下限(是否delayed ack)
			segment->resendts = current + segment->rto + rtomin;
		}
		else if (_itimediff(current, segment->resendts) >= 0) {	// 满足重传条件的seg
			needsend = 1;
			segment->xmit++;
			kcp->xmit++;
			if (kcp->nodelay == 0) {
				// delayed ack
				// 翻倍rto
				segment->rto += _imax_(segment->rto, (IUINT32)kcp->rx_rto);
			}
			else {
				// 步长在合法的情况下 = seg的rto
				IINT32 step = (kcp->nodelay < 2) ? ((IINT32)(segment->rto)) : kcp->rx_rto;
				// rto翻1.5倍
				segment->rto += step / 2;
			}
			// 重传时间戳更新
			segment->resendts = current + segment->rto;
			lost = 1;
		}
		else if (segment->fastack >= resent) {	// 满足快速重传条件
			// 小于快速重传上限或无上限
			if ((int)segment->xmit <= kcp->fastlimit || kcp->fastlimit <= 0) {
				needsend = 1;
				segment->xmit++;
				// 快速重传计数清0
				segment->fastack = 0;
				segment->resendts = current + segment->rto;
				// 存在快速重传
				change++;
			}
		}
		// 这个segment是否需要发送
		if (needsend) {
			int need;
			segment->ts = current;
			segment->wnd = seg.wnd;
			segment->una = kcp->rcv_nxt;
			// buffer中的数据大小
			size = (int)(ptr - buffer);
			// 此segment的大小
			need = IKCP_OVERHEAD + segment->len;
			// 总需要发送的数据大小超过单次上限
			if (size + need > (int)kcp->mtu) {
				// 先把buffer里的发了
				ikcp_output(kcp, buffer, size);
				ptr = buffer;
			}
			// 将segment内容写到buffer中
			ptr = ikcp_encode_seg(ptr, segment); //	overhead
			if (segment->len > 0) {
				memcpy(ptr, segment->data, segment->len);	// 实际数据
				ptr += segment->len;
			}
			//	超过发送上限了, 可以判断连接状态异常
			if (segment->xmit >= kcp->dead_link) {
				kcp->state = (IUINT32)-1;
			}
		}
	}
	// 发送不满一个mtu的segment
	// flash remain segments
	size = (int)(ptr - buffer);
	if (size > 0) {
		ikcp_output(kcp, buffer, size);
	}

	#pragma endregion 将发送缓冲区的数据进行分块并发送
	// update ssthresh
	// 如有快速重传则需要改变拥塞窗口
	// 调节增长速率，避免拥塞
	if (change) {
		// 慢启动阈值变为未确认数据包的数量的一半
		IUINT32 inflight = kcp->snd_nxt - kcp->snd_una;
		kcp->ssthresh = inflight / 2;
		// 慢启动不小于慢启动下限
		if (kcp->ssthresh < IKCP_THRESH_MIN)
			kcp->ssthresh = IKCP_THRESH_MIN;
		// 慢启动阈值 + 快速重传阈值
		kcp->cwnd = kcp->ssthresh + resent;
		kcp->incr = kcp->cwnd * kcp->mss;
	}

	// 是否丢过包
	// 重新进入慢启动
	if (lost) {
		kcp->ssthresh = cwnd / 2;
		// 阈值不小于下限
		if (kcp->ssthresh < IKCP_THRESH_MIN)
			kcp->ssthresh = IKCP_THRESH_MIN;
		kcp->cwnd = 1;
		kcp->incr = kcp->mss;
	}
	// 初始化拥塞窗口大小 和 增量=最大传输
	// 慢启动
	if (kcp->cwnd < 1) {
		kcp->cwnd = 1;
		kcp->incr = kcp->mss;
	}
}


//---------------------------------------------------------------------
// update state (call it repeatedly, every 10ms-100ms), or you can ask 
// ikcp_check when to call it again (without ikcp_input/_send calling).
// 'current' - current timestamp in millisec. 
//---------------------------------------------------------------------
void ikcp_update(ikcpcb *kcp, IUINT32 current)
{
	IINT32 slap;

	kcp->current = current;

	if (kcp->updated == 0) {
		kcp->updated = 1;
		kcp->ts_flush = kcp->current;
	}

	// 是否需要flush = 当前时间 - 下一次flush时间
	slap = _itimediff(kcp->current, kcp->ts_flush);

	// 异常情况，即刻flush
	if (slap >= 10000 || slap < -10000) {
		kcp->ts_flush = kcp->current;
		slap = 0;
	}

	// 需要flush
	if (slap >= 0) {
		kcp->ts_flush += kcp->interval;
		if (_itimediff(kcp->current, kcp->ts_flush) >= 0) {
			kcp->ts_flush = kcp->current + kcp->interval;
		}
		// 只在flush中真正tick网络信息
		ikcp_flush(kcp);
	}
}


//---------------------------------------------------------------------
// Determine when should you invoke ikcp_update:
// returns when you should invoke ikcp_update in millisec, if there 
// is no ikcp_input/_send calling. you can call ikcp_update in that
// time, instead of call update repeatly.
// Important to reduce unnacessary ikcp_update invoking. use it to 
// schedule ikcp_update (eg. implementing an epoll-like mechanism, 
// or optimize ikcp_update when handling massive kcp connections)
//---------------------------------------------------------------------
IUINT32 ikcp_check(const ikcpcb *kcp, IUINT32 current)
{
	IUINT32 ts_flush = kcp->ts_flush;
	IINT32 tm_flush = 0x7fffffff;
	IINT32 tm_packet = 0x7fffffff;
	IUINT32 minimal = 0;
	struct IQUEUEHEAD *p;

	if (kcp->updated == 0) {
		return current;
	}

	if (_itimediff(current, ts_flush) >= 10000 ||
		_itimediff(current, ts_flush) < -10000) {
		ts_flush = current;
	}

	if (_itimediff(current, ts_flush) >= 0) {
		return current;
	}

	tm_flush = _itimediff(ts_flush, current);

	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = p->next) {
		const IKCPSEG *seg = iqueue_entry(p, const IKCPSEG, node);
		IINT32 diff = _itimediff(seg->resendts, current);
		if (diff <= 0) {
			return current;
		}
		if (diff < tm_packet) tm_packet = diff;
	}

	minimal = (IUINT32)(tm_packet < tm_flush ? tm_packet : tm_flush);
	if (minimal >= kcp->interval) minimal = kcp->interval;

	return current + minimal;
}



int ikcp_setmtu(ikcpcb *kcp, int mtu)
{
	char *buffer;
	if (mtu < 50 || mtu < (int)IKCP_OVERHEAD) 
		return -1;
	buffer = (char*)ikcp_malloc((mtu + IKCP_OVERHEAD) * 3);
	if (buffer == NULL) 
		return -2;
	kcp->mtu = mtu;
	kcp->mss = kcp->mtu - IKCP_OVERHEAD;
	ikcp_free(kcp->buffer);
	kcp->buffer = buffer;
	return 0;
}

int ikcp_interval(ikcpcb *kcp, int interval)
{
	if (interval > 5000) interval = 5000;
	else if (interval < 10) interval = 10;
	kcp->interval = interval;
	return 0;
}

int ikcp_setConfig(ikcpcb *kcp, int nodelay, int interval, int resend, int nc)
{
	if (nodelay >= 0) {
		kcp->nodelay = nodelay;
		if (nodelay) {
			kcp->rx_minrto = IKCP_RTO_NDL;	
		}	
		else {
			kcp->rx_minrto = IKCP_RTO_MIN;
		}
	}
	if (interval >= 0) {
		if (interval > 5000) interval = 5000;
		else if (interval < 10) interval = 10;
		kcp->interval = interval;
	}
	if (resend >= 0) {
		kcp->fastresend = resend;
	}
	if (nc >= 0) {
		kcp->nocwnd = nc;
	}
	return 0;
}


int ikcp_wndsize(ikcpcb *kcp, int sndwnd, int rcvwnd)
{
	if (kcp) {
		if (sndwnd > 0) {
			kcp->snd_wnd = sndwnd;
		}
		if (rcvwnd > 0) {   // must >= max fragment size
			kcp->rcv_wnd = _imax_(rcvwnd, IKCP_WND_RCV);
		}
	}
	return 0;
}

int ikcp_waitsnd(const ikcpcb *kcp)
{
	return kcp->nsnd_buf + kcp->nsnd_que;
}


// read conv
IUINT32 ikcp_getconv(const void *ptr)
{
	IUINT32 conv;
	ikcp_decode32u((const char*)ptr, &conv);
	return conv;
}


