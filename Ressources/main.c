/* --------------------------------------------------------------
 * Fichier     :   main.c
 * Description :   Combo unique : VU-mètre global sur les 8 LEDs 
 * + Analyseur de spectre 4 bandes sur la matrice
 * -------------------------------------------------------------- */

#include <xc.h>
#include <stdlib.h>

#pragma config FEXTOSC = OFF           
#pragma config RSTOSC = HFINTOSC_64MHZ 
#pragma config WDTE = OFF              

#define _XTAL_FREQ 64000000 

volatile char LED_MATRIX [256]; 
volatile const char * pC = LED_MATRIX; 
extern void TX_64LEDS(void); 

// Configuration du recentrage audio
#define BIAS_AUDIO     520   
#define SEUIL_FILTRE   25    // Seuil anti-bruit fonctionnel !
#define AMP_MAX_UTILE  (520 - SEUIL_FILTRE)

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
    ADC_Init();
    
    // PORTB
    ANSELB = 0x00;
    TRISB = 0x00;  
    LATB = 0x00;   

    //  PORTC
    ANSELC = 0x00;
    TRISC = 0x00; 
    LATC = 0x00;

    // Table de correspondance PORTC
    const char table_vumetre[9] = {
        0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF 
    };

    // Variables pour les filtres
    int lp_bass = 0;
    int lp_mid_grave = 0;
    int lp_mid_aigu = 0;

    // Variables de lissage
    unsigned long volume_global_lisse = 0;
    int nb_leds_8_prec = 0;                  // Mémoire pour les 8 LEDs
    int hauteur_matrice_prec[4] = {0, 0, 0, 0}; // Mémoire pour la matrice

    // Boucle principale
    while(1) {
        int max_global = 0;
        int max_bass = 0;
        int max_mid_grave = 0;
        int max_mid_aigu = 0;
        int max_aigu = 0;

        // CAPTURE ET FILTRAGE
        for (int i = 0; i < 150; i++) {
            int sample = (int)ADC_Read() - BIAS_AUDIO;

            // Amplitude globale des 8 leds
            int amp_pure = abs(sample);
            if (amp_pure > max_global) max_global = amp_pure;

            // Filtre à basses
            lp_bass = ((lp_bass * 15) + sample) / 16;
            int amp_bass = abs(lp_bass);
            if (amp_bass > max_bass) max_bass = amp_bass;

            // Filtre des médiums-graves
            lp_mid_grave = ((lp_mid_grave * 7) + sample) / 8;
            int amp_mg = abs(lp_mid_grave - lp_bass);
            if (amp_mg > max_mid_grave) max_mid_grave = amp_mg;

            // Filtre des médiums-aigus
            lp_mid_aigu = ((lp_mid_aigu * 3) + sample) / 4;
            int amp_ma = abs(lp_mid_aigu - lp_mid_grave);
            if (amp_ma > max_mid_aigu) max_mid_aigu = amp_ma;

            // Filtres des aigus
            int amp_aigu = abs(sample - lp_mid_aigu);
            if (amp_aigu > max_aigu) max_aigu = amp_aigu;
            
            __delay_us(40); 
        }

        // Traitement du vu-mètre
        if (max_global <= SEUIL_FILTRE) max_global = 0;
        else max_global = max_global - SEUIL_FILTRE;

        volume_global_lisse = ((volume_global_lisse * 11) + max_global) / 12;
        
        // Calcul de la hauteur en 0-8 au lieu de 0-7 vu qu'on a les 9 leds.
        int nb_leds_8_cible = (int)(((volume_global_lisse * 8) + (AMP_MAX_UTILE / 2)) / AMP_MAX_UTILE);
        if (nb_leds_8_cible > 8) nb_leds_8_cible = 8;

        // Inertie pour les leds
        if (nb_leds_8_cible >= nb_leds_8_prec) nb_leds_8_prec = nb_leds_8_cible;
        else nb_leds_8_prec--;
        if (nb_leds_8_prec < 0) nb_leds_8_prec = 0;

        // Affichage physique sur la carte
        LATC = table_vumetre[nb_leds_8_prec];


        // Analyseur de Spectre de la matrice 8x8
        int amplitudes[4] = {max_bass, max_mid_grave, max_mid_aigu, max_aigu};
        int gains[4] = {18, 22, 26, 32}; // Ajustement de sensibilité par bande
        int hauteurs_cibles[4];

        for (int b = 0; b < 4; b++) {
            hauteurs_cibles[b] = (amplitudes[b] * gains[b]) / 256;
            if (hauteurs_cibles[b] > 8) hauteurs_cibles[b] = 8;

            // Inertie pour la matrice
            if (hauteurs_cibles[b] >= hauteur_matrice_prec[b]) {
                hauteur_matrice_prec[b] = hauteurs_cibles[b]; 
            } else {
                hauteur_matrice_prec[b]--; 
                if (hauteur_matrice_prec[b] < 0) hauteur_matrice_prec[b] = 0;
            }
        }

        // Remplissage de la matrice 8x8 (Dégradé fonctionnel en 4 vert 2 jaune 2 rouge)
        for (int col = 0; col < 8; col++) {
            int bande = col / 2; // Répartition des 8 colonnes sur les 4 bandes
            int limite_allumage = hauteur_matrice_prec[bande];

            for (int row = 0; row < 8; row++) {
                int index_led = (row * 8 + col) * 4;

                if (row < limite_allumage) {
                    if (row < 4) {
                        LED_MATRIX[index_led + 0] = 0x10; // Vert
                        LED_MATRIX[index_led + 1] = 0x00; 
                        LED_MATRIX[index_led + 2] = 0x00; 
                    } else if (row < 6) {
                        LED_MATRIX[index_led + 0] = 0x08; // Jaune
                        LED_MATRIX[index_led + 1] = 0x12; 
                        LED_MATRIX[index_led + 2] = 0x00; 
                    } else {
                        LED_MATRIX[index_led + 0] = 0x00; // Rouge
                        LED_MATRIX[index_led + 1] = 0x18; 
                        LED_MATRIX[index_led + 2] = 0x00; 
                    }
                    LED_MATRIX[index_led + 3] = 0x00; 
                } else {
                    LED_MATRIX[index_led + 0] = 0x00;
                    LED_MATRIX[index_led + 1] = 0x00;
                    LED_MATRIX[index_led + 2] = 0x00;
                    LED_MATRIX[index_led + 3] = 0x00;
                }
            }
        }

        // Affichage pour la matrice -> delay baissé de ms vers us pour un meilleur affichage (plus de clignotements)
        TX_64LEDS();
        __delay_us(100); 
    }
}

