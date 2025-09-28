$$narg = 1;

#include <stdio.h>
#include "BIOS.H"

#define WIDTH  256    
#define HEIGHT 256  
#define VMODE  VM40
#define MAXCOLOR 15
#define SCALE  192
#define FOCAL  192
#define N 16

/* Адреса устройств из документации */
#define sndc0r (*(int*)0161010) /* Регистр частоты канала 0 */
#define sndc1r (*(int*)0161012) /* Регистр частоты канала 1 */
#define sndc2r (*(int*)0161014) /* Регистр частоты канала 2 */
#define sndcsr (*(int*)0161016) /* Регистр управления каналами */
#define snlc0r (*(int*)0161020) /* Регистр громкости канала 0 */
#define snlc1r (*(int*)0161022) /* Регистр громкости канала 1 */
#define snlc2r (*(int*)0161024) /* Регистр громкости канала 2 */
#define snlcsr (*(int*)0161026) /* Регистр состояния громкости */


/* Определение адресов регистров */
#define RCSR  (*(unsigned short *)0176500)
#define RBUF  (*(unsigned short *)0176502)
#define TCSR  (*(unsigned short *)0176504)
#define TBUF  (*(unsigned short *)0176506)
#define PPIC  (*(unsigned short *)0161034)

/* Состояние битов */
#define TX_READY (TCSR & 0200)
#define RX_READY (RCSR & 0200)
#define RX_ERROR (RCSR & 0170000)
#define BREAK (RBUF & 04000)
#define SET_MODE (RBUF & 01000)
#define RS232C_MODE (RBUF & 0400) /* Бит 8: 1=RS232C, 0=токовая петля */

/* Адрес регистра управления дисководом */
#define HFBUF (*(unsigned short *)0177130) 
#define FLXTBL_SIZE 18 /* Размер таблицы FLXTBL в словах (36 байт) */

/* Часы-календарь */
#define CLKREG (*(unsigned short *)0177110)
#define BUF_SIZE 4000

short BR[BUF_SIZE];

struct WINTYP win;
struct PALTYP PAL;
struct PALTYP PAL2;
int IDENT, IDENT1, IDENT2, ANUM, ANUM1, ANUM2;
int *KEYB = 0177560;
int *KEYBUF = 0177562;

char STRA[] = "    ДИАГНОСТИКА ПК11/16к";
char STRB[] = "   Нажми ПРОБЕЛ для выхода";
char STRC[] = "    Если вы услышали звук с";
char STRD[] = "  нарастанием на трех каналах,";
char STRZ[] = "   устройстров работет верно!";
char STRE[] = "     [Инициализация порта]";
char STRF[] = "  ОШИБКА: RS232C не активирован!";
char STRG[] = "  Инициализация RS232C успешна";
char STRH[] = "       [Проверка RS232C]";
char STRI[] = "      Режим RS232C активен";
char STRJ[] = "     Нажми на ПК клавишу 'G'";
char STRL[] = "     Проверка сигналов:  OK";
char STRM[] = "   ОШИБКА: RS232C не работает!";
char STRN[] = "     Тест RS232C провален!";
char STRO[] = "     Время теста 20 секунд!!!";
char STRP[] = "      8N2, 9600 бод, RS232C";
char STRR[] = "      [Инициализация звука]";
char STRS[] = "        Проверка канала 1";
char STRT[] = "        Проверка канала 2";
char STRU[] = "        Проверка канала 3";
char STRV[] = "     Устройсто тест не прошло";
char STRW[] = "    Идет воспроизведение звука";
char STRX[] = "    Для повторного теста нужна";
char STRY[] = "      перезагрузка  ПК11/16!";


struct Point3D { int x, y, z; };

struct Point3D cube[8] = {
    {-40,-40,-40}, {40,-40,-40}, {40,40,-40}, {-40,40,-40},  /* Уменьшены координаты */
    {-40,-40,40}, {40,-40,40}, {40,40,40}, {-40,40,40}
};

int edges[12][2] = {
    {0,1}, {1,2}, {2,3}, {3,0},
    {4,5}, {5,6}, {6,7}, {7,4},
    {0,4}, {1,5}, {2,6}, {3,7}
};

int angle_x = 0;
int an_y = 0;


/* Объявления функций в стиле K&R */
struct Point3D rotate(p, ax, ay)
struct Point3D *p;
int ax, ay;
{
    struct Point3D res;
    /* Использование целочисленных тригонометрических функций */
    int cos_ax = cos(ax);
    int sin_ax = sin(ax);
    int cos_ay = cos(ay);
    int sin_ay = sin(ay);
    
    /* Вращение вокруг X */
    res.y = (p->y * cos_ax - p->z * sin_ax) / 100;
    res.z = (p->y * sin_ax + p->z * cos_ax) / 100;
    
    /* Вращение вокруг Y */
    res.x = (p->x * cos_ay + res.z * sin_ay) / 100;
    res.z = (res.z * cos_ay - p->x * sin_ay) / 100;
    
    return res;
}


struct Point3D project(p)
struct Point3D *p;
{
    int denominator, persp;
    struct Point3D res;
    int center_x;
    int center_y;
    denominator = p->z + FOCAL;
    persp = (FOCAL * SCALE) / denominator;

    if (denominator == 0) denominator = 1;
    
    /* Центр видимой области окна с учётом отступов */
    center_x = (win.SX1 + win.SX2) / 2; /* = (10 + 246)/2 = 128*/
    center_y = (win.SY1 + win.SY2) / 2 - 20; /* = (20 + 236)/2 = 128*/
    
    res.x = (p->x * persp) / SCALE + center_x;
    res.y = (p->y * persp) / SCALE + center_y; 
    res.z = p->z;
    return res;
}

/* Структура параметров дисковода */
struct flxtbl {
    unsigned short start_cyl;      /* Начальный цилиндр (биты 0-10 + флаги) */
    unsigned short num_cyl;        /* Количество цилиндров */
    unsigned short drive_density;   
    /*    Биты:
        - 0: Начальная сторона (0/1)
        - 1: Плотность (0=HD 1.6М, 1=DD 800К)
        - 2: Физический номер устройства (0/1)
    */
    unsigned short heads;          /* Количество головок */
    unsigned short start_sec;      /* Начальный сектор */
    unsigned short sec_per_track;  /* Секторов на дорожке */
    char  gap3;                    /* Размер межсекторного промежутка */
    char  step_time;               /* Время позиционирования (мс) */
    char  filler;                  /* Заполнитель сектора */
    char  reserved1;               /* Резервное поле */
    unsigned short total_blocks;   /* Общее количество блоков */
    unsigned short res;            /* Резервное поле */
};
/* Структура команды ввода-вывода */
struct iocb {
    unsigned short blkn;     /* Номер блока (0 - служебная информация) */
    unsigned short unit_code;/* 
        Биты:
        - 3: Признак FDD (1)
        - 8-15: Код операции (0360 - чтение FLXTBL) */
    unsigned short buff;     /* Адрес буфера данных */
    unsigned short wcnt;     /* Количество слов для чтения/записи */
};
/* Буфер для хранения таблицы параметров */
unsigned short flx_buffer[FLXTBL_SIZE]; 


/*============= Область функций ===================*/

int KEYPRESSED() { return(*KEYB & 0200); }
int READKEY() { return(*KEYBUF); }
GETCHH() { while(!KEYPRESSED()); READKEY(); }

PAUSE(time)
int time;
{
        int t1, t2;
        for (t1 = 0; t1 < time; ++t1)
                for (t2 = 0; t2 < 1024; ++t2);
}

/* Улучшенные тригонометрические функции */
cos(deg)
int deg;
{
    int quadrant;
    int base_angle;
    int idx;    
    static int cos_table[] = { 
        100,97,87,71,50,26,0,-26,-50,-71,-87,-97,-100 
    };
    deg = deg % 360;
    if(deg < 0) deg += 360;
    
    quadrant = deg / 90;
    base_angle = deg % 90;
    idx = base_angle / 15;
    
    switch(quadrant) {
        case 0: return cos_table[idx];
        case 1: return -cos_table[6 - idx];
        case 2: return -cos_table[idx];
        case 3: return cos_table[6 - idx];
        default: return 100;
    }
}

sin(deg)
int deg;
{
    return cos(deg - 90);
}
/* Инициализация графики Окно 1*/
init_gr() {
    int i;
    PAL.PCODE = 0;
    PAL.PMODPAL = VM40;
    for(i=0; i<16; i++)
        PAL.PAL[i] = i | (i<<5) | (i<<10);
    PLCRE(&PAL);

    GCREA(WIDTH, HEIGHT, PL1+VM40, &IDENT, &ANUM);
    GCLRR(IDENT);

    win.WNUM = 0;
    win.AREA = ANUM;
    win.SY1=20; win.SY2 = HEIGHT - 20;
    win.SX1=10; win.SX2 = WIDTH - 10;
    win.DEN=0x0202;
    VWCRE(&win);

    GROP(IDENT, 0);
    return 0;
}

/* Инициализация графики Окно 2*/
rs_grtest()
    {
    int i;
    PAL2.PCODE = 0;
    PAL2.PMODPAL = VM40;
    for(i = 0; i < 16; i++)
        PAL2.PAL[i] = i | (i<<4) | (i<<13);
    PLCRE(&PAL2);
    
    GCREA(WIDTH, HEIGHT, PL2+VM40, &IDENT2, &ANUM2);
    GCLRR(IDENT2);
    
    win.WNUM = 0;
    win.AREA = ANUM2;
    win.SY1=20; win.SY2=HEIGHT - 20;
    win.SX1=10; win.SX2=WIDTH - 10;
    win.DEN=0x0202;
    VWCRE(&win);
    
    GROP(IDENT2, 2);
    return 0;
}

/* ==================Функция определения объема памяти ======================*/

struct MemoryConfig {
    unsigned short total_kb;
    unsigned short expected_map_bytes;
    unsigned short block_size_kb;
};
typedef struct MemoryConfig MemoryConfig;

static MemoryConfig CONFIGS[] = {
    {4096, 0200, 4},
    {2048, 0100, 4},
    {1024, 0120, 4},
    {512, 020, 4}
};

MemoryConfig* detect_memory_config() {
    unsigned short actual_map_bytes = MFHLT(0100002);
    int q;
    for (q = 0; q < 4; ++q) {
        if (CONFIGS[q].expected_map_bytes == actual_map_bytes) {
            return &CONFIGS[q];
        }
    }
    return &CONFIGS[0];
}

int mem_test() {

    char status_msg[20];
    MemoryConfig *cfg;
    cfg = detect_memory_config();
    sprintf(status_msg, "Установлено ОЗУ: %u кб", cfg->total_kb);
    WPRINT(ANUM, 10, 40, status_msg);
  
    return 1;
}

/* ===========================Тест звукогенератора =========================*/
int sound_test() {
    int result = 1;
    /* Комплексная проверка с измерением времени */
    VWFORE(IDENT2);
    if(!play_chord()) {
        STRV[0] = 07;
        STRV[1] = 0360;
        WPRINT(ANUM2, 2, 120, STRV); 
        PAUSE(100);
        result = 0;
        }
        
    GCLRR(IDENT2);
    VWFORE(IDENT);
    return result;
}

/* Воспроизведение аккорда с таймингом */
int play_chord() {
    int start, y, n;
    int vol[N];
    vol[0]= 0; vol[1]= 0140; vol[2]=0130; vol[3]=0127; vol[4]=0120; vol[5]=0110;
    vol[6]=0100; vol[7]=060; vol[8]=040; vol[9]=030; vol[10]=020; vol[11]=014;
    vol[12]=010; vol[13]=06; vol[14]=04; vol[15]=03;

    STRR[0] = 07;
    STRR[1] = 0360;
    WPRINT(ANUM2, 2, 10, STRR);
    TIMEOF(); /*отключаем учет процессорного времени*/

    /* Запуск канала 1*/
    STRS[0] = 07;
    STRS[1] = 0360;
    WPRINT(ANUM2, 2, 30, STRS);
    STRW[0] = 07;
    STRW[1] = 0360;
    WPRINT(ANUM2, 2, 50, STRW);
  
        for(n = 0; n <= 15; ++n)
        {
        sndcsr = 066; /* все каналы в режиме генерации */
        snlcsr = 034;             
        snlc0r = vol[n];   /* Установка громкости */
        sndc0r = 193;  /* A4 */
        sndc0r = 17;
        /* задержка  */
        PAUSE(20); 
        snlcsr = 064;    
        sndcsr = 066;
        };
            /* Выключение */
    PAUSE(10);
   

    /* Запуск канала 2*/
    STRT[0] = 07;
    STRT[1] = 0360;
    WPRINT(ANUM2, 2, 70, STRT);
    STRW[0] = 07;
    STRW[1] = 0360;
    WPRINT(ANUM2, 2, 90, STRW);

        for(n = 0; n <= 15; ++n)
        { 
        sndcsr = 0166;
        snlcsr = 0134;  
        snlc1r = vol[n];     /* Установка громкости */
        sndc1r = 24;  /* C#5 */
        sndc1r = 14;
    /* задержка  */
        PAUSE(20);
        snlcsr = 0164;
        sndcsr = 0166;        
        };
            /* Выключение */            
    PAUSE(10);


    /* Запуск канала 3*/
    STRU[0] = 07;
    STRU[1] = 0360;
    WPRINT(ANUM2, 2, 110, STRU);
    STRW[0] = 07;
    STRW[1] = 0360;
    WPRINT(ANUM2, 2, 130, STRW);

        for(n = 0; n <= 15; ++n)
        { 
        sndcsr = 0266;
        snlcsr = 0234;      
        snlc2r = vol[n]; /* Установка громкости */
        sndc2r = 218;  /* E5 */
        sndc2r = 11;
    /* задержка  */
        PAUSE(20);
        snlcsr = 0264;    
        sndcsr = 0266;
        };
    /* Выключение */
    PAUSE(10);
    
    TIMEON();/*включаем учет процессорного времени*/

    STRC[0] = 07;
    STRC[1] = 0360;
    WPRINT(ANUM2, 2, 150, STRC);
    STRD[0] = 07;
    STRD[1] = 0360;
    WPRINT(ANUM2, 2, 170, STRD);
    STRZ[0] = 07;
    STRZ[1] = 0360;
    WPRINT(ANUM2, 2, 190, STRZ);

    PAUSE(250);

    return 1;
}
/*============== Тест RS232C ===============*/ 

/* Передача символа (без изменений) */
put_serial(c)
char c;
{
    while(!TX_READY);
    TBUF = c;
}
/* Прием символа (без изменений) */
getserial()
{

    while(!RX_READY);
    if(RX_ERROR) return -1;
    return RBUF & 0377;
}

/* Инициализация порта с проверкой режима */
serial_init()
{
    STRE[0] = 07;
    STRE[1] = 0360;
    WPRINT(ANUM2, 2, 10, STRE);
    /* Установка параметров с включением RS232C */
    PPIC |= 02; /*PPIC = 0377;*/
    RBUF = 0716; /* 8N2, 9600 бод, RS232C */

    /* Проверка успешности установки режима */
    if(!RS232C_MODE) {
        STRF[0] = 07;
        STRF[1] = 0360;
        WPRINT(ANUM2, 2, 20, STRF);       
        return;
    } 
        STRG[0] = 07;
        STRG[1] = 0360;
        WPRINT(ANUM2, 2, 20, STRG);    
}

/* Специальная проверка режима интерфейса */
check_rs232c() 
{
    char buf[50];
    int received;
    int count_rs, wait_rs;
    int A = 0;

    STRH[0] = 07;
    STRH[1] = 0360;
    WPRINT(ANUM2, 2, 40, STRH);    

    /* Проверка бита конфигурации */
    if (RS232C_MODE) {
        STRI[0] = 07;
        STRI[1] = 0360;
        WPRINT(ANUM2, 2, 60, STRI); 
        STRP[0] = 07;
        STRP[1] = 0360;
        WPRINT(ANUM2, 2, 70, STRP); 

        /* Ожидание символа 'G' */

        STRJ[0] = 07;
        STRJ[1] = 0360;
        WPRINT(ANUM2, 2, 90, STRJ);

        STRO[0] = 07;
        STRO[1] = 0360;
        WPRINT(ANUM2, 2, 160, STRO); 

        STRX[0] = 07;
        STRX[1] = 0360;
        WPRINT(ANUM2, 2, 180, STRX); 
 
        STRY[0] = 07;
        STRY[1] = 0360;
        WPRINT(ANUM2, 2, 190, STRY);           

        count_rs = 0;
        wait_rs = 1100;  /* ~20 сек  */
        received = -1;
        put_serial('P');
        put_serial('R'); 
        put_serial('E');
        put_serial('S'); 
        put_serial('S'); 
        put_serial(' '); 
        put_serial('G');
        put_serial(' ');                   
        while (count_rs < wait_rs) {
            if (RX_READY) {
                received = getserial();
                if (received == 'G') break;
            }
            PAUSE(1);
            count_rs++;
        }

        if (received == 'G') {
            sprintf(buf, "    Принят символ: %c", received);
            WPRINT(ANUM2, 6, 110, buf); 
            STRL[0] = 07;
            STRL[1] = 0360;
            WPRINT(ANUM2, 2, 130, STRL);
            A = 1;
        } else {
            sprintf(buf, "    Таймаут или ошибка");
            WPRINT(ANUM2, 6, 110, buf); 
            STRM[0] = 07;
            STRM[1] = 0360;
            WPRINT(ANUM2, 2, 130, STRM);
        }
        return A;
    }
    return A;
}

/* Тест RS232C */
RS_test() {
    int result;

    VWFORE(IDENT2);
    serial_init();    
    /* Основная проверка интерфейса */

    result = check_rs232c();

    if (!result) {
        STRN[0] = 07;
        STRN[1] = 0360;
        WPRINT(ANUM2, 2, 120, STRN);
        PAUSE(150);
        GCLRR(IDENT2);
        VWFORE(IDENT);
        return 0;
    }
    PAUSE(150);

    GCLRR(IDENT2);
    VWFORE(IDENT);
    return 1;
}
/*========Тест времени===========*/

/* Вывод времени в формате HH:MM:SS */
int getR0(val)
int (val); {return (val);}

rtc_test() {
    int i;
    /* Буфер для хранения строки времени (22 символа + терминатор) */
    char time_buf[23]; 
    char buf[22];
    getR0(time_buf);
    
    /* 1. Запрос строки времени через регистр CLKREG */
    /* 0177777 - команда для получения строки времени */
    CLKREG = 0177777;

    time_buf[22] = '\0';
    /* Вывод на экран */
    sprintf(buf, "Запрос времени: %s", time_buf + 14);        
    WPRINT(ANUM, 10, 140, buf);

    return 1;
}    

/*=======Тест дисковода (НГМД) ==========*/
/* Функция вывода информации о дисководе */
print_floppy_info(unit) 
    int unit; {
    char fd_msg[24];
    struct flxtbl *flx = (struct flxtbl *)flx_buffer;    
    /* Извлечение параметров из поля start_cyl */
    unsigned short cyl_num = flx->start_cyl & 03777;   /* Номер цилиндра (биты 0-10) */
    unsigned short sector_start = (flx->start_cyl >> 15) & 1; /* Старт сектора (бит 15) */
    unsigned short disk_enabled = (flx->start_cyl >> 14) & 1; /* Активность диска (бит 14) */
    unsigned short read_only = (flx->start_cyl >> 13) & 1;    /* Режим только чтения (бит 13) */
    unsigned short bootable = (flx->start_cyl >> 12) & 1;     /* Загрузочный диск (бит 12) */
    unsigned short warm_boot = (flx->start_cyl >> 11) & 1;    /* Возможность Warm Boot (бит 11) */
    /* Извлечение параметров из drive_density */
    unsigned short phys_device = (flx->drive_density >> 2) & 1;  /* Бит 2 */
    unsigned short density     = (flx->drive_density >> 1) & 1;  /* Бит 1 */
    unsigned short start_side  = flx->drive_density & 1;         /* Бит 0 */

    /* Вывод информации на русском языке */
    sprintf(fd_msg, "Параметры дисковода FD%d:", unit);
    WPRINT(ANUM2, 10, 10, fd_msg);
    sprintf(fd_msg, "Физическое устройство: %u", phys_device);
    WPRINT(ANUM2, 10, 30, fd_msg);    
    sprintf(fd_msg, "Плотность записи: %s", density ? "DD (800К)" : "HD (1.6М)");
    WPRINT(ANUM2, 10, 50, fd_msg);    
    sprintf(fd_msg, "Стартовый сектор: %s", sector_start ? "2" : "1");
    WPRINT(ANUM2, 10, 70, fd_msg);       
    sprintf(fd_msg, "Диск активен: %s", disk_enabled ? "Да " : "Нет");
    WPRINT(ANUM2, 10, 90, fd_msg);     
    sprintf(fd_msg, "Только чтение: %s", read_only ? "Да " : "Нет");
    WPRINT(ANUM2, 10, 110, fd_msg);    
    sprintf(fd_msg, "Количество головок: %u", flx->heads);
    WPRINT(ANUM2, 10, 130, fd_msg);    
    sprintf(fd_msg, "Количество сектов: %u", flx->sec_per_track);
    WPRINT(ANUM2, 10, 150, fd_msg);     
    sprintf(fd_msg, "Всего блоков: %u", flx->total_blocks);
    WPRINT(ANUM2, 10, 170, fd_msg);
    sprintf(fd_msg, "Загрузочный: %s", bootable ? "Да " : "Нет");
    WPRINT(ANUM2, 10, 190, fd_msg);     

    /**printf("Начальный цилиндр:  %u\n", cyl_num);   
    printf("Warm Boot:          %s\n", warm_boot ? "Да" : "Нет");
    printf("Начальная сторона:  %u\n", start_side);  **/ 
}
int floppy_test() {
    char msg[25];
    int unit = 0;
    struct iocb flx_req;
    int detected = 0; /* Флаг обнаружения дисковода */
    /* Проверка всех возможных дисководов (0-7) */
    VWFORE(IDENT2);
    while (unit < 8) {
        /* Ожидание готовности контроллера (бит 7) */
        while ((HFBUF & 0200) == 0);
        /* Формирование команды:
           - бит 3: признак FDD (010 в восьмеричной)
           - код операции 0360 (чтение FLXTBL)
           - номер устройства в старших битах */
        flx_req.unit_code = (010 | unit) << 8 | 0360; 
        flx_req.blkn = 0;
        flx_req.buff = (unsigned short)flx_buffer;
        flx_req.wcnt = 1; /* +1: запрос на чтение FLXTBL */
        /* Отправка команды */
        HFBUF = (unsigned short)&flx_req;
        unit=unit;
        /* Ожидание завершения операции */
        while ((HFBUF & 0200) == 0);

        /* Проверка статуса операции */
        if ((HFBUF >> 8) == 0) {
            print_floppy_info(unit);
            detected = 1;
        } else {
            sprintf(msg, "FD %d: Ошибка (статус=%06o)", unit, HFBUF);
            WPRINT(ANUM2, 10, 110, msg);     
            /*printf("FD %d: Ошибка (статус=%06o)\n", unit, HFBUF);*/
        }
        PAUSE(300);
        unit++;
        
    }

    if (!detected) {
        sprintf(msg, "  Дисководы не обнаружены");
        WPRINT(ANUM2, 10, 190, msg);
        PAUSE(150);

        VWKILL(&win);
        GKILL(IDENT2);
        PLFRE(&PAL2); 

        return 0;
    }

    PAUSE(150);
    VWKILL(&win);
    GKILL(IDENT2);
    PLFRE(&PAL2);

    return 1;
}

/*========  Тест эмулятора графического дисплея ==========*/
draw_line(x1, y1, x2, y2, col)
int x1, y1, x2, y2, col;
{
    GFGC(IDENT2, col);        /* Установка цвета один раз */
    GDOT(IDENT2, x1, y1);
    GLINE(IDENT2, x2, y2); /* Рисуем линию за один вызов */
}

/* Новая функция стирания куба */
erase_cube(x1, y1) 
    int x1, y1; {
    int i;
    GRVR(IDENT2, x1, y1, &BR); /* Восстановление фона */
            /* Очистка буфера */
    for(i = 0; i < BUF_SIZE; i++)
    BR[i] = 0;
}
/* Расширенный тест видеоконтроллера */
int video_test() {
    char wn[50];
    int i, count;
    int max_wait;

    int x1, y1, x2, y2;

    count = 0;
    max_wait = 60;
    
    rs_grtest();


    sprintf(wn, "    Время теста 20 секунд!");
    WPRINT(ANUM2, 6, 100, wn);

    PAUSE(100);
    GCLRR(IDENT2);

    sprintf(wn, "Отрисовка 3D куба");
    WPRINT(ANUM2, 30, 20, wn);
    sprintf(wn, "Вер. 1.0 Сборка 08.03.2025");
    WPRINT(ANUM2, 10, 190, wn);
            /* Координаты области захвата */
    x1 = WIDTH/2 - 60, y1 = HEIGHT/2 - 60 - 20; 
    x2 = WIDTH/2 + 60, y2 = HEIGHT/2 + 60 - 20;

    while(count < max_wait) {
        struct Point3D p1, p2, pp1, pp2;

        angle_x = (angle_x + 10) % 0240;
        an_y = (an_y + 5) % 0240;
        
        for(i = 0; i < 12; i++) {
            p1 = rotate(&cube[edges[i][0]], angle_x, an_y);
            p2 = rotate(&cube[edges[i][1]], angle_x, an_y);
            pp1 = project(&p1);
            pp2 = project(&p2);
            
            if(pp1.z > -FOCAL && pp2.z > -FOCAL)
                draw_line(pp1.x, pp1.y, pp2.x, pp2.y, (i%7)+9); 
        }
        PAUSE(1);
        GVRR(IDENT2, x1, y1, x2, y2, &BR);        /*Сохраняем*/
        erase_cube(x1, y1);   
             
        count++; 
    }   

    PAUSE(100);
    GCLRR(IDENT2);
    VWFORE(IDENT); 

    return 1;
}

/*=============Тест интерфейса принтера=======================*/
printer_test() {
    return  0 ;
}
/* ==================Вывод статуса ===========================*/
draw_status(y, msg, status)
int y, status;
char *msg;
{
    char buf[40];
    sprintf(buf, "%s: %s", msg, status ? "OK" : "FAIL");
    WPRINT(ANUM, 10, y, buf);
}
/*============= Основной цикл ==================================*/
main() {
    init_gr();
    STRA[0] = 07;
    STRA[1] = 0360;
    WPRINT(ANUM, 10, 10, STRA);

    /* Выполнение тестов */
    mem_test();
    PAUSE(150);
    draw_status(60, "Графический дисплей", video_test());
    PAUSE(100);
    draw_status(80, "Тест RS232C", RS_test());
    PAUSE(100);
    draw_status(100, "Тест Звука", sound_test());
    PAUSE(100);
    draw_status(120, "Тест дисковода", floppy_test());
    PAUSE(100);
    /*draw_status(140, "Printer Test", printer_test());*/
    rtc_test();
    
    /* Ожидание пробела */
    STRB[0] = 07;
    STRB[1] = 0360;
    WPRINT(ANUM, 10, 190, STRB);
    *KEYB = 0;
    while(1) {
        int i;
        if(KEYPRESSED() && READKEY() == 040) break;
        for(i=0;i<100;i++);
    }
    * KEYB |= 0100;
    GCLRR(IDENT);
    GKILL(IDENT);
    PLFRE(&PAL);
    VWKILL(&win);
    return 0;
}

