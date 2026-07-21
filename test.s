.global my_symbol
.extern a

.section text
start:
    halt

.section data
my_symbol:
    .word 42
    .word my_symbol
    .skip 4
    .ascii "hi"
b:
    .word a

.end
