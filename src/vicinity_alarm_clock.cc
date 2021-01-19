// Author: Simon Gohl
// Proximity Alarm Clock (Smart-Home Alarm Clock)
// Sounds alarm only if desired Bluetooth device is detected
// 
// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
//
// Utility of rpi-rgb-led matrix library (../ext/rgbmatrix)
// led-matrix library this depends on is GPL v2

#include "led-matrix.h"
#include "graphics.h"

#include <SFML/Audio.hpp>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <thread>
#include <mutex>

#define MUSIC_FILE_PATH "../audio/alarm_sound.wav"


using namespace rgb_matrix;

volatile bool interrupt_received = false;
volatile bool bluetooth_connected = false;
std::recursive_mutex recur_m;

static void InterruptHandler(int signo) {
  interrupt_received = true;
}

static int usage(const char *progname) {
  fprintf(stderr, "usage: %s [options]\n", progname);
  fprintf(stderr, "Reads text from stdin and displays it. "
          "Empty string: clear screen\n");
  fprintf(stderr, "Options:\n");
  rgb_matrix::PrintMatrixFlags(stderr);
  fprintf(stderr,
          "\t-d <time-format>  : Default '%%H:%%M'. See strftime()\n"
          "\t-b <brightness>   : Sets brightness percent. Default: 100.\n"
          "\t-x <x-origin>     : X-Origin of displaying text (Default: 0)\n"
          "\t-y <y-origin>     : Y-Origin of displaying text (Default: 0)\n"
          "\t-S <spacing>      : Spacing pixels between letters (Default: 0)\n"
          "\t-C <r,g,b>        : Color. Default 255,255,0\n"
          "\t-B <r,g,b>        : Background-Color. Default 0,0,0\n"
          );

  return 1;
}

static bool parseColor(Color *c, const char *str) {
  return sscanf(str, "%hhu,%hhu,%hhu", &c->r, &c->g, &c->b) == 3;
}

static bool FullSaturation(const Color &c) {
    return (c.r == 0 || c.r == 255)
        && (c.g == 0 || c.g == 255)
        && (c.b == 0 || c.b == 255);
}

// Recursive function to check for Bluetooth devices
// Break out of recursion if interrupt recieved
static void BT_scanner()
{
  recur_m.lock();
  inquiry_info *ii = NULL;
  int max_rsp, num_rsp;
  int dev_id, sock, len, flags;
  int i;
  char addr[19] = { 0 };
  char name[248] = { 0 };

  dev_id = hci_get_route(NULL);
  sock = hci_open_dev( dev_id );
  if (dev_id < 0 || sock < 0) {
      perror("opening socket");
      exit(1);
  }

  len  = 8;
  max_rsp = 255;
  flags = IREQ_CACHE_FLUSH;
  ii = (inquiry_info*)malloc(max_rsp * sizeof(inquiry_info));
    
  num_rsp = hci_inquiry(dev_id, len, max_rsp, NULL, &ii, flags);
  if( num_rsp < 0 ) perror("hci_inquiry");

  for (i = 0; i < num_rsp; i++) {
      ba2str(&(ii+i)->bdaddr, addr);
      memset(name, 0, sizeof(name));

      if (hci_read_remote_name(sock, &(ii+i)->bdaddr, sizeof(name), 
          name, 0) < 0) {
            strcpy(name, "[unknown]");
      }
      printf("%s  %s\n", addr, name);
        
      if (strcmp(addr, "Simon's phone")) {
        bluetooth_connected = true;  
      } else { 
        bluetooth_connected = false;
      }
  }
  if(num_rsp == 0) {
    bluetooth_connected = false;
  }
    
  free( ii );
  close( sock );
  recur_m.unlock();
  if (!interrupt_received) {
    BT_scanner();
  }
}

int main(int argc, char *argv[]) {
  RGBMatrix::Options matrix_options;
  rgb_matrix::RuntimeOptions runtime_opt;
  if (!rgb_matrix::ParseOptionsFromFlags(&argc, &argv,
                                         &matrix_options, &runtime_opt)) {
    return usage(argv[0]);
  }

  const char *time_format = "%H:%M";
  Color color(200, 0, 0);
  Color bg_alarm(0, 100, 100);
  Color bg_black(0, 0, 0); 

  
  const char *bdf_font_file = strdup("../ext/rgbmatrix/fonts/8x13.bdf");
  const char *bdf_font_file2 = strdup("../ext/rgbmatrix/fonts/5x7.bdf");
  int x_orig = 0;
  int y_orig = 0;
  int brightness = 100;
  int letter_spacing = 0;

  int opt;
  while ((opt = getopt(argc, argv, "x:y:C:B:b:S:d:")) != -1) {
    switch (opt) {
    case 'd': time_format = strdup(optarg); break;
    case 'b': brightness = atoi(optarg); break;
    case 'x': x_orig = atoi(optarg); break;
    case 'y': y_orig = atoi(optarg); break;
    case 'S': letter_spacing = atoi(optarg); break;
    case 'C':
      if (!parseColor(&color, optarg)) {
        fprintf(stderr, "Invalid color spec: %s\n", optarg);
        return usage(argv[0]);
      }
      break;
    case 'B':
      if (!parseColor(&bg_black, optarg)) {
        fprintf(stderr, "Invalid background color spec: %s\n", optarg);
        return usage(argv[0]);
      }
      break;
    default:
      return usage(argv[0]);
    }
  }

  /*
   * Load font. This needs to be a filename with a bdf bitmap font.
   */
  rgb_matrix::Font font;
  if (!font.LoadFont(bdf_font_file)) {
    fprintf(stderr, "Couldn't load font '%s'\n", bdf_font_file);
    return 1;
  }
  rgb_matrix::Font font2;
  if (!font2.LoadFont(bdf_font_file2)) {
    fprintf(stderr, "Couldn't load font '%s'\n", bdf_font_file2);
    return 1;
  }

  if (brightness < 1 || brightness > 100) {
    fprintf(stderr, "Brightness is outside usable range.\n");
    return 1;
  }

  RGBMatrix *matrix = rgb_matrix::CreateMatrixFromOptions(matrix_options,
                                                          runtime_opt);
  if (matrix == NULL) {
    return 1;
  }
  
  matrix->SetBrightness(brightness);

  const bool all_extreme_colors = (brightness == 100)
      && FullSaturation(color)
      && FullSaturation(bg_alarm)
      && FullSaturation(bg_black);
  if (all_extreme_colors) {
      matrix->SetPWMBits(1);
  }
  
  int x = x_orig;
  int y = y_orig;

  FrameCanvas *offscreen = matrix->CreateFrameCanvas();

  char text_buffer[256];
  struct timespec next_time;
  next_time.tv_sec = time(NULL);
  next_time.tv_nsec = 0;
  struct tm tm;
  
  sf::Music music;
  music.openFromFile(MUSIC_FILE_PATH);
  music.setVolume(50);
  
  std::thread t1(BT_scanner);
  
  bool alarm_active = false;
  bool alarm_sounding = false;
  bool alarm_bg_blinker = false;
  
  char alarm_time_buffer[100];
  const char* alarm_time = "10:49";
  strcpy(alarm_time_buffer, "Alarm: ");
  strcat(alarm_time_buffer, alarm_time);

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);
  
  while (!interrupt_received) {
      localtime_r(&next_time.tv_sec, &tm);
      strftime(text_buffer, sizeof(text_buffer), time_format, &tm);
      // Make background black to clear residual pixel colors
      offscreen->Fill(bg_black.r, bg_black.g, bg_black.b);
      
      if (strncmp(text_buffer, alarm_time, 5) == 0 && bluetooth_connected) {
        alarm_active = true;
      } else { 
        // TODO: Turn alarm off with keyboard input
        // For now, turn off alarm with turning off Bluetooth on device
        alarm_active = false;  
      }
      
      if (alarm_active) {
        if(!alarm_sounding) {
          music.play();
          alarm_sounding = true;
        }
        alarm_bg_blinker = !alarm_bg_blinker;
        if (alarm_bg_blinker) {
          offscreen->Fill(bg_alarm.r, bg_alarm.g, bg_alarm.b);
        } 
      } else { 
          music.stop();
          alarm_sounding = false;
      } 
      
      // Display current time
      rgb_matrix::DrawText(offscreen, font, x, y + font.baseline(),
                           color, NULL, text_buffer,
                           letter_spacing);
      
      // Display Bluetooth connected and alarm time
      if (bluetooth_connected) {
        const char* BT_line = "BT connected";
        rgb_matrix::DrawText(offscreen, font2, x, y + font.height() - 2
                            + font.baseline(), color, NULL, BT_line, 
                            letter_spacing);
        rgb_matrix::DrawText(offscreen, font2, x, y + font.height()
                            + font2.height() + font.baseline(), 
                            color, NULL, alarm_time_buffer, letter_spacing);
      }

      // Wait until we're ready to show it.
      clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &next_time, NULL);

      // Atomic swap with double buffer
      offscreen = matrix->SwapOnVSync(offscreen);
      
      next_time.tv_sec += 1;
  }
  
  // Just in case interrupt received before bluetooth turned off
  // Hence, music was never stopped 
  music.stop();
  
  t1.join();
  
  // Finished. Shut down the RGB matrix.
  matrix->Clear();
  delete matrix;

  write(STDOUT_FILENO, "\n", 1);  // Create a fresh new line after ^C on screen
  return 0;
}
