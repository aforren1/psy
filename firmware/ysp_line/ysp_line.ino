// ysp_line.ino - a photodiode (or any edge input) on the ysp line protocol v1.
//
// The reference board for ysp/box.h's YBOX_LINE and ysp/device.h: it stamps
// each edge of one input with its own microsecond counter and sends the
// stamp, so the host times the edge by the board's interrupt, not by USB
// arrival. Board-neutral Arduino code: built for the Teensy 4.x first
// (native USB at 480 Mbit/s, so no FTDI latency timer) and for the
// Raspberry Pi Pico (RP2040, earlephilhower core). Any board with
// micros(), attachInterrupt() and a serial port works.
//
// Wiring, digital mode (the default): a photodiode module with a
// comparator (a digital out) on YSP_PIN; light gives HIGH unless
// YSP_INVERT is 1. Analog mode (YSP_ANALOG 1): a photodiode and resistor
// divider on YSP_PIN, read with analogRead(); the edge is the sample that
// crosses YSP_HI going up or YSP_LO going down (hysteresis), stamped at
// that sample, so the stamp is late by up to one sample period.
//
// The protocol (ysp/box.h, THE YSP LINE PROTOCOL v1), board to host:
//   S <t>             every 100 ms: the counter now
//   E <t> <ch> <0|1>  an edge on channel YSP_CHANNEL: 1 light, 0 dark
//   Q <seq> <t>       the answer to "q <seq>": the counter when the
//                     query's newline was read
//   I ysp-line 1 <board> photodiode   the answer to "i"
// <t> is micros(), 32 bits, wrapping every 71.6 minutes; the host unwraps.
//
// Public domain / MIT-0, as the rest of ysp.

#ifndef YSP_PIN
#define YSP_PIN 2
#endif
#ifndef YSP_CHANNEL
#define YSP_CHANNEL 1
#endif
#ifndef YSP_INVERT
#define YSP_INVERT 0
#endif
#ifndef YSP_ANALOG
#define YSP_ANALOG 0
#endif
#ifndef YSP_HI
#define YSP_HI 600      // analog mode: counts of a 10-bit read
#endif
#ifndef YSP_LO
#define YSP_LO 400
#endif

#if defined(TEENSYDUINO)
static const char* const kBoard = "teensy";
#elif defined(ARDUINO_ARCH_RP2040)
static const char* const kBoard = "rp2040";
#else
static const char* const kBoard = "arduino";
#endif

// Edges from the interrupt, drained by loop(): a power-of-two ring, one
// writer (the interrupt) and one reader (loop), so no lock is needed
// beyond reading the head once.
static const uint8_t kRing = 64;
static volatile uint32_t ring_t[kRing];
static volatile uint8_t ring_l[kRing];
static volatile uint8_t ring_head = 0;
static uint8_t ring_tail = 0;
static volatile uint32_t lost = 0;

static void push_edge(uint32_t t, uint8_t level) {
  uint8_t h = ring_head;
  if ((uint8_t)(h - ring_tail) >= kRing) { lost++; return; }
  ring_t[h % kRing] = t;
  ring_l[h % kRing] = level;
  ring_head = (uint8_t)(h + 1);
}

#if !YSP_ANALOG
// The stamp comes first: everything after it adds to the board's own delay.
static void on_edge() {
  uint32_t t = micros();
  uint8_t level = (uint8_t)(digitalRead(YSP_PIN) ? 1 : 0);
  push_edge(t, (uint8_t)(level ^ (YSP_INVERT ? 1 : 0)));
}
#else
static uint8_t analog_level = 0;
#endif

static char line[32];
static uint8_t nline = 0;
static uint32_t next_sync = 0;

// Teensy's USB serial sends when its buffer fills or a timer runs out;
// send_now() sends at once. Other cores send on flush().
static void send_now() {
#if defined(TEENSYDUINO)
  Serial.send_now();
#else
  Serial.flush();
#endif
}

static void handle_line(uint32_t t_read) {
  line[nline] = '\0';
  if (line[0] == 'q' && line[1] == ' ') {
    Serial.print("Q ");
    Serial.print(line + 2);
    Serial.print(' ');
    Serial.println(t_read);
  } else if (line[0] == 'i' && line[1] == '\0') {
    Serial.print("I ysp-line 1 ");
    Serial.print(kBoard);
    Serial.println(" photodiode");
  }
  send_now();
}

void setup() {
  Serial.begin(115200);      // USB CDC ignores the rate; a UART board uses it
#if !YSP_ANALOG
  pinMode(YSP_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(YSP_PIN), on_edge, CHANGE);
#else
  analogReadResolution(10);
#endif
  next_sync = micros();
}

void loop() {
#if YSP_ANALOG
  {
    int v = analogRead(YSP_PIN);
    uint32_t t = micros();
    uint8_t lv = analog_level ? (uint8_t)(v > YSP_LO) : (uint8_t)(v >= YSP_HI);
    if (lv != analog_level) {
      analog_level = lv;
      push_edge(t, (uint8_t)(lv ^ (YSP_INVERT ? 1 : 0)));
    }
  }
#endif
  // edges, oldest first
  {
    bool sent = false;
    while (ring_tail != ring_head) {
      uint32_t t = ring_t[ring_tail % kRing];
      uint8_t l = ring_l[ring_tail % kRing];
      ring_tail++;
      Serial.print("E ");
      Serial.print(t);
      Serial.print(' ');
      Serial.print(YSP_CHANNEL);
      Serial.print(' ');
      Serial.println(l);
      sent = true;
    }
    if (sent) send_now();
  }
  // the sync
  if ((int32_t)(micros() - next_sync) >= 0) {
    uint32_t t = micros();
    next_sync += 100000u;
    Serial.print("S ");
    Serial.println(t);
    send_now();
  }
  // queries from the host: the stamp is taken when the newline is read
  while (Serial.available() > 0) {
    int c = Serial.read();
    uint32_t t = micros();
    if (c == '\r') continue;
    if (c == '\n') { handle_line(t); nline = 0; continue; }
    if (nline < sizeof line - 1) line[nline++] = (char)c;
  }
}
