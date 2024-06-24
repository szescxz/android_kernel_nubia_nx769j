
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
#include <linux/types.h>
#include <linux/string.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/kthread.h>

#ifndef __RINGBUFFER_H__
#define __RINGBUFFER_H__

#define NLOGD(format,args...)    printk(KERN_INFO format,##args)

typedef struct {
	int tid0;
	int tid1;
	int tid2;
	int tid3;
	u64 tid0_time;
	u64 tid1_time;
	u64 tid2_time;
	u64 tid3_time;
        struct kthread_work work;
} RBItem;

typedef struct {
    short mDebug;
    RBItem *pBase;
    int* pAttr;
    int mSize;
    int mFront;
    int mRear;

    void (*addItem)(void *thiz, RBItem *item);
    void (*dump)(void *thiz);
    void (*dumpToBuf)(void *thiz, char* buf, int len);
    void (*clear)(void *thiz);
    int (*getCnt)(void *thiz);
    int (*getSize)(void *thiz);
    void (*setSize)(void *thiz, int size);
    int (*isEmpty)(void *thiz);
    int (*isFull)(void *thiz);
    void (*debug)(void *thiz, short d);// d > 0 :on  d <= 0: off
    int (*getMostFrequent)(void *thiz, RBItem *output);
	
} RingBuffer;

RingBuffer *createRingBuffer(int size);
void destroyRingBuffer(RingBuffer *ringbuffer);

#endif
