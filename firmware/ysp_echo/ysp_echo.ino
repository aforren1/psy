// ysp_echo.ino - an echo board with its own timestamps, for serial latency tests.
//
// tests/compare/serial_bench/ uses this board to split a host round trip
// into its two one-way parts and to give a device-side truth for "when was
// this line sent". The board stamps each command when it reads the command's
// newline and stamps each answer just before it hands the answer to USB; a
// clock fit on the host (BRACKET pairs, ysp/rt.h) maps those stamps to the
// host clock. A separate sketch and not a command in firmware/ysp_line/,
// because ysp_line is a photodiode board that sends a sync every 100 ms, and
// unsolicited lines in the middle of a timed echo would break the fixed
// answer lengths the bench relies on.
//
// Built for the Teensy 4.0 and 4.1 (native USB at 480 Mbit/s, 125 us
// microframes) and the Raspberry Pi Pico (RP2040, earlephilhower core, native
// USB at 12 Mbit/s, 1 ms frames). Neither has a USB-serial bridge chip, so
// neither has an FTDI latency timer. Any board with micros() and a serial
// port works; on a board with a bridge chip the bridge adds its own latency.
//
// Host to board, ASCII lines ending in '\n' ('\r' is ignored):
//   e <seq>        echo: answer "R <seq> <t_rx> <t_tx>"
//   q <seq>        ysp line v1 timer query: answer "Q <seq> <t_rx>"
//   s <hz> <n>     stream n lines "A <t> 1 <k>", k = 1..n, at hz lines per
//                  second (1 to 10000); a new "s" replaces a running stream
//   x              stop the stream
//   i              answer "I ysp-line 1 <board> echo"
// Anything else is ignored. Commands are read while a stream runs, so a host
// can send "q" probes during a stream.
//
// Board to host. Every number is zero-padded to 10 digits, so every line of
// a kind has one length and a host can read an exact byte count:
//   R <seq> <t_rx> <t_tx>   35 bytes with the '\n'
//   Q <seq> <t_rx>          24 bytes
//   A <t> 1 <k>             26 bytes
// t_rx is micros() when the command's newline was read from the USB buffer.
// t_tx and the t of an A line are micros() taken before the line is
// formatted and written; the formatting and the copy to the USB buffer come
// after the stamp. micros() is 32 bits and wraps every 71.6 minutes; the
// host unwraps. Q and A lines are ysp line protocol v1 (ysp/box.h, which
// accepts the leading zeros); R is not, and a YBOX_LINE decoder counts it as
// garbage.
//
// Public domain / MIT-0, as the rest of ysp.

#if defined(TEENSYDUINO)
static const char* const kBoard = "teensy";
#elif defined(ARDUINO_ARCH_RP2040)
static const char* const kBoard = "rp2040";
#else
static const char* const kBoard = "arduino";
#endif

static char line[32];
static uint8_t nline = 0;
static bool overlong = false;

static bool streaming = false;
static uint32_t stream_period = 1000;
static uint32_t stream_next = 0;
static uint32_t stream_k = 0;
static uint32_t stream_n = 0;

// Teensy's USB serial sends when its buffer fills or a timer runs out;
// send_now() sends at once. Other cores send on flush().
static void send_now() {
#if defined(TEENSYDUINO)
  Serial.send_now();
#else
  Serial.flush();
#endif
}

// Ten decimal digits, zero-padded: a uint32 never needs more.
static char* put10(char* p, uint32_t v) {
  for (int i = 9; i >= 0; i--) {
    p[i] = (char)('0' + v % 10u);
    v /= 10u;
  }
  return p + 10;
}

// Parses an unsigned decimal at *p after spaces; false if there is none.
static bool get_u32(const char** p, uint32_t* out) {
  const char* s = *p;
  uint32_t v = 0;
  int digits = 0;
  while (*s == ' ') s++;
  while (*s >= '0' && *s <= '9') {
    v = v * 10u + (uint32_t)(*s - '0');
    s++;
    digits++;
  }
  if (digits == 0 || digits > 10) return false;
  *p = s;
  *out = v;
  return true;
}

static void handle_line(uint32_t t_rx) {
  const char* p = line + 1;
  uint32_t a, b;
  char out[40];
  char* o = out;
  if (line[0] == 'e' && get_u32(&p, &a)) {
    uint32_t t_tx = micros();
    *o++ = 'R'; *o++ = ' ';
    o = put10(o, a); *o++ = ' ';
    o = put10(o, t_rx); *o++ = ' ';
    o = put10(o, t_tx); *o++ = '\n';
    Serial.write((const uint8_t*)out, (size_t)(o - out));
    send_now();
  } else if (line[0] == 'q' && get_u32(&p, &a)) {
    *o++ = 'Q'; *o++ = ' ';
    o = put10(o, a); *o++ = ' ';
    o = put10(o, t_rx); *o++ = '\n';
    Serial.write((const uint8_t*)out, (size_t)(o - out));
    send_now();
  } else if (line[0] == 's' && get_u32(&p, &a) && get_u32(&p, &b)) {
    if (a >= 1 && a <= 10000 && b >= 1) {
      stream_period = 1000000u / a;
      stream_n = b;
      stream_k = 0;
      stream_next = micros();
      streaming = true;
    }
  } else if (line[0] == 'x' && line[1] == '\0') {
    streaming = false;
  } else if (line[0] == 'i' && line[1] == '\0') {
    Serial.print("I ysp-line 1 ");
    Serial.print(kBoard);
    Serial.println(" echo");
    send_now();
  }
}

void setup() {
  Serial.begin(115200);      // USB CDC ignores the rate; a UART board uses it
}

void loop() {
  // commands: the stamp is taken when the newline is read
  while (Serial.available() > 0) {
    int c = Serial.read();
    uint32_t t = micros();
    if (c == '\r') continue;
    if (c == '\n') {
      line[nline] = '\0';
      if (!overlong) handle_line(t);
      nline = 0;
      overlong = false;
      continue;
    }
    if (nline < sizeof line - 1) line[nline++] = (char)c;
    else overlong = true;   // drop the whole line, not a truncated command
  }
  // the stream: a late line is sent at once and the schedule is kept, so
  // the k of each line says which slot it belongs to
  if (streaming && (int32_t)(micros() - stream_next) >= 0) {
    char out[32];
    char* o = out;
    uint32_t t = micros();
    stream_k++;
    stream_next += stream_period;
    *o++ = 'A'; *o++ = ' ';
    o = put10(o, t);
    *o++ = ' '; *o++ = '1'; *o++ = ' ';
    o = put10(o, stream_k); *o++ = '\n';
    Serial.write((const uint8_t*)out, (size_t)(o - out));
    send_now();
    if (stream_k >= stream_n) streaming = false;
  }
}
