/*Protocol 6 (Selective repeat)  */
#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"


typedef unsigned char seq_nr; //���/* sequence or ack numbers *///����������char��
typedef enum { data, ack, nak } frame_kind; /* frame kind definition */

/*֡�ṹ*/
typedef struct { /* frames are transported in this layer */
	unsigned char kind; /* data/ack/nak��ÿ��֡����*/
	seq_nr ack; /* ack��ţ�ÿ��֡����*/
	seq_nr seq; /* data��ţ�������ack��nakû��*/
	unsigned char data[PKT_LEN];//������ack��nakû��/* the network layer packet */
	unsigned int  padding;//����֡��CRC����֡��CRC��ǰ�棩
} frame;

#define MAX_SEQ 7 //������/* should be 2^n- 1 */
#define NR_BUFS ((MAX_SEQ+ 1)/2)  //��󻺳�������
#define ACK_TIMER 50 //ACK��ʱ������λms
#define DATA_TIMER  2000

/*ȫ�ֱ���*/
static unsigned char out_buf[NR_BUFS][PKT_LEN]; /*���������*/
static unsigned char in_buf[NR_BUFS][NR_BUFS];/*���������*/


bool no_nak = true; /*no nak has been sent yet */
seq_nr oldest_frame = MAX_SEQ + 1; /* initial value is only for the simulator */

/* Same as between in protocol 5, but shorter and more obscure. */
static bool between(seq_nr a, seq_nr b, seq_nr c)
{
	return ((a <= b) && (b < c)) || ((c < a) && (a <= b)) || ((b < c) && (c < a));
}

/* Construct and send a data, ack, or nak frame. */
/*���һ֡��data/ack/nak�������������*/
static void put_frame(frame_kind fk/*֡������*/, seq_nr frame_nr/*data�����*/, seq_nr frame_expected, static unsigned char buffer[]=NULL)
{
		
	frame s; /* scratch variable */
	int len = 2;//��С��֡��ֻ��kind��ack
	s.kind = fk; /* kind == data, ack,nak*/
	if (fk == data)
	{
		strcpy(s.data, buffer);//��buffer���packet������֡��//dest,src
		//s.data = buffer [frame_nr %NR_BUFS];//���������﷨���⣿��
		len = 3 + PKT_LEN;//kind,ack,seq,data[]
	}
	s.seq = frame_nr; /* only meaningful for data frames */
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
	if (fk == nak) 
		no_nak = false; /* one nak per frame, please */

	/*��У��λ��ĩβ*/
	*(unsigned int *)((unsigned char *)&s + len/*ֱ�Ӵֱ�����֡ĩβ*/) = crc32((unsigned char *)&s, len);//��У��λ//��ѧǿ������ת��
	
	/*�������*/
	send_frame((unsigned char *)&s, len + 4);//��tm��4������

	if (fk == data) 
		start_timer(frame_nr %NR_BUFS,DATA_TIMER);
		stop_ack_timer(); /* no need for separate ack frame */
}
void main(int argc, char **argv)
{
	seq_nr ack_expected;/* lower edge of sender �� s window */
	seq_nr next_frame_to_send;/* upper edge of sender's window + 1*/
	seq_nr frame_expected;/* lower edge of receiver's window*/
	seq_nr too_far;/* upper edge of receiver's window +1 */
	//static unsigned char frame_nr = 0, buffer[NR_BUFS][PKT_LEN];
	//unsigned char out_buf[NR_BUFS][PKT_LEN]

	int event, arg;
	int len = 0;
	frame f; /* scratch variable */

	bool arrived[NR_BUFS];/* inbound bit map */
	seq_nr nbuffered;/* ��������������˼���//���ʹ��ڴ�С����*/
	enable_network_layer();/* initialize */
	ack_expected = 0;/* next ack expected on the inbound stream */
	next_frame_to_send = 0;/* number of next outgoing frame */
	frame_expected = 0;
	too_far = NR_BUFS;/*���մ����Ͻ�+1*/
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
			nbuffered = nbuffered + 1; /* expand the window */
			get_packet(out_buf[next_frame_to_send % NR_BUFS]);/* ȡ packet */
			put_frame(data, next_frame_to_send, frame_expected, out_buf[next_frame_to_send % NR_BUFS]);/* �� frame */
			inc(next_frame_to_send);/* advance upper window edge */ //???ѭ���ӻ���ͨ++��������
			break;

		case FRAME_RECEIVED: /* a data or control frame has arrived*/
			//from_physical_layer(&r); /* fetch incoming frame from physical layer */
			/*CRCУ��*///????
			len = recv_frame((unsigned char *)&f, sizeof f);//��������ȡһ֡����
			if (len < 5 || crc32((unsigned char *)&f, len) != 0) {
				dbg_event((char*)"**** Receiver Error, Bad CRC Checksum\n");//��ѧǿ������ת��
				break;
			}

			if (f.kind == data)//У����ȷ�����뻺���������ܿ��Խ��������
			{
				dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short *)f.data);
				if ((f.seq != frame_expected) && no_nak)
					put_frame(nak, 0, frame_expected);//ȱʡ���һ������
				else//f.seq == frame_expected
					start_ack_timer(ACK_TIMER);
				if (between(frame_expected, f.seq, too_far) && (arrived[f.seq%NR_BUFS] == false)) /* Frames may be accepted in any order. */
				{
					arrived[f.seq%NR_BUFS] == true; /* mark buffer as full */
					strcpy(in_buf[f.seq%NR_BUFS], f.data);/*data����in_buf*/
					while (arrived[frame_expected %NR_BUFS]) /* Pass frames and advance window. */
					{
						put_packet(in_buf[frame_expected %NR_BUFS], len - 7/*???*/);//���������//�����ֶ�ʲô��
						no_nak = true;
						arrived[frame_expected % NR_BUFS] = false;
						inc(frame_expected); /*advance lower edge of receiver's window*/
						inc(too_far); /* advance upper edge of receiver's window */
						start_ack_timer(ACK_TIMER); /* to see if a separate ack is needed */
					}
				}
			}
			if ((f.kind == nak) && between(ack_expected, (f.ack + 1) % (MAX_SEQ + 1), next_frame_to_send))
				put_frame(data, (f.ack + 1) % (MAX_SEQ + 1), frame_expected,);//������outbuf��

			/*ÿ��֡�����ACK���ڴ��ۻ�ȷ��*///f.ack������[ack_expected,next_frame_to_send)��
			while (between(ack_expected, f.ack, next_frame_to_send))//�ۼ�ȷ��
			{
				nbuffered = nbuffered - 1; /* handle piggybacked ack */
				stop_timer(ack_expected %NR_BUFS); /* frame arrived intact */
				inc(ack_expected); /*advance lower edge of sender�� B window *///???ѭ���ӻ���ͨ++��������
			}
			break;

		case DATA_TIMEOUT:
			dbg_event("---- DATA %d timeout\n", arg);
			put_frame(data, oldest_frame, frame_expected, out_buf[oldest_frame%NR_BUFS]/*??*/); /*we timed out*/
			break;

		case ACK_TIMEOUT:
			dbg_event("---- ACK %d timeout\n", frame_expected - 1);//����
			put_frame(ack, 0, frame_expected); /* ack timer expired; send ack */
		}

		if (nbuffered < NR_BUFS)
			enable_network_layer();
		else
			disable_network_layer();
	}
}

void inc(seq_nr &seq)//MAX_SEQѭ����
{
	seq = (++seq) % MAX_SEQ;
}