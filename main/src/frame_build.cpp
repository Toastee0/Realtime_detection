#include <stdint.h>

void build_header(uint8_t* header_out) {
    const uint8_t FRAME_HEADER = 0xE1;
    const uint8_t MANUFACTURER = 0x01;
    const uint8_t LINE = 0x04;
    const uint8_t DEVICE = 0x01;

    header_out[0] = FRAME_HEADER;
    header_out[1] = (MANUFACTURER << 4) | LINE;
    header_out[2] = DEVICE;
}
void build_report_frame(uint16_t person_count, uint8_t* frame_out) {
    build_header(frame_out); // Llena los primeros 3 bytes

    frame_out[3] = 0x00; // SYNC_TYPE
    frame_out[4] = 0x00; // FLAG_INFO
    frame_out[5] = 0x40; // DATA_COUNTER
    frame_out[6] = 0x01; // reportType: personas
    frame_out[7] = (person_count >> 8) & 0xFF;
    frame_out[8] = person_count & 0xFF;
}
void build_alarm_frame(uint8_t event_type, uint8_t* frame_out) {
    build_header(frame_out); // Llena los primeros 3 bytes

    frame_out[3] = 0x40; // EVENT_TYPE
    frame_out[4] = 0x00; // FLAG_INFO
    frame_out[5] = 0x21; // DATA_EVENT
    frame_out[6] = event_type; // 0x02 o 0x03
}
