/*
 * Copyright (C) 2022 Nubia Cube Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include "ringbuffer.h"

void addItem(void *thiz, RBItem *item);
void addItemData(void *thiz, int tid0, int tid1, int tid2);
void dump(void *thiz);
void dumpToBuf(void *thiz, char* buf, int len);
void clear(void *thiz);
int getCnt(void *thiz);
int getSize(void *thiz);
void setSize(void *thiz, int size);
int isEmpty(void *thiz);
int isFull(void *thiz);
int getMostFrequent(void *thiz, RBItem *output);
void debug(void *thiz, short d);

int isEquels(RBItem* item1, RBItem* item2) {
	if(item1->tid0 != item2->tid0) {
          return 0;
	}
	if(item1->tid1 != item2->tid1) {
          return 0;
	}
	if(item1->tid2 != item2->tid2) {
          return 0;
	}
	if(item1->tid3 != item2->tid3) {
          return 0;
	}
	return 1;
}


void print(RBItem *pItem, int idx, int cnt) {
      NLOGD(" item idx:%d - %d, %d, %d, %d  - %d\n", idx, pItem->tid0, pItem->tid1, pItem->tid2, pItem->tid3, cnt);
}
RingBuffer *createRingBuffer(int size){
        RingBuffer *ringbuffer = NULL;
	if(size < 2) {
		NLOGD("error, size must > 2, but %d\n", size);
		return NULL;
	}
	ringbuffer = (RingBuffer*)kmalloc(sizeof(RingBuffer), GFP_KERNEL);
	ringbuffer->mSize = size;
	ringbuffer->mDebug = 0;
	ringbuffer->pBase = (RBItem*)kmalloc(sizeof(RBItem)*size, GFP_KERNEL);
	ringbuffer->addItem = addItem;
	ringbuffer->dump = dump;
	ringbuffer->dumpToBuf = dumpToBuf;
	ringbuffer->clear = clear;
	ringbuffer->debug = debug;
	ringbuffer->isEmpty = isEmpty;
	ringbuffer->isFull = isFull;
	ringbuffer->getCnt = getCnt;
	ringbuffer->getSize = getSize;
	ringbuffer->setSize = setSize;
	ringbuffer->pAttr = (int*)kmalloc(sizeof(int)*size, GFP_KERNEL);
	ringbuffer->getMostFrequent = getMostFrequent;
	clear(ringbuffer);
        NLOGD("ringbuffer %p be created size:%d\n", ringbuffer, size);
	return ringbuffer;
}

void destroyRingBuffer(RingBuffer *ringbuffer){
	if(ringbuffer == NULL) {
		NLOGD("error, need thiz pointer\n");
		return;
	}
	kfree(ringbuffer->pAttr);
	ringbuffer->pAttr = NULL;
	kfree(ringbuffer->pBase);
	ringbuffer->pBase = NULL;
	NLOGD("ringbuffer %p be destroyed\n", ringbuffer);
	kfree(ringbuffer);
}

void clear(void *thiz) {
        RingBuffer *me = NULL;
	if(thiz == NULL) {
		NLOGD("error, need thiz pointer\n");
		return;
	}
	me = (RingBuffer*)thiz;
	memset(me->pBase, 0, sizeof(RBItem)*me->mSize);
	memset(me->pAttr, 0, sizeof(int)*me->mSize);
	me->mFront = 0;
	me->mRear = 0;
}
void setSize(void *thiz, int size) {
    RingBuffer *me = NULL;
    if(thiz == NULL) {
      NLOGD("error, need thiz pointer\n");
      return;
    }
    me = (RingBuffer*)thiz;
    me->mSize = size;
}
int getSize(void *thiz) {
    RingBuffer *me = NULL;
    if(thiz == NULL) {
      NLOGD("error, need thiz pointer\n");
      return -1;
    }
    me = (RingBuffer*)thiz;
    return me->mSize;
}
int getCnt(void *thiz) {
    RingBuffer *me = NULL;
    if(thiz == NULL) {
    	NLOGD("error, need thiz pointer\n");
    	return -1;
    }
    me = (RingBuffer*)thiz;
    if(me->mFront == me->mRear) {
    	return 0;
    } else if(me->mFront > me->mRear) {
    	return me->mSize - (me->mFront - me->mRear);
    } else if(me->mFront < me->mRear) {
    	return me->mRear - me->mFront;
    } 
    return -1;
}

int isEmpty(void *thiz) {
	RingBuffer *me = NULL;
	if(thiz == NULL) {
		NLOGD("error, need thiz pointer\n");
		return -1;
	}
	me = (RingBuffer*)thiz;
	if(me->mFront == me->mRear) {
		return 1;
	} 
	return 0;
}

int isFull(void *thiz) {
	int nextRear = -1;
        RingBuffer *me = NULL;
	if(thiz == NULL) {
		NLOGD("error, need thiz pointer\n");
		return -1;
	}
	me = (RingBuffer*)thiz;
        nextRear = (me->mRear + 1)%me->mSize;
	if(me->mFront == nextRear) {
		return 1;
	} 
	return 0;
}
void addItem(void *thiz, RBItem *item){
	int nextRear = 0;
	RingBuffer *me = NULL;
	if(thiz == NULL) {
		NLOGD("error, need thiz pointer\n");
		return;
	}
	me = (RingBuffer*)thiz;
	nextRear = (me->mRear + 1)%me->mSize;
	if( nextRear == me->mFront) { //queue full
		me->mFront = (me->mFront + 1)%me->mSize;
	}
	memcpy(&me->pBase[me->mRear], item, sizeof(RBItem));

	me->mRear = nextRear;

}

void dump(void *thiz){
	int front = 0;
	int rear = 0;
	RBItem item;
        RingBuffer *me = NULL;
	if(thiz == NULL) {
		NLOGD("error, need thiz pointer\n");
		return;
	}
	me = (RingBuffer*)thiz;
	front = me->mFront;
	rear = me->mRear;
        //update attr
	getMostFrequent(me, &item);
	//print item & attr
	if(me->mDebug > 0)NLOGD("RingBuffer:%p front:%d, rear:%d, size:%d, cnt:%d, isFull:%d, isEmpty:%d\n", me, me->mFront, me->mRear
          , me->mSize, me->getCnt(me), me->isFull(me), me->isEmpty(me));
	while(front != rear) {
		print(&me->pBase[front], front, me->pAttr[front]);
		front = (front + 1)%me->mSize;
	}
}

void dumpToBuf(void *thiz, char* buf, int len){
	/*int front = 0;
	int rear = 0;
	int wlen = 0;
	RBItem item;
        RingBuffer *me = NULL;
	if(thiz == NULL) {
		NLOGD("error, need thiz pointer\n");
		return;
	}
	me = (RingBuffer*)thiz;
	front = me->mFront;
	rear = me->mRear;
        //update attr
	getMostFrequent(me, &item);
	//print item & attr
	wlen = snprintf(buf, len, "RingBuffer:%p front:%d, rear:%d, size:%d, cnt:%d, isFull:%d, isEmpty:%d\n", me, me->mFront
                , me->mRear, me->mSize, me->getCnt(me), me->isFull(me), me->isEmpty(me));
	while(front != rear) {
		wlen += snprintf(buf+wlen, len - wlen, " item idx:%d - %d, %d, %d  - %d\n", front, me->pBase[front].tid0
                       , me->pBase[front].tid1, me->pBase[front].tid2, me->pAttr[front]);
		front = (front + 1)%me->mSize;
		if(wlen >= len - 1){
			break;
		}
	}*/
}

void debug(void *thiz, short d){
        RingBuffer *me = NULL;
	if(thiz == NULL) {
		NLOGD("error, need thiz pointer\n");
		return;
	}
	me = (RingBuffer*)thiz;
	me->mDebug = d;
}

void printAttr(RingBuffer *ringbuffer) {
	int i = 0;
	if(ringbuffer == NULL) {
		NLOGD("error, need thiz pointer\n");
		return;
	}

	NLOGD("      ");
	for(i = 0; i < ringbuffer->mSize; i++) {
		NLOGD(" %d", ringbuffer->pAttr[i]);
	}
	NLOGD("\n");

}
int getMostFrequent(void *thiz, RBItem *output){
	int front = 0;
	int rear = 0;
	int i = 0;
	int mostAppearIdx = -1;
	int appearMax = 0;
        RingBuffer *me = NULL;
	if(thiz == NULL) {
		NLOGD("error, need thiz pointer\n");
		return -1;
	}
	if(output == NULL) {
		NLOGD("Error, output item is NULL!\n");
		return -2;
	}

	me = (RingBuffer*)thiz;
	front = me->mFront;
	rear = me->mRear;
	if(front == rear) {//empty
           NLOGD("error, get most frequent on empty.\n");
           return -3;
	}
	for(i = 0; i < me->mSize; i++) {
		me->pAttr[i] = 1;
	}
	if(me->mDebug > 0) {
		NLOGD("      mFront:%d  mRear:%d\n", front, rear);
		printAttr(me);
	}
	if(rear > 0) {
           rear--;
	} else {
	   rear = me->mSize - 1;
	}
	while(rear != front) {
		if(me->mDebug > 0) {
		    NLOGD(" rear:%d (%d, %d, %d)\n", rear, me->pBase[rear].tid0, me->pBase[rear].tid1, me->pBase[rear].tid2);
	    }
	    if(me->pAttr[rear] <= 0) {
	    	if(rear > 0) {
				rear--;
			} else {
				rear = me->mSize - 1;
			}
			continue;
	    }

		i = me->mFront;
		while( i != rear ) {
			if(me->mDebug > 0) {
			    NLOGD("    i:%d (%d, %d, %d)\n", i,me->pBase[i].tid0,me->pBase[i].tid1,me->pBase[i].tid2);
		        }
			if(isEquels(&me->pBase[rear], &me->pBase[i])) {
				me->pAttr[rear]++;
				me->pAttr[i] = 0;
				if(me->mDebug > 0) {
				    NLOGD("      vote %d to %d\n", i, rear);
			    }
			}
			i = (i + 1)%me->mSize;
		}

		if(rear > 0) {
			rear--;
		} else {
			rear = me->mSize - 1;
		}

		if(me->mDebug > 0) {
		    printAttr(me);
		    NLOGD("        next rear:%d\n", rear);
		}

	}

	front = me->mFront;
	rear = me->mRear;
	while(rear != front) {
		if(me->pAttr[rear] > appearMax) {
			appearMax = me->pAttr[rear];
			mostAppearIdx = rear;
		}
		if(rear > 0) {
			rear--;
		} else {
			rear = me->mSize - 1;
		}
	}
	memcpy(output, &me->pBase[mostAppearIdx], sizeof(RBItem));

	if(me->mDebug > 0) {
	   NLOGD("      most frequent index:%d value:%d (%d,%d,%d,%d)\n", mostAppearIdx, appearMax, output->tid0, output->tid1, output->tid2, output->tid3);
        }
    return 0;

}
