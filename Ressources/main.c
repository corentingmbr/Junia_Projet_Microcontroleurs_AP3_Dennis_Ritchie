/* --------------------------------------------------------------
 * Fichier     :   main.c
 * Description :   VU-mètre complet sur Matrice de 64 LEDs (RB4)
 * -------------------------------------------------------------- */

#include <xc.h>

#pragma config FEXTOSC = OFF
#pragma config RSTOSC = HFINTOSC_64MHZ
#pragma config WDTE = OFF

#define _XTAL_FREQ 64000000

// Variables partagées avec l'assembleur
volatile char LED_MATRIX [256];
volatile const char * pC = LED_MATRIX;
extern void TX_64LEDS(void);

// Vos paramètres de calibrage validés précédemment
#define SEUIL_BRUIT    520
#define ADC_MAX_REEL   1023

void ADC_Init(void) {
    TRISA |= 0x01;
    ANSELA |= 0x01;
    ADPCH = 0x00;
    ADCLK = 0x1F;
    ADCON0bits.ADFM = 1;
    ADCON0bits.ADCS = 0;
    ADCON0bits.ADON = 1;
}

unsigned int ADC_Read(void) {
    ADCON0bits.GO = 1;
    while(ADCON0bits.GO);
    return (unsigned int)((ADRESH << 8) | ADRESL);
}

void main(void) {
    /* Initialisations */
    ADC_Init();

    // Configuration de la broche RB4 (Sortie numérique pour la Matrice)
    ANSELB &= ~0x10; // Désactive l'analogique sur RB4
    TRISB &= ~0x10;  // RB4 en sortie
    LATB &= ~0x10;   // RB4 à 0 au départ

    unsigned long volume_lisse = 0;
    int nb_leds_prec = 0;
    unsigned int plage_utile = ADC_MAX_REEL - SEUIL_BRUIT;

    while(1) {
        unsigned int volume_max = 0;

        // 1. Fenêtre de capture (60 mesures flash pour capturer les basses)
        for (int i = 0; i < 60; i++) {
            unsigned int mesure = ADC_Read();
            if (mesure > volume_max) {
                volume_max = mesure;
            }
            __delay_us(80);
        }

        // 2. Filtre anti-bruit
        if (volume_max <= SEUIL_BRUIT) {
            volume_max = 0;
        } else {
            volume_max = volume_max - SEUIL_BRUIT;
        }

        // 3. Lissage par moyenne glissante
        volume_lisse = ((volume_lisse * 11) + volume_max) / 12;

        // 4. Calcul du nombre de LEDs sur l'échelle de 64 (avec arrondi parfait)
        int nb_leds_cible = (int)(((volume_lisse * 64) + (plage_utile / 2)) / plage_utile);

        if (nb_leds_cible > 64) {
            nb_leds_cible = 64;
        }

        // 5. Inertie de descente fluide
        if (nb_leds_cible >= nb_leds_prec) {
            nb_leds_prec = nb_leds_cible;
        } else {
            nb_leds_prec--;
        }

        // 6. Remplissage du tableau LED_MATRIX (G, R, B, W)
        for (int i = 0; i < 64; i++) {
            int index = i * 4;

            if (i < nb_leds_prec) {
                // Dégradé de couleur selon la position de la LED (i)
                if (i < 30) {
                    // Les 30 premières LEDs : VERT
                    LED_MATRIX[index + 0] = 0x15; // Vert
                    LED_MATRIX[index + 1] = 0x00; // Rouge
                    LED_MATRIX[index + 2] = 0x00; // Bleu
                }
                else if (i < 50) {
                    // De 30 à 50 : ORANGE (Vert + Rouge)
                    LED_MATRIX[index + 0] = 0x0B; // Vert moyen
                    LED_MATRIX[index + 1] = 0x15; // Rouge
                    LED_MATRIX[index + 2] = 0x00; // Bleu
                }
                else {
                    // Les 14 dernières : ROUGE complet
                    LED_MATRIX[index + 0] = 0x00; // Vert
                    LED_MATRIX[index + 1] = 0x20; // Rouge brillant
                    LED_MATRIX[index + 2] = 0x00; // Bleu
                }
                LED_MATRIX[index + 3] = 0x00;     // Blanc éteint
            }
            else {
                // LED doit être éteinte
                LED_MATRIX[index + 0] = 0x00;
                LED_MATRIX[index + 1] = 0x00;
                LED_MATRIX[index + 2] = 0x00;
                LED_MATRIX[index + 3] = 0x00;
            }
        }

        // 7. Envoi physique des données à la matrice
        TX_64LEDS();

        // Pause de rafraîchissement
        __delay_ms(8);
    }
}