#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <LedControl.h>
#include <string.h>
#include <STM32RTC.h>
#include <DFRobotDFPlayerMini.h>

/* ================= BUTTON STRUCT ================= */
struct ButtonState {
  bool last;
  unsigned long lastTime;
};

ButtonState btnSet  = {true, 0};
ButtonState btnUp   = {true, 0};
ButtonState btnDown = {true, 0};

/* ================= HARDWARE ================= */
#define BTN_SET   PB14
#define BTN_UP    PB13
#define BTN_DOWN  PB12

#define EEPROM_ADDR 0x50
#define MAX_BELLS 20
#define TT_NAME_LEN 12
#define EEPROM_BASE 0


#define EEPROM_TTCOUNT_L   0
#define EEPROM_TTCOUNT_H   1
#define EEPROM_DEFAULT_TT  2
#define EEPROM_TT_START    3


/* ---------- 8x8 MAX7219 ---------- */
#define MATRIX_DIN PA7
#define MATRIX_CLK PA5
#define MATRIX_CS  PA4

#define BUZZER_PIN PA2
#define RELAY_PIN  PA1



/* -------- DFPLAYER -------- */
#define MP3_BUSY_PIN PB1
HardwareSerial mp3Serial(PA10, PA9);   // RX, TX

/////////////////////// Start Exam timer //////////////
bool examRunning = false;
bool examScheduled = false;
uint32_t examStartSec = 0;
uint32_t examDurationSec = 0;


//examScheduled = true;


uint8_t examStartH = 0;
uint8_t examStartM = 0;

uint8_t examDurH = 3;   // default 3 hours
uint8_t examDurM = 0;
uint16_t selectedTimetableIndex = 0;

//////////////////////End Exam Timer 
/* ================= UI ================= */
enum UiState {
  UI_DASHBOARD,
  UI_MENU,
  UI_SET_DATETIME,
  UI_SET_BELL,
  UI_MANAGE_Timetable,
  UI_CLEAR_EEPROM,
  UI_TRIGGER_BELL,
  UI_EXAM_TIMER,
  UI_AUTO_BELLS,
  UI_SELFTEST
};

UiState uiState = UI_DASHBOARD;
bool screenChanged = true;


//HardwareSerial &mp3Serial = Serial1;
DFRobotDFPlayerMini mp3;

/* ================= BELL TYPE ================= */
enum BellType : uint8_t
{
  BELL_PERIOD = 0,
  BELL_PRAYER = 1,
  BELL_RECESS = 2,
  BELL_END    = 3
};

LedControl matrix = LedControl(MATRIX_DIN, MATRIX_CLK, MATRIX_CS, 1);
LiquidCrystal_I2C lcd(0x27, 20, 4);

STM32RTC& rtc = STM32RTC::getInstance();

/* ================= DATA ================= */
uint8_t bellCount = 0;
uint8_t bellHour[MAX_BELLS];
uint8_t bellMin[MAX_BELLS];
uint8_t bellType[MAX_BELLS];


bool ttInfoEmbedded = false;
bool bellActive = false;

/* ---- mp3 runtime ---- */
bool mp3Playing = false;
int  activeBellIndex = -1;

/* ================= Time Table  ================= */
void readTimetableName(uint16_t index, char *out)
{
  if (index >= getTimetableCount())
  {
    out[0] = 0;
    return;
  }

  uint16_t addr = getTimetableAddress(index);

  for (uint8_t i = 0; i < TT_NAME_LEN; i++)
    out[i] = eepromReadByte(addr + i);

  out[TT_NAME_LEN] = 0;
}







/* ================= Time Table  ================= */
bool deleteTimetable(uint16_t index)
{
  uint16_t total = getTimetableCount();
  if (total == 0 || index >= total) return false;

  uint16_t addrDel  = getTimetableAddress(index);
  uint16_t addrNext = getTimetableAddress(index + 1);
  uint16_t addrEnd  = getTimetableAddress(total);

  // Shift EEPROM data to close the gap
  for (uint16_t a = addrNext; a < addrEnd; a++)
  {
    uint8_t v = eepromReadByte(a);
    eepromWriteByte(addrDel++, v);
  }

  setTimetableCount(total - 1);

  uint8_t def = getDefaultTimetable();
  if (def == index) 
  {
      setDefaultTimetable(0); // Reset to first if current was deleted
  } 
  else if (def > index) 
  {
      setDefaultTimetable(def - 1); // Shift index down
  }

  // Reload the current default into RAM so the bells update immediately
  loadTimetable(getDefaultTimetable()); 
  
  return true;
}

/* ================= Time Table  ================= */
uint8_t getTimetableBellCount(uint16_t index)
{
  uint16_t addr = getTimetableAddress(index);
  addr += TT_NAME_LEN;
  return eepromReadByte(addr);
}
/* ================= Time Table  ================= */
void readTimetableBell(uint16_t ttIndex,
                        uint8_t bellIndex,
                        uint8_t &h,
                        uint8_t &m,
                        uint8_t &t)
{
  uint16_t addr = getTimetableAddress(ttIndex);

  addr += TT_NAME_LEN;           // skip name
  uint8_t cnt = eepromReadByte(addr++);
  if (bellIndex >= cnt) return;

  addr += bellIndex * 3;

  h = eepromReadByte(addr++);
  m = eepromReadByte(addr++);
  t = eepromReadByte(addr++);
}
/* ================= Time Table  ================= */
void screenTimetableInfo(uint16_t ttIndex)
{
  static uint8_t top = 0;

  if (screenChanged)
  {
    top = 0;
    lcd.clear();
    lcd.noBlink();
    screenChanged = false;
  }

  uint8_t total = getTimetableBellCount(ttIndex);

  char name[TT_NAME_LEN+1];
  readTimetableName(ttIndex, name);

  char line[21];

  // row 0 – name
  snprintf(line, sizeof(line), "TT: %s", name);
  lcdPrintRow(0, line);

  // rows 1 & 2 – two bells per page
  for (uint8_t r = 0; r < 2; r++)
  {
    uint8_t i = top + r;

    if (i < total)
    {
      uint8_t h,m,t;
      readTimetableBell(ttIndex, i, h, m, t);

      char c;
      if      (t == BELL_PERIOD) c = 'C';
      else if (t == BELL_PRAYER) c = 'P';
      else if (t == BELL_RECESS) c = 'R';
      else if (t == BELL_END)    c = 'E';
      else                       c = '?';

      snprintf(line, sizeof(line),
               "B%02d %02d:%02d %c",
               i+1, h, m, c);

      lcdPrintRow(1+r, line);
    }
    else
      lcdPrintRow(1+r, "");
  }

  lcdPrintRow(3, "UP/DN SCROLL SET");

  if (readButton(BTN_DOWN, btnDown))
  {
    if (top + 2 < total) top++;
  }

  if (readButton(BTN_UP, btnUp))
  {
    if (top > 0) top--;
  }

  if (readButton(BTN_SET, btnSet))
  {
    uiState = UI_MENU;   // or back to list (see note below)
    screenChanged = true;
  }
}
/* ================= Time Table  ================= */
/* ================= Time Table  ================= */
/* ================= Time Table  ================= */
/* ================= Time Table  ================= */
/* ================= Time Table  ================= */
/* ================= Time Table  ================= */
/* ================= Time Table  ================= */
/* ================= Time Table  ================= */

void eepromWriteBlock(uint16_t addr, const uint8_t *buf, uint16_t len)
{
  for (uint16_t i = 0; i < len; i++)
    eepromWriteByte(addr + i, buf[i]);
}

void eepromReadBlock(uint16_t addr, uint8_t *buf, uint16_t len)
{
  for (uint16_t i = 0; i < len; i++)
    buf[i] = eepromReadByte(addr + i);
}

uint16_t getTimetableCount()
{
  uint16_t v  = eepromReadByte(EEPROM_TTCOUNT_L);
  v |= (uint16_t)eepromReadByte(EEPROM_TTCOUNT_H) << 8;
  return v;
}

void setTimetableCount(uint16_t c)
{
  eepromWriteByte(EEPROM_TTCOUNT_L, c & 0xFF);
  eepromWriteByte(EEPROM_TTCOUNT_H, (c >> 8) & 0xFF);
}

uint8_t getDefaultTimetable()
{
  return eepromReadByte(EEPROM_DEFAULT_TT);
}

void setDefaultTimetable(uint8_t index)
{
  eepromWriteByte(EEPROM_DEFAULT_TT, index);
}



uint16_t getTimetableAddress(uint16_t index)
{
  uint16_t addr = EEPROM_TT_START;

  uint16_t total = getTimetableCount();
  if (index >= total) index = total;

  for (uint16_t i = 0; i < index; i++)
  {
    addr += TT_NAME_LEN;

    uint8_t cnt = eepromReadByte(addr);

    // ---------- HARD SAFETY ----------
    if (cnt > MAX_BELLS)
      cnt = 0;
    // --------------------------------

    addr += 1;
    addr += cnt * 3;
  }

  return addr;
}


const char *monthName(uint8_t m)
{
  static const char *names[] =
  {"JAN","FEB","MAR","APR","MAY","JUN",
   "JUL","AUG","SEP","OCT","NOV","DEC"};

  if (m < 1 || m > 12) return "UNK";
  return names[m-1];
}

uint8_t countMonthTimetables(const char *base)
{
  uint16_t n = getTimetableCount();
  uint8_t cnt = 0;

  for(uint16_t i=0;i<n;i++)
  {
    char name[TT_NAME_LEN+1];

    uint16_t addr = getTimetableAddress(i);

    for(uint8_t j=0;j<TT_NAME_LEN;j++)
      name[j] = eepromReadByte(addr+j);

    name[TT_NAME_LEN] = 0;

    if (strncmp(name, base, 3) == 0)
      cnt++;
  }
  return cnt;
}
void makeAutoTimetableName(char *out)
{
  uint16_t cnt = getTimetableCount();

  // first ever timetable
  if (cnt == 0)
  {
    strcpy(out, "DEFAULT");
    return;
  }

  uint8_t m = rtc.getMonth();
  const char *base = monthName(m);

  uint8_t already = countMonthTimetables(base);

  if (already == 0)
  {
    strcpy(out, base);
  }
  else
  {
    snprintf(out, TT_NAME_LEN+1, "%s_%d", base, already+1);
  }
}

bool saveCurrentTimetable(const char *name)
{
  uint16_t ttCount = getTimetableCount();

  uint16_t addr = getTimetableAddress(ttCount);

  // name
  for (uint8_t i = 0; i < TT_NAME_LEN; i++)
  {
    uint8_t c = 0;
    if (i < strlen(name)) c = name[i];
    eepromWriteByte(addr++, c);
  }

  // bell count
  eepromWriteByte(addr++, bellCount);

  // bells
  for (uint8_t i = 0; i < bellCount; i++)
  {
    eepromWriteByte(addr++, bellHour[i]);
    eepromWriteByte(addr++, bellMin[i]);
    eepromWriteByte(addr++, bellType[i]);
  }

  setTimetableCount(ttCount + 1);

  // only first timetable becomes default
  if (ttCount == 0)
  {
    setDefaultTimetable(0);
  }

  return true;
}






bool loadTimetable(uint16_t index)
{
  if (index >= getTimetableCount())
    return false;

  uint16_t addr = getTimetableAddress(index);

  // skip name
  addr += TT_NAME_LEN;

  uint8_t cnt = eepromReadByte(addr++);

  if (cnt > MAX_BELLS) return false;

  bellCount = cnt;

  for (uint8_t i = 0; i < bellCount; i++)
  {
    bellHour[i] = eepromReadByte(addr++);
    bellMin[i]  = eepromReadByte(addr++);
    bellType[i] = eepromReadByte(addr++);
  }

  return true;
}


/* ================= Time Table  ================= */


/* ================= FONT ================= */
// index mapping
// 0..9  -> digits 0..9
// 10    -> C
// 11    -> R
// 12    -> P
// 13    -> E

const byte font8x8[][8] =
{
/* 0 */
{0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},
/* 1 */
{0x18,0x38,0x18,0x18,0x18,0x18,0x3C,0x00},
/* 2 */
{0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00},
/* 3 */
{0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
/* 4 */
{0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00},
/* 5 */
{0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
/* 6 */
{0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00},
/* 7 */
{0x7E,0x66,0x06,0x0C,0x18,0x18,0x18,0x00},
/* 8 */
{0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
/* 9 */
{0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00},

/* 10 = C */
{0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},

/* 11 = R */
{0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00},

/* 12 = P */
{0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},

/* 13 = E */
{0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00}
};


/* ================= MATRIX ================= */
void drawChar8x8(byte index)
{
  matrix.clearDisplay(0);

  for (int row = 0; row < 8; row++)
  {
    // vertical flip
    byte line = font8x8[index][7 - row];

    for (int col = 0; col < 8; col++)
    {
      // horizontal flip
      bool on = line & (1 << col);

      matrix.setLed(0, row, col, on);
    }
  }
}



/* ================= MATRIX ================= */
/* ================= MATRIX ================= */
void showCountdownOnMatrix(long sec)
{
  if (sec < 0) {
    matrix.clearDisplay(0);
    return;
  }

  if (sec > 60) sec = 60;

  int cols = map(sec, 0, 60, 0, 8);

  matrix.clearDisplay(0);

  for (int c = 0; c < cols; c++) {
    for (int r = 0; r < 8; r++) {
      matrix.setLed(0, r, 7 - c, true);
    }
  }
}

/* ================= INDICATOR ================= */
void updateCountdownIndicators(long countdown)
{
  static unsigned long t = 0;
  static bool blink = false;

  if (bellCount == 0 || countdown < 0) {
    matrix.clearDisplay(0);
    return;
  }

  if (countdown <= 10 && countdown >= 1)
  {
    if (millis() - t > 200) {
      t = millis();
      blink = !blink;
    }

    if (blink) showCountdownOnMatrix(countdown);
    else matrix.clearDisplay(0);

    return;
  }

  if (countdown <= 60) {
    showCountdownOnMatrix(countdown);
    return;
  }

  matrix.clearDisplay(0);
}


/* ================= Time Table  ================= */
/* ================= Time Table  ================= */

void screenTimetableList() {
  static enum { L_LIST, L_INFO, L_CONFIRM } layer = L_LIST;
  static uint16_t sel = 0;
  static uint16_t top = 0;
  static uint8_t infoTop = 0;
  static uint8_t operation = 0; // 1=Delete, 2=Default
  static bool waitRelease = false;

  uint16_t total = getTimetableCount();

  // 1. Safety: Wait for button release
  if (waitRelease) {
    if (digitalRead(BTN_SET) == HIGH && digitalRead(BTN_UP) == HIGH && digitalRead(BTN_DOWN) == HIGH) {
      waitRelease = false;
    }
    return; 
  }

  // 2. Screen Reset
  if (screenChanged) {
    layer = L_LIST; sel = 0; top = 0;
    lcd.clear(); lcd.noBlink();
    screenChanged = false;
  }

  // 3. If EEPROM is empty
  if (total == 0) {
    lcdPrintRow(0, "NO TIMETABLES");
    lcdPrintRow(3, "SET = BACK");
    if (readButton(BTN_SET, btnSet)) { uiState = UI_MENU; screenChanged = true; }
    return;
  }

  char name[TT_NAME_LEN + 1];
  readTimetableName(sel, name);

  /* ================= LAYER: CONFIRMATION (YES/NO) ================= */
  if (layer == L_CONFIRM) {
    lcdPrintRow(0, (operation == 1) ? "DELETE TABLE?" : "SET AS DEFAULT?");
    lcdPrintRow(1, name);
    lcdPrintRow(3, "SET=YES   UP=NO");

    if (readButton(BTN_UP, btnUp)) { // User chose NO
      layer = L_INFO; 
      lcd.clear();
    }
    if (readButton(BTN_SET, btnSet)) { // User chose YES
      if (operation == 1) {
        deleteTimetable(sel);
        total = getTimetableCount();
        if (sel >= total && total > 0) sel = total - 1;
        layer = L_LIST;
      } else {
        setDefaultTimetable(sel);
        loadTimetable(sel);
        layer = L_INFO;
      }
      waitRelease = true; 
      lcd.clear();
    }
    return;
  }

  /* ================= LAYER: DETAIL INFO (Bell List) ================= */
  if (layer == L_INFO) {
    uint8_t bellTotal = getTimetableBellCount(sel);
    lcdPrintRow(0, name);

    for (uint8_t r = 0; r < 2; r++) {
      uint8_t i = infoTop + r;
      if (i < bellTotal) {
        uint8_t h, m, t; readTimetableBell(sel, i, h, m, t);
        
        // Convert Type Number to Text
        const char* typeText = "PERIOD";
        if (t == 1) typeText = "PRAYER";
        else if (t == 2) typeText = "RECESS";
        else if (t == 3) typeText = "END";

        char line[21]; 
        snprintf(line, sizeof(line), "%02d:%02d %s", h, m, typeText);
        lcdPrintRow(r + 1, line);
      } else lcdPrintRow(r + 1, "");
    }
    lcdPrintRow(3, "UP/DN=SCR SET=BACK");

    if (readButton(BTN_DOWN, btnDown) && infoTop + 1 < bellTotal) infoTop++;
    if (readButton(BTN_UP, btnUp) && infoTop > 0) infoTop--;

    // COMBOS for Delete/Default
    if (digitalRead(BTN_SET) == LOW) {
      if (digitalRead(BTN_UP) == LOW)   { operation = 2; layer = L_CONFIRM; waitRelease = true; lcd.clear(); return; }
      if (digitalRead(BTN_DOWN) == LOW) { operation = 1; layer = L_CONFIRM; waitRelease = true; lcd.clear(); return; }
    }

    if (readButton(BTN_SET, btnSet)) { layer = L_LIST; infoTop = 0; lcd.clear(); }
    return;
  }

  /* ================= LAYER: LIST VIEW (Main Selection) ================= */
  if (readButton(BTN_DOWN, btnDown) && sel + 1 < total) sel++;
  if (readButton(BTN_UP, btnUp)) {
    if (sel > 0) sel--;
    else { // If user presses UP at the top of the list, go back to Menu
       uiState = UI_MENU;
       screenChanged = true;
       return;
    }
  }

  if (sel < top) top = sel;
  if (sel >= top + 2) top = sel - 1;

  lcdPrintRow(0, "   SELECT TABLE");
  uint8_t defIdx = getDefaultTimetable();

  for (uint8_t r = 0; r < 2; r++) {
    uint16_t idx = top + r;
    if (idx < total) {
      char tName[TT_NAME_LEN + 1]; readTimetableName(idx, tName);
      char line[21];
      snprintf(line, sizeof(line), "%c %-12s %c", (idx == sel) ? '>' : ' ', tName, (idx == defIdx) ? '*' : ' ');
      lcdPrintRow(r + 1, line);
    } else lcdPrintRow(r + 1, "");
  }
  lcdPrintRow(3, "SET=OPEN  UP=EXIT");

  if (readButton(BTN_SET, btnSet)) { layer = L_INFO; lcd.clear(); }
}



/* ================= Time Table  ================= */
/* ================= Time Table  ================= */










///////////////////////
////////////////////////
/////////////////////////



/* ================= Time Table  ================= */
/* ================= EEPROM ================= */
void eepromWriteByte(uint16_t addr, uint8_t data) {
  Wire.beginTransmission(EEPROM_ADDR);
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)(addr & 0xFF));
  Wire.write(data);
  Wire.endTransmission();
  delay(10);
}

uint8_t eepromReadByte(uint16_t addr) {
  Wire.beginTransmission(EEPROM_ADDR);
  Wire.write(addr >> 8);
  Wire.write(addr & 0xFF);
  Wire.endTransmission();
  Wire.requestFrom(EEPROM_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}

/* ================= BUTTON ================= */
bool readButton(uint8_t pin, ButtonState &b)
{
  bool now = digitalRead(pin);

  if (now != b.last) {
    b.lastTime = millis();
    b.last = now;
  }

  if ((millis() - b.lastTime) > 40) {
    if (now == LOW) {
      static unsigned long lock[3] = {0,0,0};

      unsigned long *lk;
      if (pin == BTN_SET)      lk = &lock[0];
      else if (pin == BTN_UP)  lk = &lock[1];
      else                     lk = &lock[2];

      if (millis() - *lk > 300) {
        *lk = millis();
        return true;
      }
    }
  }
  return false;
}

/* ================= LCD ================= */
void lcdPrintRow(uint8_t row, const char *txt) {
  lcd.setCursor(0, row);
  lcd.print(txt);
  for (int i = strlen(txt); i < 20; i++) lcd.print(' ');
}

void showError(const char* line1, const char* line2) {
  lcd.clear();
  lcdPrintRow(1, line1);
  lcdPrintRow(2, line2);
  delay(1500);
}

/* ================= TIME / BELL ================= */
int timeToMinutes(uint8_t h, uint8_t m) {
  return h * 60 + m;
}

void getBellStatus(int &currentBell, int &nextBell, long &countdownSec) {

  currentBell = -1;
  nextBell = -1;
  countdownSec = -1;

  if (bellCount == 0) return;

  int nowMin = timeToMinutes(rtc.getHours(), rtc.getMinutes());
  int nowSec = rtc.getSeconds();

  for (int i = 0; i < bellCount; i++) {

    int bellTimeMin = timeToMinutes(bellHour[i], bellMin[i]);

    if (nowMin > bellTimeMin || (nowMin == bellTimeMin && nowSec > 0)) {
      currentBell = i;
    }
    else {
      nextBell = i;
      int deltaMin = bellTimeMin - nowMin;
      countdownSec = deltaMin * 60 - nowSec;
      return;
    }
  }

  currentBell = -1;
  nextBell    = -1;
  countdownSec = -1;
}

/* ================= BELL HANDLER ================= */
void handleBell()
{
  static unsigned long bellStart = 0;
  static bool timerStarted = false;

  if (bellActive && !timerStarted)
  {
      bellStart = millis();
      timerStarted = true;
  }

  if (bellActive && timerStarted)
  {
      if (millis() - bellStart >= 3000)
      {
          digitalWrite(RELAY_PIN, HIGH);
          digitalWrite(BUZZER_PIN, LOW);

          bellActive = false;
          timerStarted = false;
      }
  }
}



/* ================= I2C SCAN ================= */
void scanI2C() {
  for (byte a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      lcd.clear();
      lcdPrintRow(0, "I2C found:");
      lcd.setCursor(0,1);
      lcd.print(a, HEX);
      delay(300);
    }
  }
}

/* ================= DASHBOARD ================= */
char getBellSymbolChar(int index)
{
  if (index < 0 || index >= bellCount) return '-';

  if (bellType[index] == BELL_PRAYER) return 'P';
  if (bellType[index] == BELL_RECESS) return 'R';
  if (bellType[index] == BELL_END)    return 'E';

  // PERIOD
  return 'C';   // N = normal / period
}



/* ================= DASHBOARD ================= */


/* ================= DASHBOARD ================= */
void screenDashboard() {

  if (screenChanged) {
    lcd.clear();
    lcd.noBlink();
    screenChanged = false;
  }

  char line0[21];

  snprintf(line0, sizeof(line0),
         "%02d/%02d/%04d %02d:%02d:%02d",
         rtc.getDay(),
         rtc.getMonth(),
         2000 + rtc.getYear(),
         rtc.getHours(),
         rtc.getMinutes(),
         rtc.getSeconds());

  lcdPrintRow(0, line0);

  int curBell, nextBell;
  long countdown;

  getBellStatus(curBell, nextBell, countdown);

if (curBell >= 0)
//  snprintf(line0, sizeof(line0),"Current Bell:%2d %c",curBell + 1,getBellSymbolChar(curBell));
{
    uint8_t t = bellType[curBell];
    int periodNumber = 0;

    // Calculate period number exactly like the Matrix logic does
    for (int i = 0; i <= curBell; i++) {
        if (bellType[i] == BELL_PERIOD) {
            periodNumber++;
        }
    }

    if (t == BELL_PRAYER) {
        snprintf(line0, sizeof(line0), "Current: PRAYER [P]");
    } else if (t == BELL_RECESS) {
        snprintf(line0, sizeof(line0), "Current: RECESS [R]");
    } else if (t == BELL_END) {
        snprintf(line0, sizeof(line0), "Current: SCHOOL OFF[E]");
    } else {
        // This matches the 1, 2, 3... shown on your Matrix
        snprintf(line0, sizeof(line0), "Current: PERIOD %d", periodNumber);
    }
} else {
    snprintf(line0, sizeof(line0), "Current: --");
}
lcdPrintRow(1, line0);
//else
  //snprintf(line0, sizeof(line0), "Current Bell: --");

//lcdPrintRow(1, line0);






if (nextBell >= 0)
  snprintf(line0, sizeof(line0),
           "Next Bell: %02d:%02d %c",
           bellHour[nextBell],
           bellMin[nextBell],
           getBellSymbolChar(nextBell));
else
  snprintf(line0, sizeof(line0), "Next Bell:   --:--");

lcdPrintRow(2, line0);

  if (countdown >= 0) {

    int hh = countdown / 3600;
    int mm = (countdown % 3600) / 60;
    int ss = countdown % 60;

    snprintf(line0, sizeof(line0),
             "In: %02d:%02d:%02d", hh, mm, ss);
  }
  else {
    snprintf(line0, sizeof(line0), "In: --:--:--");
  }

  lcdPrintRow(3, line0);

  if (readButton(BTN_SET, btnSet)) {
    uiState = UI_MENU;
    screenChanged = true;
  }
}

/* ================= MENU ================= */
/* ================= MENU ================= */
void screenMenu()
{
  static int sel = 0;
  static int top = 0;
  static bool waitRelease = false;

  const char* items[] = {
    "Set Date & Time",
    "Set Bell",
    "Manage Timetable",
    "Clear EEPROM",
    "Trigger Bell Now",
    "Start Exam Timer",
    "Auto Generate Bells",
    "Self Test",
    "Return"
  };

  const int ITEM_COUNT = 9;
  const int VISIBLE = 3;

  /* -------- screen entry -------- */
  if (screenChanged)
  {
    sel = 0;
    top = 0;
    waitRelease = true;

    lcd.clear();
    lcd.noBlink();
    screenChanged = false;
  }

  /* -------- wait for key release -------- */
  if (waitRelease)
  {
    if (digitalRead(BTN_SET)  == HIGH &&
        digitalRead(BTN_UP)   == HIGH &&
        digitalRead(BTN_DOWN) == HIGH)
    {
      waitRelease = false;
    }
    return;
  }

  /* -------- navigation -------- */
  if (readButton(BTN_DOWN, btnDown) && sel < ITEM_COUNT - 1) sel++;
  if (readButton(BTN_UP,   btnUp)   && sel > 0) sel--;

  if (sel < top) top = sel;
  if (sel >= top + VISIBLE) top = sel - VISIBLE + 1;

  /* -------- render -------- */
  lcdPrintRow(0, "MENU");

  for (int r = 0; r < VISIBLE; r++)
  {
    int idx = top + r;
    char line[21];

    if (idx < ITEM_COUNT)
    {
      snprintf(line, sizeof(line),
               "%c %s",
               (idx == sel) ? '>' : ' ',
               items[idx]);
      lcdPrintRow(r + 1, line);
    }
    else
    {
      lcdPrintRow(r + 1, "");
    }
  }

  /* -------- select -------- */
  if (readButton(BTN_SET, btnSet))
  {
    screenChanged = true;

    switch (sel)
    {
      case 0: uiState = UI_SET_DATETIME;      break;
      case 1: uiState = UI_SET_BELL;          break;
      case 2: uiState = UI_MANAGE_Timetable; break;
      case 3: uiState = UI_CLEAR_EEPROM;      break;
      case 4: uiState = UI_TRIGGER_BELL;      break;
      case 5: uiState = UI_EXAM_TIMER;        break;
      case 6: uiState = UI_AUTO_BELLS;        break;
      case 7: uiState = UI_SELFTEST;          break;
      case 8: uiState = UI_DASHBOARD;         break;
    }
    return;
  }
}

/* ================= PLACEHOLDER ================= */
void placeholderScreen(const char* title) {

  if (screenChanged) {
    lcd.clear();
    lcd.noBlink();
    screenChanged = false;
  }

  lcdPrintRow(0, title);
  lcdPrintRow(1, "Feature Coming");
  lcdPrintRow(2, "Soon...");
  lcdPrintRow(3, "SET = BACK");

  if (readButton(BTN_SET, btnSet)) {
    uiState = UI_MENU;
    screenChanged = true;
  }
}

/* ================= SET DATE/TIME ================= */
void screenSetDateTime()
{
  static int step, d, m, y, h, mi;

  if (screenChanged)
  {
    d  = rtc.getDay();
    m  = rtc.getMonth();
    y  = 2000 + rtc.getYear();
    h  = rtc.getHours();
    mi = rtc.getMinutes();

    step = 0;
    lcd.clear();
    lcd.blink();
    screenChanged = false;
  }

  char l1[21], l2[21];

  snprintf(l1, sizeof(l1), "%02d %02d %04d", d, m, y);
  snprintf(l2, sizeof(l2), "%02d %02d", h, mi);

  lcdPrintRow(0, "SET DATE & TIME");
  lcdPrintRow(1, l1);
  lcdPrintRow(2, l2);
  lcdPrintRow(3, "SET=SAVE");

  const uint8_t cx[] = {0,3,6,0,3};
  const uint8_t cy[] = {1,1,1,2,2};
  lcd.setCursor(cx[step], cy[step]);

  if (readButton(BTN_UP, btnUp)) {
    if (step == 0 && d < 31) d++;
    else if (step == 1 && m < 12) m++;
    else if (step == 2) y++;
    else if (step == 3) h = (h + 1) % 24;
    else if (step == 4) mi = (mi + 1) % 60;
  }

  if (readButton(BTN_DOWN, btnDown)) {
    if (step == 0 && d > 1) d--;
    else if (step == 1 && m > 1) m--;
    else if (step == 2 && y > 2000) y--;
    else if (step == 3) h = (h + 23) % 24;
    else if (step == 4) mi = (mi + 59) % 60;
  }

  if (readButton(BTN_SET, btnSet)) {

    step++;

    if (step > 4) {

      lcd.noBlink();
      y = y % 100;
      rtc.setDate(d, m, y);
      rtc.setTime(h, mi, 0);

      uiState = UI_MENU;
      screenChanged = true;
    }
  }
}

/* ================= SET BELL ================= */
void screenSetBell()
{
  static int stage;   // 0=count, 1=edit bells, 2=save screen
  static int idx;
  static int hh, mm;
  static int step;    // 0=HH,1=MM,2=TYPE
  static uint8_t typeSel;

  static char ttName[TT_NAME_LEN+1];

  if (screenChanged)
  {
    stage = 0;
    idx = 0;
    hh = 0;
    mm = 0;
    step = 0;
    typeSel = BELL_PERIOD;

    lcd.clear();
    lcd.noBlink();
    screenChanged = false;
  }

  /* ================= STAGE 0 : number of bells ================= */
  if (stage == 0)
  {
    lcdPrintRow(0, "SET BELL");
    lcdPrintRow(1, "No of Bells");

    char cntLine[21];
    snprintf(cntLine, sizeof(cntLine), "%02d", bellCount);
    lcdPrintRow(2, cntLine);

    lcdPrintRow(3, "UP/DN SET=OK");

    if (readButton(BTN_UP, btnUp) && bellCount < MAX_BELLS) bellCount++;
    if (readButton(BTN_DOWN, btnDown) && bellCount > 0)     bellCount--;

    if (readButton(BTN_SET, btnSet))
    {
      if (bellCount == 0)
      {
        showError("INVALID", "Bell Count");
      }
      else
      {
        stage = 1;
        idx = 0;
        hh = 0;
        mm = 0;
        step = 0;
        typeSel = BELL_PERIOD;

        lcd.clear();
        lcd.blink();
      }
    }
    return;
  }

  /* ================= STAGE 2 : save screen ================= */
  if (stage == 2)
  {
    lcdPrintRow(0, "SAVE TIMETABLE ?");

    char line[21];
    snprintf(line, sizeof(line), "NAME: %s", ttName);
    lcdPrintRow(1, line);

    lcdPrintRow(3, "SET=SAVE UP=NO");

    if (readButton(BTN_SET, btnSet))
    {
      saveCurrentTimetable(ttName);

      showError("TIMETABLE", "SAVED");

      uiState = UI_MENU;
      screenChanged = true;
      return;
    }

    if (readButton(BTN_UP, btnUp))
    {
      uiState = UI_MENU;
      screenChanged = true;
      return;
    }

    return;
  }

  /* ================= STAGE 1 : edit bells ================= */

  lcdPrintRow(0, "SET BELL");

  char title[21];
  snprintf(title, sizeof(title), "Bell %d of %d", idx + 1, bellCount);
  lcdPrintRow(1, title);

  // row 2 : time
  char line2[21];
  snprintf(line2, sizeof(line2), "%02d:%02d", hh, mm);
  lcdPrintRow(2, line2);

  // row 3 : type when step==2
  if (step == 2)
  {
    const char* tname[] = {"PERIOD","PRAYER","RECESS","END"};

    char line3[21];
    snprintf(line3, sizeof(line3), ">%s", tname[typeSel]);
    lcdPrintRow(3, line3);

    lcd.setCursor(11, 3);
    lcd.print("SET=NEXT");

    lcd.setCursor(0, 3);
  }
  else
  {
    lcdPrintRow(3, "");
    lcd.setCursor(11, 3);
    lcd.print("SET=NEXT");

    if (step == 0) lcd.setCursor(0,2);
    else if (step == 1) lcd.setCursor(3,2);
  }

  /* ---- edit values ---- */

  if (readButton(BTN_UP, btnUp))
  {
    if      (step == 0 && hh < 23) hh++;
    else if (step == 1 && mm < 59) mm++;
    else if (step == 2 && typeSel < BELL_END) typeSel++;
  }

  if (readButton(BTN_DOWN, btnDown))
  {
    if      (step == 0 && hh > 0) hh--;
    else if (step == 1 && mm > 0) mm--;
    else if (step == 2 && typeSel > 0) typeSel--;
  }

  /* ---- confirm current bell ---- */

  if (readButton(BTN_SET, btnSet))
  {
    if (step < 2)
    {
      step++;
      return;
    }

    // only one END
    if (typeSel == BELL_END)
    {
      for (int i = 0; i < idx; i++)
        if (bellType[i] == BELL_END)
        {
          showError("ONLY ONE","END BELL");
          return;
        }
    }

    // only one PRAYER
    if (typeSel == BELL_PRAYER)
    {
      for (int i = 0; i < idx; i++)
        if (bellType[i] == BELL_PRAYER)
        {
          showError("ONLY ONE","PRAYER");
          return;
        }
    }

    // time must be increasing
    if (idx > 0)
    {
      int prev = bellHour[idx - 1] * 60 + bellMin[idx - 1];
      int curr = hh * 60 + mm;

      if (curr <= prev)
      {
        char err[21];
        snprintf(err, sizeof(err),
                 "Must be > %02d:%02d",
                 bellHour[idx - 1], bellMin[idx - 1]);

        showError("INVALID TIME", err);
        step = 0;
        return;
      }
    }

    // store this bell
    bellHour[idx] = hh;
    bellMin[idx]  = mm;
    bellType[idx] = typeSel;

    idx++;
    step = 0;
    hh = 0;
    mm = 0;
    typeSel = BELL_PERIOD;

    /* ---- last bell reached ---- */
    if (idx == bellCount)
    {
      lcd.noBlink();

      makeAutoTimetableName(ttName);

      stage = 2;          // go to save screen
      lcd.clear();
      return;
    }

    // prepare next bell entry
    lcd.clear();
    lcd.blink();
  }
}



/* ================= SELF TEST ================= */
void screenSelfTest() {
  static uint8_t step = 0;
  static uint8_t subStep = 1; 
  static unsigned long lastUpdate = 0;

  // --- INITIALIZATION ---
  if (screenChanged) {
    step = 0;
    subStep = 1;
    lastUpdate = millis();
    lcd.clear();
    matrix.clearDisplay(0);
    
    // Safety: Reset outputs to Idle states
    digitalWrite(RELAY_PIN, HIGH); // OFF (Active Low)
    digitalWrite(BUZZER_PIN, LOW);  // OFF (Active High)
    
    mp3.stop();
    screenChanged = false;
  }

  // --- EXIT LOGIC ---
  // Press SET to exit only after the full test is complete
  if (step == 10 && readButton(BTN_SET, btnSet)) {
    uiState = UI_MENU;
    screenChanged = true;
    return;
  }

  // Standard interval for automatic hardware steps
  if (step < 6 && (millis() - lastUpdate < 2000)) return;
  lastUpdate = millis();

  switch (step) {
    case 0: // 1. I2C & RTC
      lcdPrintRow(0, "1. I2C/RTC TEST");
      Wire.beginTransmission(EEPROM_ADDR);
      lcdPrintRow(1, (Wire.endTransmission() == 0) ? "EEPROM: OK (0x50)" : "EEPROM: NOT FOUND");
      lcdPrintRow(2, (rtc.getYear() > 0) ? "RTC: RUNNING" : "RTC: NOT INIT");
      break;

    case 1: // 2. Relay Test
      lcdPrintRow(0, "2. RELAY TEST");
      lcdPrintRow(1, "RELAY (A1) ON...");
      digitalWrite(RELAY_PIN, LOW); // ON
      break;

    case 2: // Stop Relay
      digitalWrite(RELAY_PIN, HIGH); // OFF
      lcdPrintRow(1, "RELAY (A1) OFF");
      break;

    case 3: // 3. Buzzer Test
      lcdPrintRow(0, "3. BUZZER TEST");
      lcdPrintRow(1, "BUZZER (A2) ON...");
      digitalWrite(BUZZER_PIN, HIGH); // ON
      break;

    case 4: // Stop Buzzer
      digitalWrite(BUZZER_PIN, LOW); // OFF
      lcdPrintRow(1, "BUZZER OFF");
      break;

  case 5: // 4. Matrix Test
  lcdPrintRow(0, "4. MATRIX TEST");
  lcdPrintRow(1, "FILLING...");

  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 8; j++) {
      matrix.setLed(0, i, j, true);
    }
  }

  delay(5000);   // 5 seconds ON time

  matrix.clearDisplay(0);

  step = 6;
  return;

    case 6: // 5. Audio Scan Setup
      matrix.clearDisplay(0);
      lcd.clear();
      lcdPrintRow(0, "5. AUDIO SCAN");
      lcdPrintRow(3, "SET = SKIP TRACK");
      subStep = 1;
      step = 7;
      return;

    case 7: // 5. Audio Execution (Tracks 1-14)

  static bool trackStarted = false;
  static unsigned long trackStartTime = 0;

  if (subStep <= 17) {

    char buf[20];
    snprintf(buf, sizeof(buf), "PLAYING: %02d.mp3", subStep);
    lcdPrintRow(1, buf);

    // ---- Start Track Only Once ----
    if (!trackStarted && digitalRead(MP3_BUSY_PIN) == HIGH) {

      mp3.playMp3Folder(subStep);
      trackStarted = true;
      trackStartTime = millis();
      delay(200);   // small stability delay
      return;
    }

    // ---- If Track Finished ----
    if (trackStarted && digitalRead(MP3_BUSY_PIN) == HIGH) {
      trackStarted = false;
      subStep++;
      delay(300);
      return;
    }

    // ---- Manual Skip ----
    if (digitalRead(BTN_SET) == LOW) {
      mp3.stop();
      delay(300);
      trackStarted = false;
      subStep++;
      return;
    }

    return;
  }

     step = 8;
      return;

    case 8: // 6. Button Test Setup
      lcd.clear();
      lcdPrintRow(0, "6. BUTTON TEST");
      subStep = 1;
      step = 9;
      return;

    case 9: // 6. Button Execution (Wait for Release)
      if (subStep == 1) {
        lcdPrintRow(1, "PRESS: SET (B14)");
        if (digitalRead(BTN_SET) == LOW) { 
          lcdPrintRow(2, "SET OK!"); 
          while(digitalRead(BTN_SET) == LOW); // WAIT FOR RELEASE
          delay(500); 
          subStep++; 
        }
      } else if (subStep == 2) {
        lcdPrintRow(1, "PRESS: UP  (B13)");
        if (digitalRead(BTN_UP) == LOW) { 
          lcdPrintRow(2, "UP OK!"); 
          while(digitalRead(BTN_UP) == LOW); // WAIT FOR RELEASE
          delay(500); 
          subStep++; 
        }
      } else if (subStep == 3) {
        lcdPrintRow(1, "PRESS: DOWN(B12)");
        if (digitalRead(BTN_DOWN) == LOW) { 
          lcdPrintRow(2, "DOWN OK!"); 
          while(digitalRead(BTN_DOWN) == LOW); // WAIT FOR RELEASE
          delay(500); 
          subStep++; 
        }
      } else {
        step = 10;
      }
      return;

    case 10: // Done
      lcd.clear();
      lcdPrintRow(1, "TEST COMPLETED");
      lcdPrintRow(3, "PRESS SET TO EXIT");
      return;
  }

  if (step < 6) step++;
} 

///////////////////////////////////////////////////////
////////////////////////////////////////////////////
uint32_t rtcToSeconds()
{
  return (uint32_t)rtc.getHours() * 3600UL
       + (uint32_t)rtc.getMinutes() * 60UL
       + (uint32_t)rtc.getSeconds();
}



/////////////////////////////////////////////////
bool bothPressed(uint8_t p1, uint8_t p2)
{
  static unsigned long t = 0;

  if (digitalRead(p1) == LOW && digitalRead(p2) == LOW)
  {
    if (millis() - t > 400)   // hold both for 400ms
      return true;
  }
  else
  {
    t = millis();
  }

  return false;
}

///////////////////////////////////////////////////
///////////////////////////////////////////////////
void updateExamMatrixPattern(uint32_t left, uint32_t total) {
  matrix.clearDisplay(0);
  
  // ਸਮੇਂ ਦੇ ਹਿਸਾਬ ਨਾਲ ਉਚਾਈ (0 ਤੋਂ 8) ਕੱਢੋ
  int height = map(left, 0, total, 0, 8);
  
  // 8 ਵਰਟੀਕਲ ਬਾਰਾਂ ਬਣਾਉਣ ਲਈ ਲੂਪ
  for (int col = 0; col < 8; col++) {
    for (int row = 0; row < height; row++) {
      // ਹੇਠਾਂ ਤੋਂ ਉੱਪਰ ਵੱਲ ਬਾਰਾਂ ਭਰੋ
      matrix.setLed(0, 7 - row, col, true); 
    }
  }
}

void screenExamTimer() {
  static uint8_t step = 0;
  static uint8_t lastStep = 255;
  static unsigned long lastMatrixUpdate = 0;

  // --- 1. LCD ਦੀ ਪਹਿਲੀ ਲਾਈਨ (ਹਮੇਸ਼ਾ ਚੱਲਦੀ ਘੜੀ) ---
  char clockLine[21];
  snprintf(clockLine, sizeof(clockLine), "%02d/%02d %02d:%02d:%02d", 
           rtc.getDay(), rtc.getMonth(), rtc.getHours(), rtc.getMinutes(), rtc.getSeconds());
  lcd.setCursor(0, 0);
  lcd.print(clockLine);

  if (screenChanged) {
    if (!examScheduled && !examRunning) {
      examStartH = rtc.getHours();
      examStartM = rtc.getMinutes();
      examDurH = 3;   
      examDurM = 0;
      step = 0;
    }
    lastStep = 255;
    lcd.clear();
    screenChanged = false;
  }

  /* ================= WAITING (ਸਮਾਂ ਹੋਣ ਦੀ ਉਡੀਕ) ================= */
  if (examScheduled && !examRunning) {
    uint32_t now = rtcToSeconds();
    uint32_t startSec = (uint32_t)examStartH * 3600UL + (uint32_t)examStartM * 60UL;

    lcdPrintRow(1, "STATUS: WAITING...");
    char startLine[21];
    snprintf(startLine, sizeof(startLine), "START AT: %02d:%02d", examStartH, examStartM);
    lcdPrintRow(2, startLine);
    lcdPrintRow(3, "UP+SET = CANCEL");

    // ਸਿਰਫ਼ ਉਸੇ ਸੈਕਿੰਡ 'ਤੇ ਸਟਾਰਟ ਹੋਵੇਗਾ ਜਦੋਂ ਸਮਾਂ ਮੈਚ ਕਰੇਗਾ
    if (now == startSec && rtc.getSeconds() == 0) {
      examScheduled = false;
      examRunning = true;
      examStartSec = now;
      
      digitalWrite(RELAY_PIN, LOW);   // ਘੰਟੀ ਚਾਲੂ
      digitalWrite(BUZZER_PIN, HIGH);
      mp3.playMp3Folder(15);          // Exam Start Punjabi Audio
      delay(2000);                    // ਸਿਰਫ਼ ਸਟਾਰਟ ਅਲਰਟ ਲਈ ਛੋਟਾ ਡਿਲੇਅ
      digitalWrite(RELAY_PIN, HIGH);  // ਘੰਟੀ ਬੰਦ
      digitalWrite(BUZZER_PIN, LOW);
      lcd.clear();
    }

    if (bothPressed(BTN_UP, BTN_SET)) {
      examScheduled = false;
      uiState = UI_MENU;
      screenChanged = true;
    }
    return; 
  }

  /* ================= RUNNING (ਪ੍ਰੀਖਿਆ ਚੱਲ ਰਹੀ ਹੈ) ================= */
  if (examRunning) {
    uint32_t now = rtcToSeconds();
    uint32_t elapsed = now - examStartSec;

    if (elapsed >= examDurationSec) {
      examRunning = false;
      digitalWrite(RELAY_PIN, LOW);
      digitalWrite(BUZZER_PIN, HIGH);
      mp3.playMp3Folder(16);          // Exam Over Punjabi Audio
      delay(3000); 
      digitalWrite(RELAY_PIN, HIGH);
      digitalWrite(BUZZER_PIN, LOW);
      lcd.clear();
      showError("EXAM FINISHED", "TIME UP");
      uiState = UI_DASHBOARD;
      screenChanged = true;
      return;
    }

    // 8x8 ਮੈਟ੍ਰਿਕਸ ਪੈਟਰਨ ਅੱਪਡੇਟ (ਹਰ 1 ਸੈਕਿੰਡ ਬਾਅਦ)
    if (millis() - lastMatrixUpdate > 1000) {
      updateExamMatrixPattern(examDurationSec - elapsed, examDurationSec);
      lastMatrixUpdate = millis();
    }

    uint32_t left = examDurationSec - elapsed;
    char leftLine[21];
    lcdPrintRow(1, "EXAM IN PROGRESS");
    snprintf(leftLine, sizeof(leftLine), "TIME LEFT: %02lu:%02lu:%02lu", left/3600, (left%3600)/60, left%60);
    lcdPrintRow(2, leftLine);
    lcdPrintRow(3, "UP+SET = STOP");

    if (bothPressed(BTN_UP, BTN_SET)) {
      examRunning = false;
      matrix.clearDisplay(0);
      uiState = UI_MENU;
      screenChanged = true;
    }
    return;
  }

  /* ================= EDIT / SETUP (ਸੈਟਿੰਗ ਮੋਡ) ================= */
  if (step != lastStep) {
    if (step < 4) lcdPrintRow(3, "SET=NEXT");
    else lcdPrintRow(3, "SET=START  UP=BACK");
    lastStep = step;
  }

  char sLine[21], dLine[21];
  snprintf(sLine, sizeof(sLine), "SET START: %02d:%02d", examStartH, examStartM);
  lcdPrintRow(1, sLine);
  snprintf(dLine, sizeof(dLine), "SET DUR  : %02d:%02d", examDurH, examDurM);
  lcdPrintRow(2, dLine);

  lcd.blink();
  if      (step == 0) lcd.setCursor(11, 1);
  else if (step == 1) lcd.setCursor(14, 1);
  else if (step == 2) lcd.setCursor(11, 2);
  else if (step == 3) lcd.setCursor(14, 2);
  else lcd.noBlink();

  if (readButton(BTN_UP, btnUp)) {
    if      (step == 0) examStartH = (examStartH + 1) % 24;
    else if (step == 1) examStartM = (examStartM + 1) % 60;
    else if (step == 2) examDurH = (examDurH + 1) % 24;
    else if (step == 3) examDurM = (examDurM + 1) % 60;
  }
  if (readButton(BTN_DOWN, btnDown)) {
    if      (step == 0) examStartH = (examStartH + 23) % 24;
    else if (step == 1) examStartM = (examStartM + 59) % 60;
    else if (step == 2) examDurH = (examDurH + 23) % 24;
    else if (step == 3) examDurM = (examDurM + 59) % 60;
  }

  if (readButton(BTN_SET, btnSet)) {
    if (step < 4) { step++; return; }
    examDurationSec = (uint32_t)examDurH * 3600UL + (uint32_t)examDurM * 60UL;
    examScheduled = true;
    step = 0;
    lcd.clear();
    lcd.noBlink();
  }
}
///////////////////////////////////////////////////
///////////////////////////////////////////////////

//////////////////////////////////////////////////



///////////////////////////////////////////////////
//
void drawTwoDigitScroll(uint8_t d1, uint8_t d2)
{
  static uint8_t offset = 0;
  static unsigned long t = 0;

  if (millis() - t > 120)   // scroll speed
  {
    t = millis();
    offset++;
    if (offset > 8) offset = 0;
  }

  matrix.clearDisplay(0);

  for (int row = 0; row < 8; row++)
  {
    // your drawChar8x8 uses flipped access
    byte a = font8x8[d1][7 - row];
    byte b = font8x8[d2][7 - row];

    // combine two chars side by side (16 bit wide)
    uint16_t line16 = ((uint16_t)a << 8) | b;

    // take sliding 8-bit window
    byte out = (line16 >> (8 - offset)) & 0xFF;

    for (int col = 0; col < 8; col++)
    {
      bool on = out & (1 << col);   // keep your horizontal flip
      matrix.setLed(0, row, col, on);
    }
  }
}

//////////////////////////////////////////////////////
bool generateAutoTimetable(
  uint8_t startH,
  uint8_t startM,

  uint8_t prayerMinutes,        // FIRST activity
  uint8_t periodMinutes,
  uint8_t totalPeriods,

  uint8_t recessMinutes,
  uint8_t recessAfterPeriod     // 1..totalPeriods , 0 = no recess
)
{
  bellCount = 0;

  int cur = startH * 60 + startM;

  /* ---------- FIRST : PRAYER ---------- */
  if (prayerMinutes > 0)
  {
    if (bellCount >= MAX_BELLS) return false;

    bellHour[bellCount] = cur / 60;
    bellMin [bellCount] = cur % 60;
    bellType[bellCount] = BELL_PRAYER;
    bellCount++;

    cur += prayerMinutes;
  }

  /* ---------- PERIODS ---------- */
  for (uint8_t p = 1; p <= totalPeriods; p++)
  {
    if (bellCount >= MAX_BELLS) return false;

    // period start bell
    bellHour[bellCount] = cur / 60;
    bellMin [bellCount] = cur % 60;
    bellType[bellCount] = BELL_PERIOD;
    bellCount++;

    cur += periodMinutes;

    // recess after this period
    if (recessAfterPeriod == p && recessMinutes > 0)
    {
      if (bellCount >= MAX_BELLS) return false;

      bellHour[bellCount] = cur / 60;
      bellMin [bellCount] = cur % 60;
      bellType[bellCount] = BELL_RECESS;
      bellCount++;

      cur += recessMinutes;
    }
  }

  /* ---------- END ---------- */
  if (bellCount >= MAX_BELLS) return false;

  bellHour[bellCount] = cur / 60;
  bellMin [bellCount] = cur % 60;
  bellType[bellCount] = BELL_END;
  bellCount++;

  return true;
}
/////////////////////////////////////////
void screenAutoGenerateBells()
{
  static uint8_t step = 0;
  static uint8_t mode = 0;   // 0 = input, 1 = preview, 2 = confirm

  static uint8_t startH, startM;
  static uint8_t prayerMin;
  static uint8_t periodMin;
  static uint8_t totalPeriods;
  static uint8_t recessMin;
  static uint8_t recessAfter;

  static uint8_t top = 0;   // for preview scroll

  if (screenChanged)
  {
    startH = 8;
    startM = 0;

    prayerMin    = 10;
    periodMin    = 40;
    totalPeriods = 6;
    recessMin    = 20;
    recessAfter  = 3;

    step = 0;
    mode = 0;
    top  = 0;

    lcd.clear();
    lcd.blink();
    screenChanged = false;
  }

  /* =====================================================
     MODE 1 : PREVIEW GENERATED BELLS (RAM ONLY)
     ===================================================== */
  if (mode == 1)
  {
    lcd.noBlink();

    lcdPrintRow(0, "AUTO PREVIEW");

    for (uint8_t r = 0; r < 2; r++)
    {
      uint8_t i = top + r;

      if (i < bellCount)
      {
        char line[21];
        char c;

        if      (bellType[i] == BELL_PERIOD) c = 'C';
        else if (bellType[i] == BELL_PRAYER) c = 'P';
        else if (bellType[i] == BELL_RECESS) c = 'R';
        else if (bellType[i] == BELL_END)    c = 'E';
        else                                 c = '?';

        snprintf(line, sizeof(line),
                 "B%02d %02d:%02d %c",
                 i + 1,
                 bellHour[i],
                 bellMin[i],
                 c);

        lcdPrintRow(r + 1, line);
      }
      else
        lcdPrintRow(r + 1, "");
    }

    lcdPrintRow(3, "UP/DN SCROLL SET");

    if (readButton(BTN_DOWN, btnDown))
      if (top + 2 < bellCount) top++;

    if (readButton(BTN_UP, btnUp))
      if (top > 0) top--;

    if (readButton(BTN_SET, btnSet))
    {
      mode = 2;   // go to save confirm
      lcd.clear();
    }

    return;
  }

  /* =====================================================
     MODE 2 : SAVE CONFIRM
     ===================================================== */
  if (mode == 2)
  {
    lcd.noBlink();

    lcdPrintRow(0, "SAVE TIMETABLE?");
    lcdPrintRow(1, "SET=YES  UP=NO");
    lcdPrintRow(2, "");
    lcdPrintRow(3, "");

    if (readButton(BTN_SET, btnSet))
    {
      char name[TT_NAME_LEN + 1];
      makeAutoTimetableName(name);

      saveCurrentTimetable(name);

      showError("AUTO TIME", "SAVED");

      uiState = UI_MENU;
      screenChanged = true;
      return;
    }

    if (readButton(BTN_UP, btnUp))
    {
      mode = 1;   // back to preview
      lcd.clear();
      return;
    }

    return;
  }
   


  /* =====================================================
     MODE 0 : INPUT SCREEN
     ===================================================== */

/* =====================================================
   MODE 0 : INPUT SCREEN
   ===================================================== */

  lcdPrintRow(0, "AUTO GENERATE");

  char line[21];

  switch (step)
  {
    case 0:
      snprintf(line, sizeof(line), "START %02d:%02d", startH, startM);
      lcdPrintRow(1, line);
      lcd.setCursor(6,1);
      break;

    case 1:
      snprintf(line, sizeof(line), "START %02d:%02d", startH, startM);
      lcdPrintRow(1, line);
      lcd.setCursor(9,1);
      break;

    case 2:
      snprintf(line, sizeof(line), "PRAYER %02d MIN", prayerMin);
      lcdPrintRow(1, line);
      lcd.setCursor(7,1);
      break;

    case 3:
      snprintf(line, sizeof(line), "PERIOD %02d MIN", periodMin);
      lcdPrintRow(1, line);
      lcd.setCursor(7,1);
      break;

    case 4:
      snprintf(line, sizeof(line), "TOTAL P %02d", totalPeriods);
      lcdPrintRow(1, line);
      lcd.setCursor(8,1);
      break;

    case 5:
      snprintf(line, sizeof(line), "RECESS %02d MIN", recessMin);
      lcdPrintRow(1, line);
      lcd.setCursor(7,1);
      break;

    case 6:
      snprintf(line, sizeof(line), "RECESS AFT %02d", recessAfter);
      lcdPrintRow(1, line);
      lcd.setCursor(11,1);
      break;
  }

  lcdPrintRow(2, "");
  lcdPrintRow(3, "UP/DN SET=NEXT");
  lcd.blink();

  /* -------- edit values -------- */

  if (readButton(BTN_UP, btnUp))
  {
    if      (step == 0 && startH < 23) startH++;
    else if (step == 1 && startM < 59) startM++;
    else if (step == 2 && prayerMin < 60) prayerMin++;
    else if (step == 3 && periodMin < 90) periodMin++;
    else if (step == 4 && totalPeriods < 17) totalPeriods++;
    else if (step == 5 && recessMin < 60) recessMin++;
    else if (step == 6 && recessAfter < totalPeriods) recessAfter++;
  }

  if (readButton(BTN_DOWN, btnDown))
  {
    if      (step == 0 && startH > 0) startH--;
    else if (step == 1 && startM > 0) startM--;
    else if (step == 2 && prayerMin > 0) prayerMin--;
    else if (step == 3 && periodMin > 1) periodMin--;
    else if (step == 4 && totalPeriods > 1) totalPeriods--;
    else if (step == 5 && recessMin > 0) recessMin--;
    else if (step == 6 && recessAfter > 0) recessAfter--;
  }
   if (recessAfter > totalPeriods)
  recessAfter = totalPeriods;
  /* -------- SET -------- */

  if (readButton(BTN_SET, btnSet))
  {
    if (step < 6)
    {
      step++;
      return;
    }

    // last field confirmed → generate now

    if (!generateAutoTimetable(
          startH, startM,
          prayerMin,
          periodMin,
          totalPeriods,
          recessMin,
          recessAfter))
    {
      showError("FAILED", "TOO MANY");
      screenChanged = true;
      return;
    }

    // go to preview
    top  = 0;
    mode = 1;
    lcd.clear();
    return;
  }
}


/* ================= SETUP ================= */
/* ================= SETUP ================= */
/* ================= SETUP ================= */
/* ================= SETUP ================= */
void screenTriggerBell() {
  static bool triggering = false;
  static unsigned long lastFlash = 0;
  static bool flashState = false;

  if (screenChanged) {
    lcd.clear();
    triggering = false;
    screenChanged = false;
  }

  lcdPrintRow(0, "MANUAL TRIGGER");

  if (!triggering) {
    lcdPrintRow(1, "READY TO RING");
    lcdPrintRow(3, "SET=START UP=BACK");

    if (readButton(BTN_SET, btnSet)) {
      mp3.stop(); 
      delay(50); 
      mp3.playMp3Folder(17); // Play Emergency Punjabi Script
      delay(200); // Wait for DFPlayer to pull Busy Pin LOW
      
      digitalWrite(RELAY_PIN, LOW);   // Industrial Bell ON
      digitalWrite(BUZZER_PIN, HIGH); // Internal Buzzer ON
      
      triggering = true;
      lcd.clear();
    }
  } 
  else {
    lcdPrintRow(1, "!! EMERGENCY !!");
    lcdPrintRow(2, "  ANNOUNCING  ");

    // --- 8x8 Matrix Flashing Logic ---
    if (millis() - lastFlash > 300) { // Flash every 300ms
      lastFlash = millis();
      flashState = !flashState;
      if (flashState) drawChar8x8(10); // Show 'C'
      else matrix.clearDisplay(0);    // Turn off
    }

    // --- Check if MP3 has finished ---
    // digitalRead(MP3_BUSY_PIN) == HIGH means the sound has stopped
    if (digitalRead(MP3_BUSY_PIN) == HIGH) {
      digitalWrite(RELAY_PIN, HIGH);  // Bell OFF
      digitalWrite(BUZZER_PIN, LOW);   // Buzzer OFF
      
      triggering = false;
      matrix.clearDisplay(0);
      showError("ALERT FINISHED", "");
      uiState = UI_MENU;
      screenChanged = true;
    }
  }
}
/* ================= SETUP ================= */
void screenTriggerBell1() {
  static bool triggering = false;
  static unsigned long startTime = 0;

  if (screenChanged) {
    lcd.clear();
    lcd.noBlink();
    triggering = false;
    screenChanged = false;
  }

  lcdPrintRow(0, "MANUAL TRIGGER");

  if (!triggering) {
    lcdPrintRow(1, "READY TO RING");
    lcdPrintRow(3, "SET=START UP=BACK");

    if (readButton(BTN_UP, btnUp)) {
      uiState = UI_MENU;
      screenChanged = true;
    }

    if (readButton(BTN_SET, btnSet)) {
      // Start the physical bell
      digitalWrite(RELAY_PIN, LOW);
      digitalWrite(BUZZER_PIN, HIGH);
      
      // Start the MP3 (Track 1 is usually the standard bell)
      //mp3.play(1); 
      mp3.play(17); 
      //mp3.playMp3Folder(17);
      delay(100); // Small pause for serial to finish
      // Step 2: Show the Matrix icon
      drawChar8x8(10); 
      delay(100);
      startTime = millis();
      triggering = true;
      lcd.clear();
    }
  } 
  else {
    // While the bell is ringing
    lcdPrintRow(1, "RINGING...");
    lcdPrintRow(2, "PLEASE WAIT");
    
    // Show an animation on the Matrix to indicate action
    drawChar8x8(10); // Show 'C' or a custom bell icon

    // Stop after 3 seconds
    if (millis() - startTime >= 3000) {
      digitalWrite(RELAY_PIN, HIGH);
      digitalWrite(BUZZER_PIN, LOW);
      
      triggering = false;
      matrix.clearDisplay(0);
      showError("BELL FINISHED", "");
      uiState = UI_MENU;
      screenChanged = true;
    }
  }
}
/* ================= SETUP ================= */
void screenClearEEPROM() {
  static bool confirm = false;

  if (screenChanged) {
    confirm = false;
    lcd.clear();
    lcd.noBlink();
    screenChanged = false;
  }

  if (!confirm) {
    lcdPrintRow(0, "!!! WARNING !!!");
    lcdPrintRow(1, "WIPE ALL DATA?");
    lcdPrintRow(3, "SET=YES  UP=BACK");

    if (readButton(BTN_UP, btnUp)) {
      uiState = UI_MENU;
      screenChanged = true;
    }

    if (readButton(BTN_SET, btnSet)) {
      confirm = true; // Move to the actual clearing phase
      lcd.clear();
    }
  } 
  else {
    lcdPrintRow(1, "CLEARING...");
    
    // 1. Reset the Timetable Count (Bytes 0 & 1)
    setTimetableCount(0);
    
    // 2. Reset Default Index (Byte 2)
    setDefaultTimetable(0);
    
    // 3. Clear the first 32 bytes (Management Area + First Name)
    // This ensures even if the count was wrong, the system sees 0s
    for (uint16_t i = 0; i < 32; i++) {
      eepromWriteByte(i, 0);
      
      // Visual feedback on the Matrix while it's working
      if (i % 4 == 0) matrix.setLed(0, 0, i/4, true); 
    }

    // Reset RAM variables
    bellCount = 0;
    
    showError("EEPROM WIPED", "SYSTEM RESET");
    matrix.clearDisplay(0);
    uiState = UI_DASHBOARD;
    screenChanged = true;
  }
}
/* ================= SETUP ================= */




































/* ================= SETUP ================= */
/* ================= SETUP ================= */

/* ================= SETUP ================= */
void setup() {

  pinMode(BTN_SET, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);

  pinMode(MP3_BUSY_PIN, INPUT_PULLUP);

  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(RELAY_PIN, HIGH);
  
  Wire.begin();
  Wire.setClock(100000);
  Wire.setTimeout(20); 

uint16_t cnt = getTimetableCount();
if (cnt > 50)
{
  setTimetableCount(0);
  setDefaultTimetable(0);
}


  lcd.init();
  lcd.backlight();

 // scanI2C();

  rtc.setClockSource(STM32RTC::LSE_CLOCK);
  rtc.begin();

//  loadBellsFromEEPROM();
uint16_t n = getTimetableCount();

if (n > 0)
{
  uint8_t d = getDefaultTimetable();
  if (d >= n) d = 0;

  loadTimetable(d);
}
     // default
else
  bellCount = 0;




  pinMode(MATRIX_CS, OUTPUT);
  pinMode(MATRIX_CLK, OUTPUT);
  pinMode(MATRIX_DIN, OUTPUT);

  matrix.shutdown(0, false);
  matrix.setIntensity(0, 5);
  matrix.clearDisplay(0);

  /* ---- DFPLAYER ---- */
  mp3Serial.begin(9600);
  mp3.begin(mp3Serial);
  mp3.volume(25);

  lcd.clear();
  uiState = UI_DASHBOARD;
  screenChanged = true;
}

/* ================= LOOP ================= */
void loop()
{
  int cb, nb;
  long cd;

  getBellStatus(cb, nb, cd);

  /* =====================================================
     1️⃣  EXACT TIME TRIGGER (ONLY ONCE PER MINUTE)
     ===================================================== */

  static int lastBellMinute = -1;

  int nowH = rtc.getHours();
  int nowM = rtc.getMinutes();
  int nowS = rtc.getSeconds();

  int currentMinute = nowH * 60 + nowM;

  if (!examRunning)
  {
    // Trigger only at second 0 and only once per minute
    if (nowS == 0 && currentMinute != lastBellMinute)
    {
      for (int i = 0; i < bellCount; i++)
      {
        if (bellHour[i] == nowH &&
            bellMin[i]  == nowM)
        {
          activeBellIndex = i;
          bellActive = true;
          mp3Playing = true;

          digitalWrite(BUZZER_PIN, HIGH);
          digitalWrite(RELAY_PIN, LOW);

          break;
        }
      }

      lastBellMinute = currentMinute;
    }
  }

  /* =====================================================
     2️⃣ MATRIX DISPLAY
     ===================================================== */
/* -------- MATRIX DISPLAY -------- */

if (!examRunning)
{
    // Show current bell if valid
    if (cb >= 0 && !(cb == bellCount - 1 && nb == -1 && !bellActive))
    {
        uint8_t t = bellType[cb];

        // 🔹 PRAYER
        if (t == BELL_PRAYER)
        {
            drawChar8x8(12);   // P
        }

        // 🔹 RECESS
        else if (t == BELL_RECESS)
        {
            drawChar8x8(11);   // R
        }

        // 🔹 END
        else if (t == BELL_END)
        {
            // Show only while ringing
            if (bellActive)
                drawChar8x8(13);   // E
            else
                matrix.clearDisplay(0);
        }

        // 🔹 PERIOD
        else if (t == BELL_PERIOD)
        {
            int periodNumber = 0;

            for (int i = 0; i <= cb; i++)
                if (bellType[i] == BELL_PERIOD)
                    periodNumber++;

            if (periodNumber < 10)
                drawChar8x8(periodNumber);
            else
                drawTwoDigitScroll(periodNumber / 10,
                                   periodNumber % 10);
        }
    }
    else
    {
        // Show countdown when no active bell
        updateCountdownIndicators(cd);
    }
}
else
{
    // -------- EXAM MODE MATRIX --------
    uint32_t now = rtcToSeconds();

    if (now >= examStartSec)
    {
        uint32_t elapsed = now - examStartSec;

        if (elapsed < examDurationSec)
            showCountdownOnMatrix(examDurationSec - elapsed);
        else
            matrix.clearDisplay(0);
    }
    else
    {
        matrix.clearDisplay(0);
    }
}



  

  /* =====================================================
     3️⃣ BELL DURATION HANDLER
     ===================================================== */

  if (!examRunning)
    handleBell();

  /* =====================================================
     4️⃣ MP3 CONTROL
     ===================================================== */

/* -------- MP3 CONTROL -------- */

if (mp3Playing)
{
    if (activeBellIndex >= 0)
    {
        uint8_t t = bellType[activeBellIndex];
        uint16_t track = 0;

        // 🔹 PRAYER
        if (t == BELL_PRAYER)
        {
            track = 11;   // 0011.mp3
        }

        // 🔹 RECESS
        else if (t == BELL_RECESS)
        {
            track = 12;   // 0012.mp3
        }

        // 🔹 END
        else if (t == BELL_END)
        {
            track = 13;   // 0013.mp3
        }

        // 🔹 PERIOD
        else if (t == BELL_PERIOD)
        {
            int periodNumber = 0;

            // Count only period bells up to this index
            for (int i = 0; i <= activeBellIndex; i++)
            {
                if (bellType[i] == BELL_PERIOD)
                    periodNumber++;
            }

            // Safety limit (1 to 10)
            if (periodNumber >= 1 && periodNumber <= 10)
                track = periodNumber;
        }

        // 🔥 Play only if track valid
        if (track > 0)
        {
              mp3.play(track);
                       
        }
    }

    mp3Playing = false;
}


  /* =====================================================
     5️⃣ UI HANDLER
     ===================================================== */

  switch (uiState)
  {
    case UI_DASHBOARD:    screenDashboard(); break;
    case UI_MENU:         screenMenu(); break;
    case UI_SET_DATETIME: screenSetDateTime(); break;
    case UI_SET_BELL:     screenSetBell(); break;
    case UI_MANAGE_Timetable: screenTimetableList(); break;
    case UI_CLEAR_EEPROM: screenClearEEPROM(); break;
    case UI_TRIGGER_BELL: screenTriggerBell(); break;
    case UI_EXAM_TIMER:   screenExamTimer(); break;
    case UI_AUTO_BELLS:   screenAutoGenerateBells(); break;
    case UI_SELFTEST:     screenSelfTest(); break;
  }

  delay(80);
}

/* ================= LOOP ================= */



