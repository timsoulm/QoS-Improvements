#include <slack/std.h>
#include <slack/map.h>
#include <slack/list.h>
#include <pthread.h>
#include "protocols.h"
#include "packetcore.h"
#include "message.h"
#include "grouter.h"

// WCWeightedFairScheduler: is one part of the W+FQ scheduler.
// It picks the appropriate job from the system of queues.
// If no job in packet core.. the thread waits.
// If there is job, select a Queue and then the job at the head of the Queue
// for running. Update the algorithm parameters and store them back on the queue
// datastructure: start, finish times for the queue and virtual time for the system.

// TODO: Debug this function..

extern router_config rconfig;
// WCWeightFairQueuer: function called by the classifier to enqueue
// the packets..
// TODO: Debug this function...
void *weightedFairScheduler(void *pc)
{
	pktcore_t *pcore = (pktcore_t *)pc;
	List *keylst;
	int nextqid, qcount, rstatus, pktsize;
	char *nextqkey;
	gpacket_t *in_pkt;
	simplequeue_t *nextq;

	long queueSizes[100]; //Array to store bytes sent for each queue id
	int PacketsSent[10];
	int i, j;//To iterate through the queue length array
	long crtStarvedQid;
	double crtStarvedQidWeight;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	while (1)
	{
		verbose(1, "[weightedFairScheduler]:: Weighted Fair Queue schedule processing... ");
		keylst = map_keys(pcore->queues);
		qcount = list_length(keylst);

		pthread_mutex_lock(&(pcore->qlock));
		if (pcore->packetcnt == 0)
			pthread_cond_wait(&(pcore->schwaiting), &(pcore->qlock));
		pthread_mutex_unlock(&(pcore->qlock));

		pthread_testcancel();

			 for(i = 0; i <qcount; i++)
			 {
				nextqkey = list_item(keylst, i);
				// get the queue..
				nextq = map_get(pcore->queues, nextqkey);			 	
			 	if(GetQueueSize(nextq) > 0)
			 	{
			 		crtStarvedQidWeight = (queueSizes[i] / (nextq->weight) ); //TODO
			 		crtStarvedQid = i;
			 		break;
			 	}
			 }

			 if(i == qcount)
			 {
			 	
			 	list_release(keylst);
				usleep(rconfig.schedcycle);
				continue;
			 }

			for(j = i; j < qcount; j++)
			{
				nextqkey = list_item(keylst, j);
				// get the queue..
				nextq = map_get(pcore->queues, nextqkey);
				if(( (queueSizes[j] / (nextq->weight)) < crtStarvedQidWeight) && (GetQueueSize(nextq) > 0)) //TODO
				{
					crtStarvedQid = j;
					crtStarvedQidWeight = queueSizes[j] / (nextq->weight) ; //TODO
				}
			}
			nextqid = crtStarvedQid;
			nextqkey = list_item(keylst, nextqid);
			// get the queue..
			nextq = map_get(pcore->queues, nextqkey);
			// read the queue..
			rstatus = readQueue(nextq, (void **)&in_pkt, &pktsize);//Here we get the packet size.
			
			
			if (rstatus == EXIT_SUCCESS)
			{
				writeQueue(pcore->workQ, in_pkt, pktsize);
				verbose(1, "[weightedFairScheduler---Just sent]:: Queue[%d] has now sent %lu bytes", nextqid, queueSizes[nextqid]);
				queueSizes[nextqid] = queueSizes[nextqid] + findPacketSize(&(in_pkt->data));//Storing updated data sent in array
				PacketsSent[nextqid]++;
				
			}
		
			for(i = 0; i <qcount; i++)	
			{
				nextqkey = list_item(keylst, i);
				// get the queue..
				nextq = map_get(pcore->queues, nextqkey);
				verbose(1, "Packets Queued[%d] = %d,  Bytes sent = %d, Packets Sent = %d", i, GetQueueSize(nextq), queueSizes[i], PacketsSent[i]);
			}
			list_release(keylst);

			pthread_mutex_lock(&(pcore->qlock));
			if (rstatus == EXIT_SUCCESS) 
			{
				(pcore->packetcnt)--;
			}
			pthread_mutex_unlock(&(pcore->qlock));
		
			usleep(rconfig.schedcycle);
	}
}
