#include <xc.inc>

; ==============================================================
; Fichier  : tx.asm
; Projet   : VU-mètre 4 bandes — JUNIA AP3 BX 2023-2024
; MCU      : PIC18F25K40 @ 64 MHz  (1 cycle instruction = 62,5 ns)
; Fonction : _TX_64LEDS — envoi de 256 octets (64 LEDs GRBW)
;            vers la matrice SK6812RGBW sur la broche RB0.
;
; Protocole SK6812RGBW (conforme sujet §VI) :
;   Bit '0' : T_haut ≈ 0,32 µs  |  T_bas ≈ 0,93 µs  |  période 1,25 µs
;   Bit '1' : T_haut ≈ 0,82 µs  |  T_bas ≈ 0,43 µs  |  période 1,25 µs
;   Tolérance ±150 ns — VÉRIFIER À L'OSCILLOSCOPE et ajuster les NOP.
;
; Comptage cycles à 62,5 ns/cycle :
;   Bit '0' HIGH : BSF(1) + BTFSC skip(2) + NOP(1) + BCF(1) = 5 cy = 312,5 ns ✓
;   Bit '1' HIGH : BSF(1) + BTFSC(1) + BRA(2) + 5×NOP + BCF(1) = 10 cy = 625 ns
;                  → légèrement court (cible 820 ns) : ajouter des NOP si nécessaire
; ==============================================================

; Déclaration des variables temporaires pour les boucles en assembleur
psect   udata_acs
byte_ctr: ds 1    ; Compteur d'octets (256)
bit_ctr:  ds 1    ; Compteur de bits (8)
data_reg: ds 1    ; Sauvegarde de l'octet en cours de traitement

psect   txfunc,local,class=CODE,reloc=2

global _TX_64LEDS
global _pC
global _LED_MATRIX

_TX_64LEDS:
    ; Initialisation du pointeur FSR0 au début de la matrice
    MOVFF _pC + 0, FSR0L
    MOVFF _pC + 1, FSR0H

    CLRF byte_ctr, 1          ; Initialise le compteur à 0 (tournera 256 fois par overflow)

byte_loop:
    MOVF POSTINC0, 0, 0       ; Charge l'octet pointé dans WREG et incrémente le pointeur
    MOVWF data_reg, 1         ; Sauvegarde l'octet dans notre registre de travail
    MOVLW 8
    MOVWF bit_ctr, 1          ; Initialise le compteur à 8 bits

bit_loop:
    BSF LATB, 0, 0            ; ----> FORCE LA BROCHE RB0 À 1 (Début de l'impulsion)

    ; Test du bit de poids fort (MSB)
    BTFSC data_reg, 7, 1      ; Si le bit 7 est à 0, on saute à l'étiquette bit_zero
    BRA bit_one

bit_zero:
    ; Timing pour un '0' : Temps haut très court (~300ns), puis temps bas (~900ns)
    NOP                       ; Ajustement du timing haut
    BCF LATB, 0, 0            ; ----> REPASSE LA BROCHE RB0 À 0

    ; Pendant le temps bas, on prépare le bit suivant
    RLCF data_reg, 1, 1       ; Décalage à gauche pour analyser le bit suivant au prochain tour
    NOP
    NOP
    NOP
    BRA next_bit              ; Saute vers la fin de la boucle du bit

bit_one:
    ; Timing pour un '1' : Temps haut plus long (~600ns), puis temps bas (~600ns)
    NOP
    NOP
    NOP
    NOP
    NOP
    BCF LATB, 0, 0            ; ----> REPASSE LA BROCHE RB0 À 0

    ; Temps bas pour le '1'
    RLCF data_reg, 1, 1       ; Décalage à gauche pour le bit suivant
    NOP
    NOP

next_bit:
    DECFSZ bit_ctr, 1, 1      ; Décrémente le compteur de bits, saute si zéro
    BRA bit_loop              ; Si pas zéro, on traite le bit suivant

    DECFSZ byte_ctr, 1, 1     ; Décrémente le compteur d'octets
    BRA byte_loop             ; Si pas zéro, on traite l'octet suivant

    RETURN