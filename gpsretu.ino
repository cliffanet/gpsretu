
#include "SSD1306.h"
SSD1306  display(0x3c, 4, 15);

#include <TinyGPS++.h>
TinyGPSPlus gps;
// будем использовать стандартный экземпляр класса HardwareSerial, 
// т.к. он и так в системе уже есть и память под него выделена
#define ss Serial1

#include <EEPROM.h>

// пины кнопок: вкл/выкл, точка-1, точка-2, точка-3
const uint8_t btnpin[] = { 18, 23, 19, 22 };

// пин для управления питанием переферией
#define POWER_PIN   2

static bool is_on = true;

// текущая выбранная точка
static uint8_t pnt = 0;

// данные по всем сохранённым точкам
#define EEPROM_MGC1   0xe4
#define EEPROM_MGC2   0x7a
typedef struct __attribute__((__packed__)) {  // структура для хранения точек в eeprom
    uint8_t mgc1 = EEPROM_MGC1;               // mgc1 и mgc2 служат для валидации текущих данных в eeprom
    struct __attribute__((__packed__)) {
        bool used = false;
        double lat = 0;
        double lng = 0;
    } pnt[3];
    uint8_t mgc2 = EEPROM_MGC2;
} eeprom_point_t;
static eeprom_point_t pntall;

void pntLoad();

//------------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial.println("begin");
    // пин управление питанием GPS-модуля
    pinMode(POWER_PIN, OUTPUT);
    
    // инициализируем дисплей
    // pin:16 - это reset дисплея, его надо перевести сначала в LOW, 
    // потом обратно в HIGH и так оставить
    pinMode(16, OUTPUT);
    digitalWrite(16, LOW);
    delay(50);
    digitalWrite(16, HIGH);
    delay(50);

    // теперь инициализация самой библиотеки дисплея
    display.init();
    display.clear();
    display.flipScreenVertically();
    if (is_on) {
          pwrOn();
          char s[100];
          strcpy_P(s, PSTR("initialisizing..."));
          display.setFont(ArialMT_Plain_16);
          display.drawString(0, 0, s);
          display.display();
    }
    else {
        pwrOff();
    }

    // инициируем uart-порт GPS-приёмника
    // Стандартные пины для свободного аппаратного Serial2 (16, 17) мы не можем использовать,
    // т.к. пин-16 используется дисплеем в этой плате, поэтому нам всё равно, какие пины использовать,
    // для удобства монтажа возьмём pin-5 и pin-17, при этом UART останется таким же аппаратным
    ss.begin(9600, SERIAL_8N1, 5, 17);

    // инициируем пины кнопок
    for (auto pin : btnpin)
        pinMode(pin, INPUT_PULLUP);

    // загружаем сохранённые координаты точек
    pntLoad();
}

/* ------------------------------------------------------------------------------------------- *
 * Функция мигания сообщением
 * ------------------------------------------------------------------------------------------- */
 static void (*msgFlashFunc)() = NULL;
 static uint32_t msgFlashEnd = 0;
 void msgFlash(void (*func)(), uint32_t interval = 3500) {
    msgFlashFunc = func;
    msgFlashEnd = millis() + interval;
 }

 bool msgFlashUpd() {
    if ((msgFlashFunc == NULL) || (msgFlashEnd == 0))
        return false;
    
    uint32_t d = millis();

    if (d > msgFlashEnd) {
        msgFlashFunc = NULL;
        msgFlashEnd = 0;
        return false;
    }

    if ((msgFlashEnd-d) & 0x100)
        msgFlashFunc();

    return true;
 }
 
//------------------------------------------------------------------------------
void _drawPnt(int8_t x = 63, int8_t y = 31) {
    display.fillCircle(x, y, 16);
    auto col = display.getColor();
    display.setColor(INVERSE);
    display.setFont(ArialMT_Plain_24);
    char s[10];
    sprintf_P(s, PSTR("%d"), pnt);
    display.drawString(x-6, y-13, s);
    display.setColor(col);
}
void flashPntSelect() {
    _drawPnt();
}
void flashPntReached() {
    _drawPnt();
    char s[] = { '!', 'o', 'k', '!', 0 };
    display.drawString(88, 18, s);
    display.drawString(0, 18, s);
}
void flashPntSave() {
    _drawPnt(20);
    
    char s[10];
    strcpy_P(s, PSTR("Saved!"));
    display.drawString(45, 18, s);
}
void flashPntClear() {
    _drawPnt(20);
    
    char s[10];
    strcpy_P(s, PSTR("Cleared"));
    display.drawString(45, 18, s);
}

/* ------------------------------------------------------------------------------------------- *
 * Функции хранения координат кнопок
 * ------------------------------------------------------------------------------------------- */
void pntLoad() {
    EEPROM.begin(sizeof(eeprom_point_t));
    eeprom_point_t *p = (eeprom_point_t *)EEPROM.getDataPtr();

    if ((p->mgc1 == EEPROM_MGC1) && (p->mgc2 == EEPROM_MGC2)) {
        pntall = *p;
    }
    else {
        eeprom_point_t pntall1;
        pntall = pntall1;
    }
    EEPROM.end();
}

void pntSave() {
    if (!gps.location.isValid() || (pnt == 0))
        return;
    pntLoad();

    pntall.pnt[pnt-1].used = true;
    pntall.pnt[pnt-1].lat = gps.location.lat();
    pntall.pnt[pnt-1].lng = gps.location.lng();

    EEPROM.begin(sizeof(eeprom_point_t));
    *((eeprom_point_t *)EEPROM.getDataPtr()) = pntall;
    EEPROM.commit();
    EEPROM.end();
    
    msgFlash(flashPntSave);
}

void pntClear() {
    if (pnt == 0)
        return;
    pntLoad();

    if (!pntall.pnt[pnt-1].used)
        return;

    pntall.pnt[pnt-1].used = false;
    pntall.pnt[pnt-1].lat = 0;
    pntall.pnt[pnt-1].lng = 0;

    EEPROM.begin(sizeof(eeprom_point_t));
    *((eeprom_point_t *)EEPROM.getDataPtr()) = pntall;
    EEPROM.commit();
    EEPROM.end();
    
    msgFlash(flashPntClear);
}

/* ------------------------------------------------------------------------------------------- *
 * Функция вкл/выкл питания
 * ------------------------------------------------------------------------------------------- */
void pwrOn() {
    is_on = true;
    display.displayOn();
    digitalWrite(POWER_PIN, LOW);
}

void pwrOff() {
    is_on = false;
    display.displayOff();
    digitalWrite(POWER_PIN, HIGH);
}

/* ------------------------------------------------------------------------------------------- *
 * Функция слежения за состояниями кнопок
 * ------------------------------------------------------------------------------------------- */
void btnRead() {
    static bool psh[] = { false, false, false, false };
    static uint32_t pshlong = 0, pshlonglong = 0;
    int n = 0;

    for (auto pin : btnpin) {
        bool pushed = digitalRead(pin) == LOW;
        
        if (pushed && !psh[n]) {
            // Кнопка только что нажата

            if (n == 0) {
                // кнопка "вкл-выкл"
                if (!is_on)
                    pwrOn();
            }
            else {
                // кнопки "точка-1-2-3" - переключаем на нужную точку
                pnt = n;
                if (pntall.pnt[pnt-1].used)
                    msgFlash(flashPntSelect, 1300);
            }
            
            pshlong = millis() + 4000;
            pshlonglong = millis() + 10000;
        }
        else
        if (pushed && psh[n] && (pshlong > 0) && (pshlong < millis())) {
            // Кнопка долго удерживается
            pshlong = 0; // чтобы событие сработало однократно до момента отпускания кнопки
            
            if (n == 0) {
                // кнопка "вкл-выкл"
                if (is_on)
                    pwrOff();
            }
            else {
                // кнопки "точка-1-2-3" - запоминаем новые координаты точки
                pntSave();
            }
        }
        else
        if (pushed && psh[n] && (pshlonglong > 0) && (pshlonglong < millis())) {
            // Кнопка очень долго удерживается
            pshlonglong = 0; // чтобы событие сработало однократно до момента отпускания кнопки
            
            if (n > 0) {
                // кнопки "точка-1-2-3" - стираем координаты точки
                pntClear();
            }
        }

        psh[n] = pushed;
        if (!is_on) break;
        n++;
    }
}

/* ------------------------------------------------------------------------------------------- *
 * Функция отрисовки компаса, провёрнутого на угол ang
 * ------------------------------------------------------------------------------------------- */
// PNT - Конвертирование координат X/Y при вращении вокруг точки CX/CY на угол ANG (рад)
#define PNT(x,y,ang,cx,cy)          round(cos(ang) * (x - cx) - sin(ang) * (y - cy)) + cx,  round(sin(ang) * (x - cx) + cos(ang) * (y - cy)) + cy
// APNT - упрощённая версия PNT, где ANG берётся автоматически из текущей зоны видимости кода
#define APNT(x,y,cx,cy)             PNT(x,y,ang,cx,cy)
inline void drawCompas(float ang) {
    char s[] = { 'N', 0 };
    display.setFont(ArialMT_Plain_10);
    
    display.drawCircle(32, 31, 1);
    //display.drawCircle(32,31, 31);
    display.drawString(APNT(29,0,29,25), s);
    display.drawCircle(APNT(32,6,32,31), 6);
    s[0] = 'S';
    display.drawString(APNT(29,50,29,25), s);
    s[0] = 'W';
    display.drawString(APNT(5,25,29,25), s);
    s[0] = 'E';
    display.drawString(APNT(53,25,29,25), s);
}

/* ------------------------------------------------------------------------------------------- *
 * Функция отрисовки стрелки направления к точке внутри компаса
 * ------------------------------------------------------------------------------------------- */
inline void drawPointArrow(float ang) {
    display.drawLine(APNT(32,15,32,31), APNT(32,42,32,31));
    display.drawLine(APNT(32,15,32,31), APNT(28,20,32,31));
    display.drawLine(APNT(32,15,32,31), APNT(36,20,32,31));
    display.drawLine(APNT(32,42,32,31), APNT(28,47,32,31));
    display.drawLine(APNT(32,42,32,31), APNT(36,47,32,31));
}

/* ------------------------------------------------------------------------------------------- *
 * Функция отрисовки всей информации о GPS
 * ------------------------------------------------------------------------------------------- */
inline void drawInfo() {
    char s[50];
    display.setFont(ArialMT_Plain_10);
    
    // количество спутников в правом верхнем углу
    if (gps.satellites.value() > 0)
        sprintf_P(s, PSTR("sat: %d"), gps.satellites.value());
    else
        strcpy_P(s, PSTR("no sat :("));
    display.drawString(90, 0, s);
    
    if (gps.satellites.value() == 0)
        return;

    // Текущие координаты
    if (gps.location.isValid()) {
        sprintf_P(s, PSTR("la:%f"), gps.location.lat());
        display.drawString(65, 12, s);
        sprintf_P(s, PSTR("lo:%f"), gps.location.lng());
        display.drawString(65, 24, s);
    }

    bool in_pnt = false;
    if (gps.location.isValid() && (pnt > 0) && pntall.pnt[pnt-1].used) {
        double dist = 
            TinyGPSPlus::distanceBetween(
                gps.location.lat(),
                gps.location.lng(),
                pntall.pnt[pnt-1].lat, 
                pntall.pnt[pnt-1].lng
            );

        in_pnt = dist < 8.0;
        
        display.setFont(ArialMT_Plain_24);
        if (dist < 950) 
            sprintf_P(s, PSTR("%dm"), (int)round(dist));
        else if (dist < 9500) 
            sprintf_P(s, PSTR("%0.1fkm"), dist/1000);
        else if (dist < 950000) 
            sprintf_P(s, PSTR("%dkm"), (int)round(dist/1000));
        else
            sprintf_P(s, PSTR("%0.2fMm"), dist/1000000);
        display.drawString(65, 36, s);
    }
    
    static bool in_pnt_prev = false;
    if (!in_pnt_prev && in_pnt)
        msgFlash(flashPntReached);
    in_pnt_prev = in_pnt;

    // Компас и стрелка к точке внутри него
    if (gps.course.isValid() && gps.location.isValid()) {
        // Компас показывает, куда смещено направление нашего движения 
        // относительно сторон Света,
        drawCompas(DEG_TO_RAD*(360 - gps.course.deg()));
        // а стрелка показывает отклонение направления к точке относительно 
        // направления нашего движения
        if ((pnt > 0) && pntall.pnt[pnt-1].used) {
            if (in_pnt) {
                display.drawCircle(32, 31, 10);
                display.drawCircle(32, 31, 15);
            }
            else {
                double courseto = 
                    TinyGPSPlus::courseTo(
                        gps.location.lat(),
                        gps.location.lng(),
                        pntall.pnt[pnt-1].lat, 
                        pntall.pnt[pnt-1].lng
                    );
                drawPointArrow(DEG_TO_RAD*(courseto-gps.course.deg()));
            }
            
            display.fillCircle(32, 31, 6);
            auto col = display.getColor();
            display.setColor(INVERSE);
            sprintf_P(s, PSTR("%d"), pnt);
            display.drawString(30, 25, s);
            display.setColor(col);
        }
    }
}

//------------------------------------------------------------------------------
void loop() {
    // читаем состояние кнопок
    btnRead();
    
    if (!is_on) {
        delay(100);
        return;
    }
    
    // Считывание данных с GPS - не более 50мс
    uint32_t m = millis()+50;
    do {
        char s;
        while (ss.available())
            gps.encode(s=ss.read());
    } while (millis() < m);
    
    // Начинаем отрисовку данных на дисплее
    display.clear();

    if (msgFlashUpd()) {
        // message flashing
    }
    else
    if (gps.satellites.isValid()) {
        // Данные с GPS получены, рисуем инфо:
        drawInfo();
    }
    else {
        // Если мы не можем узнать даже кол-во спутников,
        // то видимо данных ещё совсем нет, 
        // в это время совсем ничего не рисуем
        char s[30];
        display.setFont(ArialMT_Plain_16);
        strcpy_P(s, PSTR("Waiting GPS"));
        display.drawString(20, 20, s);
        strcpy_P(s, PSTR("data..."));
        display.drawString(50, 40, s);
    }
    display.display();

    Serial.println(gps.satellites.value());
    
    delay(200);
}
