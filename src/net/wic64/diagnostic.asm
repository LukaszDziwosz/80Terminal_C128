; WiC64 adapter bring-up screen for the C128's 80-column display.
;
; This is deliberately built with the current official WiC64 ACME library,
; rather than a hand-ported user-port protocol.  The launcher loads it at
; $9000 and calls this entry with JSR, so RTS returns cleanly to the launcher.

!source "wic64.h"

!macro print .string {
    lda #<.string
    sta $fb
    lda #>.string
    sta $fc
    jsr print_string
}

* = $9000

main:
    jsr $c142                   ; C128 KERNAL: clear the active 80-col screen
    +print title
    +print checking_text

    ; The official API uses carry for a user-port timeout and Z for firmware
    ; 2.0.0 or later.  This call also exercises the exact transfer handshake
    ; used by every later TCP request.
    +wic64_detect
    bcc .device_responded
    jmp device_not_present
.device_responded:
    beq .current_firmware
    jmp legacy_firmware
.current_firmware:

    +print found_text
    +wic64_execute version_request, version_response
    bcc .version_received
    jmp request_timeout
.version_received:
    beq .version_ok
    jmp request_error
.version_ok:
    +print version_prefix
    +print version_response
    +print newline

    +wic64_execute ip_request, ip_response
    bcc .ip_received
    jmp request_timeout
.ip_received:
    beq .ip_ok
    jmp request_error
.ip_ok:
    +print ip_prefix
    +print ip_response
    +print newline
    jmp wait_for_key

device_not_present:
    +print device_error
    jmp wait_for_key

legacy_firmware:
    +print legacy_error
    jmp wait_for_key

request_timeout:
    +print timeout_error
    jmp wait_for_key

request_error:
    +print error_prefix
    ; Ask for the firmware's explanatory text after a non-zero status code.
    +wic64_execute status_request, status_response
    bcs status_unavailable
    bne status_unavailable
    +print status_response
    +print newline
    jmp wait_for_key

status_unavailable:
    +print status_unavailable_text

wait_for_key:
    +print continue_text
.wait:
    jsr $ffe4                   ; KERNAL GETIN
    beq .wait
    rts

print_string:
    ldy #$00
.next:
    lda ($fb),y
    beq .done
    jsr $ffd2                   ; KERNAL CHROUT, routed to the active display
    iny
    bne .next
.done:
    rts

title:                   !pet "80terminal / wic64", $0d, $0d, $00
checking_text:           !pet "checking user port and firmware...", $0d, $00
found_text:              !pet "wic64 detected", $0d, $00
version_prefix:          !pet "firmware: ", $00
ip_prefix:               !pet "ip address: ", $00
device_error:            !pet "no wic64 detected, or it did not respond", $0d, $00
legacy_error:            !pet "legacy firmware: version 2.0.0 or newer is required", $0d, $00
timeout_error:           !pet "wic64 request timed out", $0d, $00
error_prefix:            !pet "wic64 error: ", $00
status_unavailable_text: !pet "status message unavailable", $0d, $00
continue_text:           !pet $0d, "press any key to return to the launcher", $00
newline:                 !pet $0d, $00

; Standard-protocol requests documented by the official WiC64 library.
version_request:         !byte "R", WIC64_GET_VERSION_STRING, $00, $00
version_response:        !fill 32, $00
ip_request:              !byte "R", WIC64_GET_IP, $00, $00
ip_response:             !fill 16, $00
status_request:          !byte "R", WIC64_GET_STATUS_MESSAGE, $01, $00, $01
status_response:         !fill 64, $00

; Keep this include last and aligned as recommended by the library: its
; transfer loops rely on page-contained critical sections for reliable speed.
!align $100, $00
!source "wic64.asm"
