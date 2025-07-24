#pragma once
#include <stdint.h>

void build_header(uint8_t* header_out);
void build_report_frame(uint16_t person_count, uint8_t* frame_out);
void build_alarm_frame(uint8_t event_type, uint8_t* frame_out);
