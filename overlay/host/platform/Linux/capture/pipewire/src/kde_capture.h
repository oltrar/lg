/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
struct KDECapture;
struct KDECapture * lgKdeCapture_open(const char * connector);
uint32_t lgKdeCapture_node(struct KDECapture * capture);
bool lgKdeCapture_alive(struct KDECapture * capture);
void lgKdeCapture_close(struct KDECapture * capture);
int lgKdeCapture_list(void);
