//=====================================================================
//
// test.cpp - kcp 测试用例
//
// 说明：
// gcc test.cpp -o test -lstdc++
//
//=====================================================================

#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <iostream>
#include "test.h"
#include "ikcp.c"

using namespace std;

// 模拟网络
LatencySimulator *vnet;

// 模拟网络：模拟发送一个 udp包
int udp_output(const char *buf, int len, ikcpcb *kcp, void *user)
{
	union { int id; void *ptr; } parameter;
	parameter.ptr = user;
	vnet->send(parameter.id, buf, len);
	return 0;
}

// 测试用例
void test(int mode, FILE* file)
{
	// 创建模拟网络：丢包率10%，Rtt 60ms~125ms
	vnet = new LatencySimulator(10, 60, 125);

	// 创建两个端点的 kcp对象，第一个参数 conv是会话编号，同一个会话需要相同
	// 最后一个是 user参数，用来传递标识
	ikcpcb *kcp1 = ikcp_create(0x11223344, (void*)0);
	ikcpcb *kcp2 = ikcp_create(0x11223344, (void*)1);

	// 设置kcp的下层输出，这里为 udp_output，模拟udp网络输出函数
	kcp1->output = udp_output;
	kcp2->output = udp_output;

	IUINT32 current = iclock();
	IUINT32 slap = current + 20;
	IUINT32 index = 0;
	IUINT32 next = 0;
	IINT64 sumrtt = 0;
	int count = 0;
	int maxrtt = 0;

	// 配置窗口大小：平均延迟200ms，每20ms发送一个包，
	// 而考虑到丢包重发，设置最大收发窗口为128
	ikcp_wndsize(kcp1, 128, 128);
	ikcp_wndsize(kcp2, 128, 128);

	// 判断测试用例的模式
	if (mode == 0) {
		// 默认模式
		ikcp_setConfig(kcp1, 0, 10, 0, 0);
		ikcp_setConfig(kcp2, 0, 10, 0, 0);
	}
	else if (mode == 1) {
		// 普通模式，关闭流控等
		ikcp_setConfig(kcp1, 0, 10, 0, 1);
		ikcp_setConfig(kcp2, 0, 10, 0, 1);
	}	else {
		// 启动快速模式
		// 第二个参数 nodelay-启用以后若干常规加速将启动
		// 第三个参数 interval为内部处理时钟，默认设置为 10ms
		// 第四个参数 resend为快速重传指标，设置为2
		// 第五个参数 为是否禁用常规流控，这里禁止
		ikcp_setConfig(kcp1, 2, 10, 2, 1);
		ikcp_setConfig(kcp2, 2, 10, 2, 1);
		kcp1->rx_minrto = 10;
		kcp1->fastresend = 1;
	}


	char buffer[2000];
	int hr;

	IUINT32 ts1 = iclock();

	while (1) {
		isleep(1);
		current = iclock();
		ikcp_update(kcp1, iclock());
		ikcp_update(kcp2, iclock());

		// 每隔 20ms，kcp1发送数据
		for (; current >= slap; slap += 20) {
			((IUINT32*)buffer)[0] = index++;
			((IUINT32*)buffer)[1] = current;

			// 发送上层协议包
			ikcp_send(kcp1, buffer, 8);
		}

		// 处理虚拟网络：检测是否有udp包从p1->p2
		while (1) {
			hr = vnet->recv(1, buffer, 2000);
			if (hr < 0) break;
			// 如果 p2收到udp，则作为下层协议输入到kcp2
			ikcp_input(kcp2, buffer, hr);
		}

		// 处理虚拟网络：检测是否有udp包从p2->p1
		while (1) {
			hr = vnet->recv(0, buffer, 2000);
			if (hr < 0) break;
			// 如果 p1收到udp，则作为下层协议输入到kcp1
			ikcp_input(kcp1, buffer, hr);
		}

		// kcp2接收到任何包都返回回去
		while (1) {
			hr = ikcp_recv(kcp2, buffer, 10);
			// 没有收到包就退出
			if (hr < 0) break;
			// 如果收到包就回射
			ikcp_send(kcp2, buffer, hr);
		}

		// kcp1收到kcp2的回射数据
		while (1) {
			hr = ikcp_recv(kcp1, buffer, 10);
			// 没有收到包就退出
			if (hr < 0) break;
			IUINT32 sn = *(IUINT32*)(buffer + 0);
			IUINT32 ts = *(IUINT32*)(buffer + 4);
			IUINT32 rtt = current - ts;
			
			if (sn != next) {
				// 如果收到的包不连续
				printf("ERROR sn %d<->%d\n", (int)count, (int)next);
				return;
			}

			next++;
			sumrtt += rtt;
			count++;
			if (rtt > (IUINT32)maxrtt) maxrtt = rtt;

			printf("[RECV] mode=%d sn=%d rtt=%d\n", mode, (int)sn, (int)rtt);
		}
		if (next > 1000) break;
	}

	ts1 = iclock() - ts1;

	ikcp_release(kcp1);
	ikcp_release(kcp2);

	const char *names[3] = { "默认类tcp", "普通kcp", "极速kcp" };
	printf("%s 模式实验结果 (%dms):\n", names[mode], (int)ts1);
	fprintf(file,"%s 模式实验结果 (%dms):\n", names[mode], (int)ts1);
	printf("avgrtt=%d maxrtt=%d tx1=%d tx2=%d\n", (int)(sumrtt / count), (int)maxrtt, (int)vnet->tx1,(int)vnet->tx2);
	fprintf(file,"avgrtt=%d maxrtt=%d tx1=%d tx2=%d\n\n", (int)(sumrtt / count), (int)maxrtt, (int)vnet->tx1,(int)vnet->tx2);
	printf("press enter to next ...\n");

	// 将结果写入Log文件中


	char ch; scanf("%c", &ch);
}

int main()
{
	const char* fileName = "OutputLog.txt";
	FILE* file;
	

	if(file = fopen(fileName,"wt+")){
		cout << "log文件打开成功"<<endl;

		fputs("开始实验，模拟网络丢包率为10%，rtt为60~125ms, flush间隔为10ms;\n一端发送数据, 另一端收, 收满1000序列号的数据则停止实验：\n\n",file);
		fputs("开始默认模式，类似 TCP：正常模式(delayed ack)，无快速重传，常规拥塞控制:\n",file);
		test(0,file);
		fputs("开始普通模式(delayed ack)，关闭拥塞控制\n",file);
		test(1,file);
		fputs("快速模式，所有开关都打开，且关闭拥塞控制\n",file);
		test(2,file);

		fclose(file);
	}else{
		cout << "log文件打开失败"<<endl;
	}


	return 0;
}

