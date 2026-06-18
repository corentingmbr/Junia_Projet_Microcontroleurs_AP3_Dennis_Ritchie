#include <xc.inc>

; NOTE : On a supprimé la section 'psect udata_acs'
; On utilise les registres physiques du PIC (FSR1 et TABLAT) comme variables de rechange

psect   txfunc,local,class=CODE,reloc=2

global _TX_64LEDS
global _pC
global _LED_MATRIX

_TX_64LEDS:
    ; Récupération du pointeur vers la matrice
    MOVFF _pC + 0, WREG
    MOVWF FSR0L, 0
    MOVFF _pC + 1, WREG
    MOVWF FSR0H, 0

    ; On utilise FSR1L à la place de 'byte_ctr'
    CLRF FSR1L, 0

byte_loop:
    MOVF POSTINC0, 0, 0       ; Charge l'octet de la matrice dans WREG et avance

    ; On utilise TABLAT à la place de 'data_reg'
    MOVWF TABLAT, 0

    ; On utilise FSR1H à la place de 'bit_ctr'
    MOVLW 8
    MOVWF FSR1H, 0

bit_loop:
    ; ON MET TOUT LE PORTB A 1 !!!
    MOVLW 0xFF
    MOVWF LATB, 0

    ; Test du bit 7 (le MSB) dans TABLAT
    BTFSC TABLAT, 7, 0
    BRA bit_one

bit_zero:
    ; ON MET TOUT LE PORTB A 0 !!!!!
    MOVLW 0x00
    MOVWF LATB, 0

    RLCF TABLAT, 1, 0       ; Décalage du bit suivant dans TABLAT
    NOP
    NOP
    BRA next_bit

bit_one:
    ; on met tout le PORTB à 0
    NOP
    NOP
    NOP
    MOVLW 0x00
    MOVWF LATB, 0

    RLCF TABLAT, 1, 0
    NOP
    NOP

next_bit:
    ; Décrémente le compteur de bits FSR1H
    DECFSZ FSR1H, 1, 0
    BRA bit_loop

    ; Décrémente le compteur d'octets FSR1L
    DECFSZ FSR1L, 1, 0
    BRA byte_loop

    RETURN