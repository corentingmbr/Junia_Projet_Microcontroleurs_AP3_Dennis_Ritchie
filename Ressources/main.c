/* ==============================================================
 * Fichier     : main.c
 * Projet      : VU-mètre 4 bandes — JUNIA AP3 BX 2023-2024
 * MCU         : PIC18F25K40 @ 64 MHz (HFINTOSC interne)
 * Compilateur : XC8
 *
 * Brochage (à vérifier contre le schéma papier) :
 *   RA0 (AN0) — sortie filtre basses        (< 250 Hz)
 *   RA1 (AN1) — sortie filtre bas-médiums   (250 Hz – 1 kHz)
 *   RA2 (AN2) — sortie filtre hauts-médiums (1 kHz – 4 kHz)
 *   RA3 (AN3) — sortie filtre aigus         (> 4 kHz)
 *   RB0       — données matrice LED SK6812RGBW  (voir tx.asm)
 *   RC0–RC7   — LEDs de test LD0–LD7  (actives haut, résistances câblées)
 *   RB3       — LED de test LDM1  (indicateur alimentation)
 *   RB1       — Bouton B0  (entrée, pull-up interne, actif bas)
 *   RB2       — Bouton B1  (entrée, pull-up interne, actif bas)
 *
 * Architecture du signal :
 *   Signal audio → pré-ampli → 4 filtres analogiques →
 *   4 détecteurs d'enveloppe matériels (D1-D4, Ce, Re) →
 *   4 entrées ADC (DC proportionnel à l'amplitude par bande) →
 *   traitement numérique → matrice 8×8 LEDs
 * ============================================================== */

#include <xc.h>

#pragma config FEXTOSC = OFF
#pragma config RSTOSC  = HFINTOSC_64MHZ
#pragma config WDTE    = OFF

#define _XTAL_FREQ 64000000UL

/* ── Variables partagées avec tx.asm ─────────────────────────────
 * LED_MATRIX : 64 LEDs × 4 octets (G, R, B, W) = 256 octets
 * pC         : pointeur utilisé par _TX_64LEDS pour parcourir le tableau
 * -------------------------------------------------------------- */
volatile char LED_MATRIX[256];
volatile const char *pC = LED_MATRIX;
extern void TX_64LEDS(void);

/* ── Canaux ADC (valeur de ADPCH) ──────────────────────────────── */
#define ADC_BASSES    0x00   /* AN0 = RA0 — filtre basses         */
#define ADC_BAS_MID   0x01   /* AN1 = RA1 — filtre bas-médiums    */
#define ADC_HAUT_MID  0x02   /* AN2 = RA2 — filtre hauts-médiums  */
#define ADC_AIGUS     0x03   /* AN3 = RA3 — filtre aigus          */

/* ── Calibrage ADC ──────────────────────────────────────────────
 * Les détecteurs d'enveloppe matériels (D1-D4) fournissent un
 * niveau DC ≈ 0 V au repos.  Le seuil bruit est donc très faible
 * (quelques dizaines de counts sur 1023).
 * Ajuster SEUIL_BRUIT si la matrice scintille au silence.
 * -------------------------------------------------------------- */
#define SEUIL_BRUIT     20
#define ADC_MAX_REEL  1023

/* ── Dimensions de la matrice ──────────────────────────────────── */
#define NB_BANDES        4   /* 4 bandes de fréquences             */
#define COLS_PAR_BANDE   2   /* 2 colonnes par bande               */
#define LEDS_PAR_COL     8   /* 8 LEDs par colonne (8 rangées)     */

/* ── Luminosités SK6812RGBW (format GRBW) ──────────────────────
 * ATTENTION : ne jamais envoyer 255 — risque d'endommager la matrice
 * et la carte (surconsommation).  Valeurs de l'ordre de 16 à 32.
 * -------------------------------------------------------------- */
#define C_VERT_G    0x15   /* Vert   — rangées 0-3 (bas)          */
#define C_ORANGE_G  0x0A   /* Orange — rangées 4-5 (milieu)       */
#define C_ORANGE_R  0x14
#define C_ROUGE_R   0x20   /* Rouge  — rangées 6-7 (haut)         */

/* ── État interne par bande ────────────────────────────────────── */
static unsigned long volume_lisse[NB_BANDES];  /* niveau lissé (EMA)  */
static int           nb_leds_aff[NB_BANDES];   /* nb LEDs affichées   */

/* ── Prototypes ─────────────────────────────────────────────────── */
void         ADC_Init(void);
unsigned int ADC_Read(unsigned char channel);
void         setLED(unsigned char idx,
                    unsigned char g, unsigned char r,
                    unsigned char b, unsigned char w);
void         clearMatrix(void);
void         getRowColor(unsigned char row,
                         unsigned char *g, unsigned char *r, unsigned char *b);
void         drawBand(unsigned char band, unsigned char n_lit);

/* ============================================================== */

void ADC_Init(void) {
    TRISA  |= 0x0F;          /* RA0-RA3 en entrée                  */
    ANSELA |= 0x0F;          /* RA0-RA3 en mode analogique         */
    ADCLK   = 0x1F;          /* diviseur horloge ADC               */
    ADCON0bits.ADFM = 1;     /* résultat justifié à droite (10 bits) */
    ADCON0bits.ADCS = 0;     /* source horloge = registre ADCLK    */
    ADCON0bits.ADON = 1;     /* module ADC activé                  */
}

unsigned int ADC_Read(unsigned char channel) {
    ADPCH = channel;         /* sélection du canal                 */
    __delay_us(10);          /* temps d'acquisition du condensateur */
    ADCON0bits.GO = 1;
    while (ADCON0bits.GO);
    return (unsigned int)((ADRESH << 8) | ADRESL);
}

/* Écriture d'une LED dans LED_MATRIX (idx : 0-63, ordre GRBW) */
void setLED(unsigned char idx,
            unsigned char g, unsigned char r,
            unsigned char b, unsigned char w) {
    unsigned char base = (unsigned char)(idx << 2); /* idx × 4, max 63×4=252 */
    LED_MATRIX[base + 0] = g;
    LED_MATRIX[base + 1] = r;
    LED_MATRIX[base + 2] = b;
    LED_MATRIX[base + 3] = w;
}

void clearMatrix(void) {
    unsigned int i;
    for (i = 0; i < 256; i++) LED_MATRIX[i] = 0x00;
}

/* Couleur d'une LED selon sa rangée (0 = bas de colonne, 7 = haut) */
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

/* Affiche n_lit LEDs (0-8) depuis le bas pour une bande donnée.
 *
 * Disposition dans LED_MATRIX (conforme au sujet page 10) :
 *   Colonne c, rangée r  →  LED n° (c×8 + r)  →  index tableau (c×8 + r)
 *
 *   Bande 0 (basses)    → colonnes 0 et 1  → LEDs  0-15
 *   Bande 1 (bas-mid)   → colonnes 2 et 3  → LEDs 16-31
 *   Bande 2 (haut-mid)  → colonnes 4 et 5  → LEDs 32-47
 *   Bande 3 (aigus)     → colonnes 6 et 7  → LEDs 48-63
 */
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

/* ============================================================== */

void main(void) {
    unsigned char bande;
    unsigned int  mesure, plage_utile, volume;
    int           nb_leds_cible;

    /* ── Matrice LED : RB0 en sortie numérique ──────────────────
     * IMPORTANT : tx.asm pilote LATB bit 0 (RB0).
     * Ne pas confondre avec RB4 ou autre broche.
     * ---------------------------------------------------------- */
    ANSELB &= ~0x01;   /* RB0 non analogique                       */
    TRISB  &= ~0x01;   /* RB0 en sortie                            */
    LATB   &= ~0x01;   /* RB0 bas au démarrage                     */

    /* ── LEDs de test LD0-LD7 : tout le port C en sortie ─────── */
    ANSELC  = 0x00;    /* port C entièrement numérique             */
    TRISC   = 0x00;    /* port C entièrement en sortie             */
    LATC    = 0x00;    /* toutes les LEDs de test éteintes         */

    /* ── LED LDM1 (RB3) : indicateur "système sous tension" ──── */
    ANSELB &= ~0x08;         /* RB3 non analogique                 */
    TRISB  &= ~0x08;         /* RB3 en sortie                      */
    LATBbits.LATB3 = 1;      /* LDM1 allumée dès la mise sous tension */

    /* ── Boutons B0 (RB1) et B1 (RB2) : entrées pull-up interne  */
    ANSELB &= ~0x06;   /* RB1, RB2 non analogiques                 */
    TRISB  |=  0x06;   /* RB1, RB2 en entrée                       */
    WPUB   |=  0x06;   /* pull-up internes sur RB1 et RB2          */
    /* Lecture : PORTBbits.RB1 == 0 → bouton B0 appuyé (actif bas) */
    /* Lecture : PORTBbits.RB2 == 0 → bouton B1 appuyé (actif bas) */

    /* ── ADC ────────────────────────────────────────────────────  */
    ADC_Init();

    /* ── Initialisation des variables ───────────────────────────  */
    for (bande = 0; bande < NB_BANDES; bande++) {
        volume_lisse[bande] = 0;
        nb_leds_aff[bande]  = 0;
    }
    plage_utile = ADC_MAX_REEL - SEUIL_BRUIT;
    clearMatrix();

    /* ── Boucle principale (~8 ms/itération ≈ 125 Hz) ──────────  */
    while (1) {

        for (bande = 0; bande < NB_BANDES; bande++) {

            /* 1. Lecture ADC
             *    Les détecteurs d'enveloppe (D1-D4) fournissent déjà
             *    un niveau DC : une seule lecture suffit.           */
            mesure = ADC_Read(bande);

            /* 2. Filtre anti-bruit */
            if (mesure <= SEUIL_BRUIT) {
                volume = 0;
            } else {
                volume = mesure - SEUIL_BRUIT;
            }

            /* 3. Lissage exponentiel (EMA, facteur 1/12)
             *    Réduit les à-coups visuels sans masquer les transitoires */
            volume_lisse[bande] = ((volume_lisse[bande] * 11) + volume) / 12;

            /* 4. Mise à l'échelle 0–8 LEDs par colonne */
            nb_leds_cible = (int)(
                ((volume_lisse[bande] * LEDS_PAR_COL) + (plage_utile / 2))
                / plage_utile
            );
            if (nb_leds_cible > LEDS_PAR_COL) nb_leds_cible = LEDS_PAR_COL;
            if (nb_leds_cible < 0)             nb_leds_cible = 0;

            /* 5. Inertie : montée instantanée, descente de 1 LED/trame */
            if (nb_leds_cible >= nb_leds_aff[bande]) {
                nb_leds_aff[bande] = nb_leds_cible;
            } else {
                nb_leds_aff[bande]--;
            }

            /* 6. Mise à jour de la portion de matrice pour cette bande */
            drawBand(bande, (unsigned char)nb_leds_aff[bande]);
        }

        /* ── LEDs de test : 2 LEDs par bande active ─────────────
         * RC0/RC1 = bande basses   | RC2/RC3 = bas-médiums
         * RC4/RC5 = hauts-médiums  | RC6/RC7 = aigus
         * -------------------------------------------------------- */
        LATC = 0x00;
        for (bande = 0; bande < NB_BANDES; bande++) {
            if (nb_leds_aff[bande] > 0) {
                LATC |= (unsigned char)(0x03 << (bande * 2));
            }
        }

        /* ── Envoi série vers la matrice (via tx.asm) ─────────── */
        TX_64LEDS();

        __delay_ms(8);
    }
}
