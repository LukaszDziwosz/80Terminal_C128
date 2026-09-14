; Fixed-address bridge between the Oscar64 WiC64 overlay and the official
; ACME WiC64 library.  The C side owns the mailbox in high RAM at $8200.
; The jump table is deliberately the first code at $9000.

!source "wic64.h"

MB_HOST       = $8200          ; zero-terminated host:port (80 bytes)
MB_TX         = $8280          ; outgoing bytes (512 bytes)
MB_RX         = $6000          ; incoming bytes (8192 bytes, below runtime stack)
MB_DISCARD    = $8e80          ; 128-byte overflow drain area
MB_ERROR      = $8f00          ; firmware status text (128 bytes)
MB_RX_LENGTH  = $8f80          ; little-endian copied receive length
MB_AVAILABLE  = $8f82          ; little-endian response from TCP_AVAILABLE
MB_TX_LENGTH  = $8f84          ; little-endian C -> bridge write length

WIC_TIMEOUT   = $80            ; bridge result: user-port timeout
WIC_TRUNCATED = $81            ; bridge result: TCP response exceeded buffer
WIC_LEGACY    = $82            ; bridge result: pre-2.0 firmware

* = $9000

    jmp bridge_detect
    jmp bridge_open
    jmp bridge_available
    jmp bridge_read
    jmp bridge_write
    jmp bridge_close
    jmp bridge_status_message

; A = 0 if the current official library can communicate with firmware >= 2.
bridge_detect: !zone bridge_detect {
    +wic64_detect
    bcc .responded
    lda #WIC_TIMEOUT
    rts
.responded:
    beq .ready
    lda #WIC_LEGACY
    rts
.ready:
    lda #0
    rts
}

; Open the zero-terminated host:port text supplied by C.
bridge_open: !zone bridge_open {
    ldy #0
.copy:
    lda MB_HOST,y
    sta open_request+4,y
    beq .done
    iny
    cpy #80
    bne .copy
    lda #WIC_TRUNCATED
    rts
.done:
    sty open_request+2
    lda #0
    sta open_request+3
    +wic64_execute open_request, bridge_reply, 20
    bcc .result
    lda #WIC_TIMEOUT
.result:
    rts
}

; Return WiC64's TCP_AVAILABLE response in MB_AVAILABLE.
bridge_available: !zone bridge_available {
    +wic64_execute available_request, available_reply
    bcc .result
    lda #WIC_TIMEOUT
    rts
.result:
    bne .done
    lda available_reply
    sta MB_AVAILABLE
    lda available_reply+1
    sta MB_AVAILABLE+1
    ; A is the status returned to C, not the high byte of the byte count.
    lda #0
.done:
    rts
}

; Receive a complete TCP_READ response safely.  The official library's
; low-level calls let us cap the mailbox; a larger response is drained before
; returning WIC_TRUNCATED so an oversized remote write cannot corrupt RAM.
bridge_read: !zone bridge_read {
    lda #0
    sta bridge_copied
    sta bridge_copied+1
    sta bridge_was_truncated
    lda #<MB_RX
    sta bridge_destination
    lda #>MB_RX
    sta bridge_destination+1

    +wic64_initialize
    bcc .initialized
    jmp .timeout
.initialized:
    +wic64_send_header read_request
    bcc .header_sent
    jmp .timeout
.header_sent:
    +wic64_send
    bcc .sent
    jmp .timeout
.sent:
    +wic64_receive_header
    bcc .response_header
    jmp .timeout
.response_header:
    beq .response_ok
    jmp .error
.response_ok:

.next:
    lda wic64_transfer_size
    ora wic64_transfer_size+1
    bne .has_data
    jmp .finished
.has_data:
    lda bridge_copied+1
    cmp #32
    bcc .room
    jmp .discard
.room:

    ; Transfer min(256, remaining) into the mailbox.
    lda bridge_destination
    sta wic64_response
    lda bridge_destination+1
    sta wic64_response+1
    lda wic64_transfer_size+1
    beq .last_chunk
    lda #0
    sta wic64_bytes_to_transfer
    lda #1
    sta wic64_bytes_to_transfer+1
    jmp .receive
.last_chunk:
    lda wic64_transfer_size
    sta wic64_bytes_to_transfer
    lda #0
    sta wic64_bytes_to_transfer+1
.receive:
    lda wic64_bytes_to_transfer
    sta bridge_count
    lda wic64_bytes_to_transfer+1
    sta bridge_count+1
    jsr wic64_receive
    bcc .received
    jmp .timeout
.received:
    clc
    lda bridge_destination
    adc bridge_count
    sta bridge_destination
    lda bridge_destination+1
    adc bridge_count+1
    sta bridge_destination+1
    clc
    lda bridge_copied
    adc bridge_count
    sta bridge_copied
    lda bridge_copied+1
    adc bridge_count+1
    sta bridge_copied+1
    jmp .next

.discard:
    lda #<MB_DISCARD
    sta wic64_response
    lda #>MB_DISCARD
    sta wic64_response+1
    lda wic64_transfer_size+1
    beq .discard_last
    lda #128
    sta wic64_bytes_to_transfer
    lda #0
    sta wic64_bytes_to_transfer+1
    jmp .discard_receive
.discard_last:
    lda wic64_transfer_size
    cmp #128
    bcc .discard_size_ready
    lda #128
.discard_size_ready:
    sta wic64_bytes_to_transfer
    lda #0
    sta wic64_bytes_to_transfer+1
.discard_receive:
    jsr wic64_receive
    bcs .timeout
    lda #1
    sta bridge_was_truncated
    jmp .next

.finished:
    +wic64_finalize
    lda bridge_copied
    sta MB_RX_LENGTH
    lda bridge_copied+1
    sta MB_RX_LENGTH+1
    lda bridge_was_truncated
    beq .ok
    lda #WIC_TRUNCATED
    rts
.ok:
    lda #0
    rts
.error:
    +wic64_finalize
    rts
.timeout:
    ; The library finalized the user port before returning with carry set.
    lda #WIC_TIMEOUT
    rts
}

; Copy the C-side TX mailbox into one contiguous request then send it.
bridge_write: !zone bridge_write {
    lda MB_TX_LENGTH
    sta bridge_count
    lda MB_TX_LENGTH+1
    sta bridge_count+1
    lda bridge_count+1
    beq .copy
    cmp #1
    bne .too_large
    lda bridge_count
    beq .copy
.too_large:
    lda #WIC_TRUNCATED
    rts
.copy:
    lda bridge_count
    sta write_request+2
    lda bridge_count+1
    sta write_request+3
    ldy #0
.copy_page:
    cpy bridge_count
    bne .copy_byte
    lda bridge_count+1
    beq .send
.copy_byte:
    lda MB_TX,y
    sta write_request+4,y
    iny
    bne .copy_page
    dec bridge_count+1
    bne .copy_page
.send:
    +wic64_execute write_request, bridge_reply, 10
    bcc .result
    lda #WIC_TIMEOUT
.result:
    rts
}

bridge_close: !zone bridge_close {
    +wic64_execute close_request, bridge_reply, 5
    bcc .result
    lda #WIC_TIMEOUT
.result:
    rts
}

; Fetch the human-readable status for the previous rejected WiC64 command.
; The official API defines this request as one-byte payload $01.
bridge_status_message: !zone bridge_status_message {
    +wic64_execute status_request, MB_ERROR, 5
    bcc .result
    lda #WIC_TIMEOUT
.result:
    rts
}

read_request:      !byte "R", WIC64_TCP_READ, $00, $00
available_request: !byte "R", WIC64_TCP_AVAILABLE, $00, $00
close_request:     !byte "R", WIC64_TCP_CLOSE, $00, $00
status_request:    !byte "R", WIC64_GET_STATUS_MESSAGE, $01, $00, $01
open_request:      !byte "R", WIC64_TCP_OPEN, $00, $00
                  !fill 80, $00
write_request:     !byte "R", WIC64_TCP_WRITE, $00, $00
                  !fill 512, $00
available_reply:   !fill 2, $00
bridge_reply:      !fill 4, $00
bridge_destination: !word 0
bridge_count:       !word 0
bridge_copied:      !word 0
bridge_was_truncated: !byte 0

; Aligning this include keeps its timing-sensitive transfer loops page-local,
; as required by the official WiC64 library.
!align $100, $00
!source "wic64.asm"
