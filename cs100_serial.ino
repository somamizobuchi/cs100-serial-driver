const int DATA_PIN    = 4;  // CS-100 pin 8 (open-collector, internal pull-up)
const int CLOCK_PIN   = 2;  // CS-100 pin 10
const int TRIGGER_PIN = 3;  // CS-100 pin 11 (measure request, active high)

const unsigned long TRIGGER_PULSE_MS = 100;
const unsigned long START_TIMEOUT_MS = 5000;    // trigger to first clock edge
const unsigned long QUIET_TIMEOUT_US = 100000;  // clock gap that ends a frame

// Frame: 16 words of 16 bits, each an 8-bit header then 8 segment bits
const int FRAME_WORDS = 16;
const int FRAME_BITS  = FRAME_WORDS * 16;
volatile byte frameBits[FRAME_BITS / 8];
volatile int nBits = 0;
volatile unsigned long lastEdge = 0;

void onClock() {
  bool d = digitalRead(DATA_PIN);
  lastEdge = micros();
  if (nBits < FRAME_BITS) {
    if (d) frameBits[nBits >> 3] |= (1 << (nBits & 7));
    nBits++;
  }
}

bool getBit(int i) {
  return frameBits[i >> 3] & (1 << (i & 7));
}

// Segment bits after inversion: dp c b (a/d) (d/a) e g f
char segToChar(byte seg) {
  switch (seg & 0x7F) {
    case 0x7D: return '0';
    case 0x60: return '1';
    case 0x3E: return '2';
    case 0x7A: return '3';
    case 0x63: return '4';
    case 0x5B: return '5';
    case 0x5F: return '6';
    case 0x70: case 0x68: case 0x71: case 0x69: return '7';
    case 0x7F: return '8';
    case 0x7B: case 0x73: case 0x6B: return '9';
    case 0x02: return '-';
    case 0x00: return ' ';
    default:   return '?';
  }
}

void printHexByte(byte b) {
  if (b < 16) Serial.print("0");
  Serial.print(b, HEX);
}

// Pulse the trigger and wait for a full frame. On failure, got is the number of bits received.
bool captureFrame(int &got) {
  noInterrupts();
  memset((void *)frameBits, 0, sizeof(frameBits));
  nBits = 0;
  interrupts();

  digitalWrite(TRIGGER_PIN, HIGH);
  delay(TRIGGER_PULSE_MS);
  digitalWrite(TRIGGER_PIN, LOW);

  unsigned long start = millis();
  while (true) {
    noInterrupts();
    got = nBits;
    unsigned long last = lastEdge;
    interrupts();

    if (got >= FRAME_BITS) return true;
    if (got == 0 && millis() - start > START_TIMEOUT_MS) return false;
    if (got > 0 && micros() - last > QUIET_TIMEOUT_US) return false;
  }
}

void unpackFrame(byte *header, byte *seg) {
  for (int w = 0; w < FRAME_WORDS; w++) {
    header[w] = 0;
    seg[w] = 0;
    for (int k = 0; k < 8; k++) {
      header[w] = (header[w] << 1) | (getBit(w * 16 + k) ? 1 : 0);
      seg[w]    = (seg[w] << 1)    | (getBit(w * 16 + 8 + k) ? 0 : 1);
    }
  }
}

void printRawFrame(byte *header, byte *seg) {
  for (int w = 0; w < FRAME_WORDS; w++) {
    printHexByte(header[w]);
    printHexByte(seg[w]);
  }
}

// Words 1-4: Y, words 6-8: x, words 10-12: y, words 13-15: one extra digit each
bool formatReading(byte *seg, char *yStr, char *xStr, char *yChromaStr) {
  char c[FRAME_WORDS];
  for (int w = 0; w < FRAME_WORDS; w++) c[w] = segToChar(seg[w]);

  int n = 0;
  int yDecimals = -1;
  for (int i = 0; i < 4; i++) {
    if (c[i] == ' ') continue;
    if (!isDigit(c[i])) return false;
    yStr[n++] = c[i];
    if (seg[i] & 0x80) {
      yStr[n++] = '.';
      yDecimals = 3 - i;
    }
  }
  if (n == 0) return false;
  // Y has at most two decimals, so only extend it when the display shows fewer
  if (yDecimals == 1 || yDecimals == -1) {
    if (!isDigit(c[12])) return false;
    if (yDecimals == -1) yStr[n++] = '.';
    yStr[n++] = c[12];
  }
  yStr[n] = 0;

  for (int i = 0; i < 3; i++) {
    if (!isDigit(c[5 + i]) || !isDigit(c[9 + i])) return false;
    xStr[2 + i] = c[5 + i];
    yChromaStr[2 + i] = c[9 + i];
  }
  if (!isDigit(c[13]) || !isDigit(c[14])) return false;
  xStr[0] = yChromaStr[0] = '0';
  xStr[1] = yChromaStr[1] = '.';
  xStr[5] = c[13];
  yChromaStr[5] = c[14];
  xStr[6] = yChromaStr[6] = 0;
  return true;
}

void measure(bool raw) {
  int got;
  if (!captureFrame(got)) {
    if (got == 0) {
      Serial.println("ERR,TIMEOUT");
    } else {
      Serial.print("ERR,FRAME,");
      Serial.println(got);
    }
    return;
  }

  byte header[FRAME_WORDS];
  byte seg[FRAME_WORDS];
  unpackFrame(header, seg);

  if (raw) {
    Serial.print("OK,");
    printRawFrame(header, seg);
    Serial.println();
    return;
  }

  char yStr[8];
  char xStr[8];
  char yChromaStr[8];
  if (!formatReading(seg, yStr, xStr, yChromaStr)) {
    Serial.print("ERR,DIGIT,");
    printRawFrame(header, seg);
    Serial.println();
    return;
  }

  Serial.print("OK,");
  Serial.print(yStr);
  Serial.print(",");
  Serial.print(xStr);
  Serial.print(",");
  Serial.println(yChromaStr);
}

void handleCommand(const char *cmd) {
  if (cmd[0] == 0) return;
  if (cmd[1] != 0) {
    Serial.println("ERR,CMD");
    return;
  }

  switch (toupper(cmd[0])) {
    case 'M': measure(false); break;
    case 'R': measure(true); break;
    case '?': Serial.println("OK,CS100"); break;    default:  Serial.println("ERR,CMD"); break;
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  pinMode(DATA_PIN, INPUT_PULLUP);
  pinMode(CLOCK_PIN, INPUT);
  pinMode(TRIGGER_PIN, OUTPUT);
  digitalWrite(TRIGGER_PIN, LOW);
  attachInterrupt(digitalPinToInterrupt(CLOCK_PIN), onClock, RISING);

  Serial.println("READY");
}

void loop() {
  static char cmd[8];
  static byte len = 0;

  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r') continue;
    if (ch != '\n') {
      if (len < sizeof(cmd) - 1) cmd[len++] = ch;
      continue;
    }
    cmd[len] = 0;
    len = 0;
    handleCommand(cmd);
  }
}
