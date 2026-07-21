.global my_symbol

.section text
start:
    halt

.section data
my_symbol:
    .word 42
    .word my_symbol
    .skip 4
    .ascii "hi"

.end
