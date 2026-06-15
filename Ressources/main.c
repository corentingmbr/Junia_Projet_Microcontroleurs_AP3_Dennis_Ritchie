#include <xc.h>

#pragma config FEXTOSC = OFF
#pragma config RSTOSC  = HFINTOSC_64MHZ
#pragma config WDTE    = OFF

#define _XTAL_FREQ 64000000UL

volatile char LED_MATRIX[256];
volatile const char *pC = LED_MATRIX;
extern void TX_64LEDS(void);

#define ADC_BASSES    0x00   
#define ADC_BAS_MID   0x01   
#define ADC_HAUT_MID  0x02   
#define ADC_AIGUS     0x03   


#define SEUIL_BRUIT     20
#define ADC_MAX_REEL  1023

#define NB_BANDES        4   
#define COLS_PAR_BANDE   2   
#define LEDS_PAR_COL     8   


#define C_VERT_G    0x15   
#define C_ORANGE_G  0x0A   
#define C_ORANGE_R  0x14
#define C_ROUGE_R   0x20  

static unsigned long volume_lisse[NB_BANDES];  
static int           nb_leds_aff[NB_BANDES];   


void         ADC_Init(void);
unsigned int ADC_Read(unsigned char channel);
void         setLED(unsigned char idx,
                    unsigned char g, unsigned char r,
                    unsigned char b, unsigned char w);
void         clearMatrix(void);
void         getRowColor(unsigned char row,
                         unsigned char *g, unsigned char *r, unsigned char *b);
void         drawBand(unsigned char band, unsigned char n_lit);


void ADC_Init(void) {
    TRISA  |= 0x0F;         
    ANSELA |= 0x0F;          
    ADCLK   = 0x1F;          
    ADCON0bits.ADFM = 1;     
    ADCON0bits.ADCS = 0;     
    ADCON0bits.ADON = 1;     
}

unsigned int ADC_Read(unsigned char channel) {
    ADPCH = channel;         
    __delay_us(10);          
    ADCON0bits.GO = 1;
    while (ADCON0bits.GO);
    return (unsigned int)((ADRESH << 8) | ADRESL);
}

void setLED(unsigned char idx,
            unsigned char g, unsigned char r,
            unsigned char b, unsigned char w) {
    unsigned char base = (unsigned char)(idx << 2); 
    LED_MATRIX[base + 0] = g;
    LED_MATRIX[base + 1] = r;
    LED_MATRIX[base + 2] = b;
    LED_MATRIX[base + 3] = w;
}

void clearMatrix(void) {
    unsigned int i;
    for (i = 0; i < 256; i++) LED_MATRIX[i] = 0x00;
}


void getRowColor(unsigned char row,
                 unsigned char *g, unsigned char *r, unsigned char *b) {
    if (row < 4) {
        *g = C_VERT_G;   *r = 0x00;      *b = 0x00;
    } else if (row < 6) {
        *g = C_ORANGE_G; *r = C_ORANGE_R; *b = 0x00;
    } else {
        *g = 0x00;       *r = C_ROUGE_R;  *b = 0x00;
    }
}


void drawBand(unsigned char band, unsigned char n_lit) {
    unsigned char col_offset, col, row, led_idx;
    unsigned char g, r, b;
    for (col_offset = 0; col_offset < COLS_PAR_BANDE; col_offset++) {
        col = band * COLS_PAR_BANDE + col_offset;
        for (row = 0; row < LEDS_PAR_COL; row++) {
            led_idx = col * LEDS_PAR_COL + row;
            if (row < n_lit) {
                getRowColor(row, &g, &r, &b);
                setLED(led_idx, g, r, b, 0x00);
            } else {
                setLED(led_idx, 0x00, 0x00, 0x00, 0x00);
            }
        }
    }
}


void main(void) {
    unsigned char bande;
    unsigned int  mesure, plage_utile, volume;
    int           nb_leds_cible;

   
    ANSELB &= ~0x01;  
    TRISB  &= ~0x01;  
    LATB   &= ~0x01;   

    ANSELC  = 0x00;    
    TRISC   = 0x00;    
    LATC    = 0x00;    

    
    ANSELB &= ~0x08;         
    TRISB  &= ~0x08;         
    LATBbits.LATB3 = 1;      

    ANSELB &= ~0x06;   
    TRISB  |=  0x06;   
    WPUB   |=  0x06;   

    ADC_Init();

    for (bande = 0; bande < NB_BANDES; bande++) {
        volume_lisse[bande] = 0;
        nb_leds_aff[bande]  = 0;
    }
    plage_utile = ADC_MAX_REEL - SEUIL_BRUIT;
    clearMatrix();

    while (1) {

        for (bande = 0; bande < NB_BANDES; bande++) {

            
            mesure = ADC_Read(bande);

            if (mesure <= SEUIL_BRUIT) {
                volume = 0;
            } else {
                volume = mesure - SEUIL_BRUIT;
            }

            
            volume_lisse[bande] = ((volume_lisse[bande] * 11) + volume) / 12;

            nb_leds_cible = (int)(
                ((volume_lisse[bande] * LEDS_PAR_COL) + (plage_utile / 2))
                / plage_utile
            );
            if (nb_leds_cible > LEDS_PAR_COL) nb_leds_cible = LEDS_PAR_COL;
            if (nb_leds_cible < 0)             nb_leds_cible = 0;

            if (nb_leds_cible >= nb_leds_aff[bande]) {
                nb_leds_aff[bande] = nb_leds_cible;
            } else {
                nb_leds_aff[bande]--;
            }

            drawBand(bande, (unsigned char)nb_leds_aff[bande]);
        }

        
        LATC = 0x00;
        for (bande = 0; bande < NB_BANDES; bande++) {
            if (nb_leds_aff[bande] > 0) {
                LATC |= (unsigned char)(0x03 << (bande * 2));
            }
        }

        TX_64LEDS();

        __delay_ms(8);
    }
}
