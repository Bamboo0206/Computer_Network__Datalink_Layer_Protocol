/*Protocol 6 (Selective repeat)  */
#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"



typedef unsigned char seq_nr; //序号
typedef enum { data, ack, nak } frame_kind;

/*帧结构*/
typedef struct { 
	unsigned char kind; /* data/ack/nak，每个帧都有*/
	seq_nr ack; /* ack序号，每个帧都有*/
	seq_nr seq; /* data序号，单独的ack和nak没有*/
	unsigned char data[PKT_LEN];//单独的ack和nak没有/* the network layer packet */
	unsigned int  padding;//数据帧的CRC（短帧的CRC在前面）
} frame;

#define MAX_SEQ 7 //最大序号/* should be 2^n- 1 */
#define NR_BUFS ((MAX_SEQ+ 1)/2)  //最大缓冲区数量
#define ACK_TIMER 50 //ACK定时器，单位ms
#define DATA_TIMER  700


bool no_nak = true; /*no nak has been sent yet */
//seq_nr oldest_frame = MAX_SEQ + 1; /* initial value is only for the simulator */

void inc(seq_nr *seq);//MAX_SEQ循环加

/* Same as between in protocol 5, but shorter and more obscure. */
static bool between(seq_nr a, seq_nr b, seq_nr c)//b在区间[a,c)内否
{
	return ((a <= b) && (b < c)) || ((c < a) && (a <= b)) || ((b < c) && (c < a));
}

/*打包一帧（data/ack/nak）并交给物理层*/
static void put_frame(frame_kind fk/*帧的类型*/, seq_nr data_seq/*data的序号*/, seq_nr frame_expected, unsigned char *buffer=NULL)
{
	//printf("put_frame called,帧类型%d\n",fk);
	frame s{ 0,0,0,0,0 }; /* scratch variable */
	int len = 2;//最小的帧，只有kind和ack
	s.kind = fk; /* kind == data, ack,nak*/
	s.seq = data_seq; /* only meaningful for data frames */
	s.ack = (frame_expected - 1) % (MAX_SEQ + 1);//??????//第一帧255，char溢出了
	//printf("seq %d  ack %d \n", s.seq, s.ack);
	if (fk == nak)
		no_nak = false; /* one nak per frame, please */
	if (fk == data)
	{
		//printf("1\n");
		memcpy(s.data, buffer, PKT_LEN);//将buffer里的packet拷贝到帧里//dest,src
		//printf("1.1\n");
		len = 3 + PKT_LEN;//kind,ack,seq,data[]
		dbg_frame((char*)"Send DATA %d ACK %d, ID %d\n", s.seq, s.ack, *(short *)s.data);
		//printf("12\n");
	}
	else if(fk==ack)
	{
		printf("2\n");
		dbg_frame((char*)"Send ACK  %d\n", s.ack);
	}
	else if (fk == nak)
	{
		dbg_frame((char*)"Send NAK  %d\n", s.ack);
	}


	/*加校验位在末尾*/
	*(unsigned int *)((unsigned char *)&s + len/*直接粗暴加在帧末尾*/) = crc32((unsigned char *)&s, len);//加校验位//玄学强制类型转换
	
	/*给物理层*/
	
	send_frame((unsigned char *)&s, len + 4);//神tm加4？？？
	//printf("发送完毕\n");

	if (fk == data) 
		start_timer(data_seq %NR_BUFS,DATA_TIMER);
		stop_ack_timer(); /* no need for separate ack frame */
}
int main(int argc, char **argv)
{
	seq_nr ack_expected;/* lower edge of sender ’ s window */
	seq_nr next_frame_to_send;/* upper edge of sender's window + 1*/
	seq_nr frame_expected;/* lower edge of receiver's window*/
	seq_nr too_far;/* upper edge of receiver's window +1 */
	bool arrived[NR_BUFS];/* inbound bit map */
	seq_nr nbuffered;/* 输出缓冲区被用了几个//发送窗口大小？？*/

	unsigned char out_buf[NR_BUFS][PKT_LEN]; /*输出缓冲区*/
	unsigned char in_buf[NR_BUFS][PKT_LEN];/*输出缓冲区*/

	int event = 0, arg = 0;
	int len = 0;
	frame f{ 0,0,0,0,0 };

	//enable_network_layer();/* initialize */
	ack_expected = 0;/* next ack expected on the inbound stream */
	next_frame_to_send = 0;/* number of next outgoing frame */
	frame_expected = 0;
	too_far = NR_BUFS;/*接收窗口上界+1*/
	nbuffered = 0; /* initially no packets are buffered */
	for (int i=0; i < NR_BUFS; i++) 
		arrived[i] = false;

	protocol_init(argc, argv);
	lprintf("Designed by ZQY, build:  2019/4/22\n");

	disable_network_layer();//???????

	while (true)
	{
		event = wait_for_event(&event);/* five possibilities: see event_type above */
		switch (event) {
		case NETWORK_LAYER_READY: /* accept, save, and transmit a new frame */
			//printf("NETWORK_LAYER_READY准备发送一帧\n");
			nbuffered = nbuffered + 1; /* expand the window */
			get_packet(out_buf[next_frame_to_send % NR_BUFS]);/* 取 packet */
			put_frame(data, next_frame_to_send, frame_expected, out_buf[next_frame_to_send % NR_BUFS]);/* 发 frame */
			inc(&next_frame_to_send);/* advance upper window edge */ //循环加 0~7
			//printf("next_frame_to_send为 %d frame_expected为 %d\n", next_frame_to_send, frame_expected);
			break;

		case FRAME_RECEIVED: /* a data or control frame has arrived*/
			//printf("FRAME_RECEIVED收到一帧,类型为%d,期待frame序号为%d\n",f.kind,frame_expected);

			/*CRC校验*/
			len = recv_frame((unsigned char *)&f, sizeof f);//从物理层获取一帧？？
			if (len < 5 || crc32((unsigned char *)&f, len) != 0) {
				dbg_event((char*)"**** Receiver Error, Bad CRC Checksum\n");//玄学强制类型转换
				break;
			}

			if (f.kind == data)//校验正确，放入缓冲区，可能可以交给网络层
			{
				dbg_frame((char*)"Recv DATA %d ACK %d, ID %d\n", f.seq, f.ack, *(short *)f.data);
				if ((f.seq != frame_expected) && no_nak)
					put_frame(nak, 0, frame_expected,NULL);
				else//f.seq == frame_expected
					start_ack_timer(ACK_TIMER);

				/*在接收窗口内并且没有接收过，放入缓冲区*/
				if (between(frame_expected, f.seq, too_far) && (arrived[f.seq % NR_BUFS] == false)) /* Frames may be accepted in any order. */
				{
					arrived[f.seq % NR_BUFS] = true; /* mark buffer as full */
					memcpy(in_buf[f.seq % NR_BUFS], f.data, PKT_LEN);/*data放入in_buf*/
					while (arrived[frame_expected % NR_BUFS]) /* Pass frames and advance window. */
					{
						//printf("交给网络层\n");
						put_packet(in_buf[frame_expected %NR_BUFS], len - 7);//交给网络层
						no_nak = true;
						arrived[frame_expected % NR_BUFS] = false;
						inc(&frame_expected); /*advance lower edge of receiver's window*/
						inc(&too_far); /* advance upper edge of receiver's window */
						start_ack_timer(ACK_TIMER); /* to see if a separate ack is needed */
					}
				}
			}
			if ((f.kind == nak) && between(ack_expected, (f.ack + 1) % (MAX_SEQ + 1), next_frame_to_send))
			{
				//printf("收到单独的NAK %d\n", f.ack);
				put_frame(data, (f.ack + 1) % (MAX_SEQ + 1), frame_expected,out_buf[ (f.ack + 1) % NR_BUFS]);//？？？outbuf？
			}

			if (f.kind == ack)
				//printf("收到单独的ACK %d\n", f.ack);

			/*每个帧都会带ACK，在此累积确认*///f.ack在区间[ack_expected,next_frame_to_send)内
			dbg_frame((char*)"Recv ACK  %d\n", f.ack);
			while (between(ack_expected, f.ack, next_frame_to_send))//累计确认
			{
				nbuffered = nbuffered - 1; /* handle piggybacked ack */
				stop_timer(ack_expected % NR_BUFS); /* frame arrived intact */
				inc(&ack_expected); /*advance lower edge of sender’ B window 循环加*/
				//printf("下次收到期待的ack为：%d\n", ack_expected);
			}
			break;

		case DATA_TIMEOUT:
			dbg_event((char*)"---- DATA %d timeout\n", arg);
			put_frame(data, ack_expected, frame_expected, out_buf[ack_expected % NR_BUFS]/*??*/); /*we timed out*/
			break;

		case ACK_TIMEOUT:
			dbg_event((char*)"---- ACK %d timeout\n", frame_expected - 1);//？？
			put_frame(ack, 0, frame_expected,NULL); /* ack timer expired; send ack */
			break;
		}

		if (nbuffered < NR_BUFS)
			enable_network_layer();
		else
			disable_network_layer();
		//putchar('\n');
	}

	return 0;
}

void inc(seq_nr *seq)//MAX_SEQ循环加
{
	(*seq)++;
	*seq = (*seq) % (MAX_SEQ+1);
}